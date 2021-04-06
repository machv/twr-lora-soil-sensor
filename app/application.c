#include <application.h>
#include <at.h>

#define SEND_DATA_INTERVAL                  (15 * 60 * 1000)
#define MEASURE_INTERVAL                    (5 * 60 * 1000)

#define MAX_SOIL_SENSORS                    5

// LED instance
twr_led_t led;

// Button instance
twr_button_t button;

// Reed magnet instance
twr_button_t door_sensor;
bool is_closed = false;

// Thermometer instance
twr_tmp112_t tmp112;

// Soil sensors instance
twr_soil_sensor_t soil_sensor;
twr_soil_sensor_sensor_t sensors[MAX_SOIL_SENSORS];

// Lora instance
twr_cmwx1zzabz_t lora;

// Data streams
TWR_DATA_STREAM_FLOAT_BUFFER(sm_voltage_buffer, 8)
twr_data_stream_t sm_voltage;

TWR_DATA_STREAM_FLOAT_BUFFER(sm_temperature_buffer, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
twr_data_stream_t sm_temperature;

// for soil sensor probes
TWR_DATA_STREAM_FLOAT_BUFFER(sm_soil_temperature_buffer_0, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
TWR_DATA_STREAM_FLOAT_BUFFER(sm_soil_temperature_buffer_1, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
TWR_DATA_STREAM_FLOAT_BUFFER(sm_soil_temperature_buffer_2, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
TWR_DATA_STREAM_FLOAT_BUFFER(sm_soil_temperature_buffer_3, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
TWR_DATA_STREAM_FLOAT_BUFFER(sm_soil_temperature_buffer_4, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))

twr_data_stream_t sm_soil_temperature_0;
twr_data_stream_t sm_soil_temperature_1;
twr_data_stream_t sm_soil_temperature_2;
twr_data_stream_t sm_soil_temperature_3;
twr_data_stream_t sm_soil_temperature_4;

twr_data_stream_t *sm_soil_temperature[] =
{
    &sm_soil_temperature_0,
    &sm_soil_temperature_1,
    &sm_soil_temperature_2,
    &sm_soil_temperature_3,
    &sm_soil_temperature_4
};

TWR_DATA_STREAM_FLOAT_BUFFER(sm_soil_humidity_raw_buffer_0, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
TWR_DATA_STREAM_FLOAT_BUFFER(sm_soil_humidity_raw_buffer_1, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
TWR_DATA_STREAM_FLOAT_BUFFER(sm_soil_humidity_raw_buffer_2, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
TWR_DATA_STREAM_FLOAT_BUFFER(sm_soil_humidity_raw_buffer_3, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
TWR_DATA_STREAM_FLOAT_BUFFER(sm_soil_humidity_raw_buffer_4, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))

twr_data_stream_t sm_soil_humidity_raw_0;
twr_data_stream_t sm_soil_humidity_raw_1;
twr_data_stream_t sm_soil_humidity_raw_2;
twr_data_stream_t sm_soil_humidity_raw_3;
twr_data_stream_t sm_soil_humidity_raw_4;

twr_data_stream_t *sm_soil_humidity_raw[] =
{
    &sm_soil_humidity_raw_0,
    &sm_soil_humidity_raw_1,
    &sm_soil_humidity_raw_2,
    &sm_soil_humidity_raw_3,
    &sm_soil_humidity_raw_4
};

twr_scheduler_task_id_t battery_measure_task_id;

enum {
    HEADER_BOOT         = 0x00,
    HEADER_UPDATE       = 0x01,
    HEADER_BUTTON_CLICK = 0x02,
    HEADER_BUTTON_HOLD  = 0x03,
    HEADER_DOOR_OPEN    = 0x04,
    HEADER_DOOR_CLOSE   = 0x05,

} header = HEADER_BOOT;

void button_event_handler(twr_button_t *self, twr_button_event_t event, void *event_param)
{
    if (event == TWR_BUTTON_EVENT_CLICK)
    {
        header = HEADER_BUTTON_CLICK;

        twr_scheduler_plan_now(0);
    }
    else if (event == TWR_BUTTON_EVENT_HOLD)
    {
        header = HEADER_BUTTON_HOLD;

        twr_scheduler_plan_now(0);
    }
}

void door_event_handler(twr_button_t *self, twr_button_event_t event, void *event_param)
{
    if (event == TWR_BUTTON_EVENT_PRESS)
    {
        is_closed = true;
        header = HEADER_DOOR_CLOSE;

        twr_scheduler_plan_now(0);
    }
    else if (event == TWR_BUTTON_EVENT_RELEASE)
    {
        is_closed = false;
        header = HEADER_DOOR_OPEN;

        twr_scheduler_plan_now(0);
    }
}

void battery_event_handler(twr_module_battery_event_t event, void *event_param)
{
    (void) event;
    (void) event_param;

    float voltage = NAN;

    if (event == TWR_MODULE_BATTERY_EVENT_UPDATE)
    {
        if (twr_module_battery_get_voltage(&voltage))
        {
            twr_data_stream_feed(&sm_voltage, &voltage);
        }
    }
}

void battery_measure_task(void *param)
{
    if (!twr_module_battery_measure())
    {
        twr_scheduler_plan_current_now();
    }
}

void tmp112_event_handler(twr_tmp112_t *self, twr_tmp112_event_t event, void *event_param)
{
    if (event == TWR_TMP112_EVENT_UPDATE)
    {
        float temperature;

        if (twr_tmp112_get_temperature_celsius(self, &temperature))
        {
            twr_data_stream_feed(&sm_temperature, &temperature);
        }
    }
}

void soil_sensor_event_handler(twr_soil_sensor_t *self, uint64_t device_address, twr_soil_sensor_event_t event, void *event_param)
{
    if (event == TWR_SOIL_SENSOR_EVENT_UPDATE)
    {
        int index = twr_soil_sensor_get_index_by_device_address(self, device_address);
        if (index < 0)
            return;

        float temperature;
        if (twr_soil_sensor_get_temperature_celsius(self, device_address, &temperature))
        {
            twr_data_stream_feed(sm_soil_temperature[index], &temperature);
        }

        uint16_t raw_cap_u16;
        if (twr_soil_sensor_get_cap_raw(self, device_address, &raw_cap_u16))
        {
            int raw_cap = (int)raw_cap_u16;
            twr_data_stream_feed(sm_soil_humidity_raw[index], &raw_cap);
            
            // Experimental - send percent moisture value based on sensor calibration
            //int moisture;
            //if(twr_soil_sensor_get_moisture(self, device_address, &moisture)) 
            //{
            //    twr_log_debug("Humidity = %i", moisture);
            //    snprintf(topic, sizeof(topic), "soil-sensor/%llx/moisture", device_address);
            //    twr_radio_pub_int(topic, &moisture);
            //}
        }
    }
    else if (event == TWR_SOIL_SENSOR_EVENT_ERROR)
    {
        //int error = twr_soil_sensor_get_error(self);
        //twr_atci_printf("$ERROR: \"Soil Sensor Error: %d\",", error);
    }
}

void lora_callback(twr_cmwx1zzabz_t *self, twr_cmwx1zzabz_event_t event, void *event_param)
{
    if (event == TWR_CMWX1ZZABZ_EVENT_ERROR)
    {
        twr_led_set_mode(&led, TWR_LED_MODE_BLINK_FAST);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_SEND_MESSAGE_START)
    {
        twr_led_set_mode(&led, TWR_LED_MODE_ON);

        twr_scheduler_plan_relative(battery_measure_task_id, 20);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_SEND_MESSAGE_DONE)
    {
        twr_led_set_mode(&led, TWR_LED_MODE_OFF);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_READY)
    {
        twr_led_set_mode(&led, TWR_LED_MODE_OFF);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_JOIN_SUCCESS)
    {
        twr_atci_printf("$JOIN_OK");
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_JOIN_ERROR)
    {
        twr_atci_printf("$JOIN_ERROR");
    }
}

bool at_send(void)
{
    twr_scheduler_plan_now(0);

    return true;
}

bool at_status(void)
{
    float value_avg = NAN;

    if (twr_data_stream_get_average(&sm_voltage, &value_avg))
    {
        twr_atci_printf("$STATUS: \"Voltage\",%.1f", value_avg);
    }
    else
    {
        twr_atci_printf("$STATUS: \"Voltage\",");
    }

    twr_atci_printf("$STATUS: \"Door\",%s", is_closed ? "closed" : "open");

    if (twr_data_stream_get_average(&sm_temperature, &value_avg))
    {
        twr_atci_printf("$STATUS: \"Temperature\",%.1f", value_avg);
    }
    else
    {
        twr_atci_printf("$STATUS: \"Temperature\",");
    }

    int sensors_count = twr_soil_sensor_get_sensor_found(&soil_sensor);
    twr_atci_printf("$STATUS: \"Soil Sensors Found\",%d", sensors_count);

    for (int i = 0; i < sensors_count; i++) {
        if(twr_data_stream_get_average(sm_soil_temperature[i], &value_avg))
        {
            twr_atci_printf("$STATUS: \"[%d] Soil Temperature\",%.1f", i, value_avg);
        } 
        else
        {
            twr_atci_printf("$STATUS: \"[%d] Soil Temperature\",", i);
        }
        
        int humidity_avg;
        if(twr_data_stream_get_average(sm_soil_humidity_raw[i], &humidity_avg))
        {
            twr_atci_printf("$STATUS: \"[%d] Soil Raw humidity\",%i", i, humidity_avg);
        } 
        else
        {
            twr_atci_printf("$STATUS: \"[%d] Soil Raw humidity\",", i);
        }
    }

    return true;
}

void application_init(void)
{
    twr_log_init(TWR_LOG_LEVEL_DUMP, TWR_LOG_TIMESTAMP_ABS);

    twr_data_stream_init(&sm_voltage, 1, &sm_voltage_buffer);
    twr_data_stream_init(&sm_temperature, 1, &sm_temperature_buffer);

    // Init stream buffers for soil sensors
    twr_data_stream_init(&sm_soil_temperature_0, 1, &sm_soil_temperature_buffer_0);
    twr_data_stream_init(&sm_soil_temperature_1, 1, &sm_soil_temperature_buffer_1);
    twr_data_stream_init(&sm_soil_temperature_2, 1, &sm_soil_temperature_buffer_2);
    twr_data_stream_init(&sm_soil_temperature_3, 1, &sm_soil_temperature_buffer_3);
    twr_data_stream_init(&sm_soil_temperature_4, 1, &sm_soil_temperature_buffer_4);
    twr_data_stream_init(&sm_soil_humidity_raw_0, 1, &sm_soil_humidity_raw_buffer_0);
    twr_data_stream_init(&sm_soil_humidity_raw_1, 1, &sm_soil_humidity_raw_buffer_1);
    twr_data_stream_init(&sm_soil_humidity_raw_2, 1, &sm_soil_humidity_raw_buffer_2);
    twr_data_stream_init(&sm_soil_humidity_raw_3, 1, &sm_soil_humidity_raw_buffer_3);
    twr_data_stream_init(&sm_soil_humidity_raw_4, 1, &sm_soil_humidity_raw_buffer_4);

    // Initialize LED
    twr_led_init(&led, TWR_GPIO_LED, false, false);
    twr_led_set_mode(&led, TWR_LED_MODE_ON);  

    // Initialize button
    twr_button_init(&button, TWR_GPIO_BUTTON, TWR_GPIO_PULL_DOWN, false);
    twr_button_set_event_handler(&button, button_event_handler, NULL);

    // Initialize reed sensor
    twr_button_init(&door_sensor, TWR_GPIO_P9, TWR_GPIO_PULL_DOWN, false);
    twr_button_set_event_handler(&door_sensor, door_event_handler, NULL);

    // Initialize thermometer sensor on core module
    twr_tmp112_init(&tmp112, TWR_I2C_I2C0, 0x49);
    twr_tmp112_set_event_handler(&tmp112, tmp112_event_handler, NULL);
    twr_tmp112_set_update_interval(&tmp112, MEASURE_INTERVAL);

    // Initialize soil sensor
    twr_soil_sensor_init_multiple(&soil_sensor, sensors, MAX_SOIL_SENSORS);
    twr_soil_sensor_set_event_handler(&soil_sensor, soil_sensor_event_handler, NULL);
    twr_soil_sensor_set_update_interval(&soil_sensor, MEASURE_INTERVAL);

    // Initialize battery
    twr_module_battery_init();
    twr_module_battery_set_event_handler(battery_event_handler, NULL);
    battery_measure_task_id = twr_scheduler_register(battery_measure_task, NULL, 2020);

    // Initialize lora module
    twr_cmwx1zzabz_init(&lora, TWR_UART_UART1);
    twr_cmwx1zzabz_set_event_handler(&lora, lora_callback, NULL);
    twr_cmwx1zzabz_set_mode(&lora, TWR_CMWX1ZZABZ_CONFIG_MODE_ABP);
    twr_cmwx1zzabz_set_class(&lora, TWR_CMWX1ZZABZ_CONFIG_CLASS_A);

    // Initialize AT command interface
    at_init(&led, &lora);
    static const twr_atci_command_t commands[] = {
            AT_LORA_COMMANDS,
            {"$SEND", at_send, NULL, NULL, NULL, "Immediately send packet"},
            {"$STATUS", at_status, NULL, NULL, NULL, "Show status"},
            AT_LED_COMMANDS,
            TWR_ATCI_COMMAND_CLAC,
            TWR_ATCI_COMMAND_HELP
    };
    twr_atci_init(commands, TWR_ATCI_COMMANDS_LENGTH(commands));

    // Plan application_task to be run after 10 seconds
    twr_scheduler_plan_current_relative(10 * 1000);
}

void application_task(void)
{
    if (!twr_cmwx1zzabz_is_ready(&lora))
    {
        twr_scheduler_plan_current_relative(100);

        return;
    }
               
    // Byte (header) + Byte (Voltage) + Byte (Door State) + Short (Core Module Temperature) + Sensors * (2 bytes (temp) + 2 bytes (cap))
    static uint8_t buffer[1 + 1 + 1 + 2 + 4 * MAX_SOIL_SENSORS]; 

    memset(buffer, 0xff, sizeof(buffer));

    buffer[0] = header;

    float voltage_avg = NAN;
    twr_data_stream_get_average(&sm_voltage, &voltage_avg);

    if (!isnan(voltage_avg))
    {
        buffer[1] = ceil(voltage_avg * 10.f);
    }

    // door open/closed?
    buffer[2] = is_closed;

    // temperature on Core module
    float temperature_avg = NAN;
    twr_data_stream_get_average(&sm_temperature, &temperature_avg);
    if (!isnan(temperature_avg))
    {
        int16_t temperature_i16 = (int16_t) (temperature_avg * 10.f);

        buffer[3] = (temperature_i16 & 0xFF00) >> 8;
        buffer[4] = temperature_i16 & 0x00FF;
    }

    size_t pos = 5;

    // Soil sensors
    int sensors_count = twr_soil_sensor_get_sensor_found(&soil_sensor);
    for (int i = 0; i < sensors_count; i++) {
        // temperature
        float soil_temperature_avg = NAN;
        twr_data_stream_get_average(sm_soil_temperature[i], &soil_temperature_avg);

        if (!isnan(soil_temperature_avg))
        {
            int16_t soil_temperature_i16 = (int16_t) (soil_temperature_avg * 10.f);

            buffer[pos++] = (soil_temperature_i16 & 0xFF00) >> 8;
            buffer[pos++] = soil_temperature_i16 & 0x00FF;
        } 
        else
        {
            pos += 2;
        }
        
        // soil 
        int soil_humidity_raw_avg;
        if(twr_data_stream_get_average(sm_soil_humidity_raw[i], &soil_humidity_raw_avg))
        {
            uint16_t soil_humidity_raw_i16 = (uint16_t) (soil_humidity_raw_avg);

            buffer[pos++] = soil_humidity_raw_i16 >> 8;
            buffer[pos++] = soil_humidity_raw_i16;
        }
        else
        {
            pos += 2;
        }
    }

    twr_cmwx1zzabz_send_message(&lora, buffer, sizeof(buffer));

    static char tmp[sizeof(buffer) * 2 + 1];
    for (size_t i = 0; i < sizeof(buffer); i++)
    {
        sprintf(tmp + i * 2, "%02x", buffer[i]);
    }

    twr_atci_printf("$SEND: %s", tmp);

    header = HEADER_UPDATE;

    twr_scheduler_plan_current_relative(SEND_DATA_INTERVAL);
}
