#include <application.h>
#include <at.h>

#define SEND_DATA_INTERVAL                  (15 * 60 * 1000)
#define MEASURE_INTERVAL                    (5 * 60 * 1000)

#define MAX_SOIL_SENSORS                    5

// LED instance
bc_led_t led;

// Button instance
bc_button_t button;

// Reed magnet instance
bc_button_t door_sensor;
bool is_closed = false;

// Thermometer instance
bc_tmp112_t tmp112;

// Soil sensors instance
bc_soil_sensor_t soil_sensor;
bc_soil_sensor_sensor_t sensors[MAX_SOIL_SENSORS];

// Lora instance
bc_cmwx1zzabz_t lora;

// Data streams
BC_DATA_STREAM_FLOAT_BUFFER(sm_voltage_buffer, 8)
bc_data_stream_t sm_voltage;

BC_DATA_STREAM_FLOAT_BUFFER(sm_temperature_buffer, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
bc_data_stream_t sm_temperature;

// for soil sensor probes
BC_DATA_STREAM_FLOAT_BUFFER(sm_soil_temperature_buffer_0, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
BC_DATA_STREAM_FLOAT_BUFFER(sm_soil_temperature_buffer_1, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
BC_DATA_STREAM_FLOAT_BUFFER(sm_soil_temperature_buffer_2, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
BC_DATA_STREAM_FLOAT_BUFFER(sm_soil_temperature_buffer_3, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
BC_DATA_STREAM_FLOAT_BUFFER(sm_soil_temperature_buffer_4, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))

bc_data_stream_t sm_soil_temperature_0;
bc_data_stream_t sm_soil_temperature_1;
bc_data_stream_t sm_soil_temperature_2;
bc_data_stream_t sm_soil_temperature_3;
bc_data_stream_t sm_soil_temperature_4;

bc_data_stream_t *sm_soil_temperature[] =
{
    &sm_soil_temperature_0,
    &sm_soil_temperature_1,
    &sm_soil_temperature_2,
    &sm_soil_temperature_3,
    &sm_soil_temperature_4
};

BC_DATA_STREAM_FLOAT_BUFFER(sm_soil_humidity_raw_buffer_0, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
BC_DATA_STREAM_FLOAT_BUFFER(sm_soil_humidity_raw_buffer_1, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
BC_DATA_STREAM_FLOAT_BUFFER(sm_soil_humidity_raw_buffer_2, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
BC_DATA_STREAM_FLOAT_BUFFER(sm_soil_humidity_raw_buffer_3, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
BC_DATA_STREAM_FLOAT_BUFFER(sm_soil_humidity_raw_buffer_4, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))

bc_data_stream_t sm_soil_humidity_raw_0;
bc_data_stream_t sm_soil_humidity_raw_1;
bc_data_stream_t sm_soil_humidity_raw_2;
bc_data_stream_t sm_soil_humidity_raw_3;
bc_data_stream_t sm_soil_humidity_raw_4;

bc_data_stream_t *sm_soil_humidity_raw[] =
{
    &sm_soil_humidity_raw_0,
    &sm_soil_humidity_raw_1,
    &sm_soil_humidity_raw_2,
    &sm_soil_humidity_raw_3,
    &sm_soil_humidity_raw_4
};

bc_scheduler_task_id_t battery_measure_task_id;

enum {
    HEADER_BOOT         = 0x00,
    HEADER_UPDATE       = 0x01,
    HEADER_BUTTON_CLICK = 0x02,
    HEADER_BUTTON_HOLD  = 0x03,
    HEADER_DOOR_OPEN    = 0x04,
    HEADER_DOOR_CLOSE   = 0x05,

} header = HEADER_BOOT;

void button_event_handler(bc_button_t *self, bc_button_event_t event, void *event_param)
{
    if (event == BC_BUTTON_EVENT_CLICK)
    {
        header = HEADER_BUTTON_CLICK;

        bc_scheduler_plan_now(0);
    }
    else if (event == BC_BUTTON_EVENT_HOLD)
    {
        header = HEADER_BUTTON_HOLD;

        bc_scheduler_plan_now(0);
    }
}

void door_event_handler(bc_button_t *self, bc_button_event_t event, void *event_param)
{
    if (event == BC_BUTTON_EVENT_PRESS)
    {
        is_closed = true;
        header = HEADER_DOOR_CLOSE;

        bc_scheduler_plan_now(0);
    }
    else if (event == BC_BUTTON_EVENT_RELEASE)
    {
        is_closed = false;
        header = HEADER_DOOR_OPEN;

        bc_scheduler_plan_now(0);
    }
}

void battery_event_handler(bc_module_battery_event_t event, void *event_param)
{
    (void) event;
    (void) event_param;

    float voltage = NAN;

    if (event == BC_MODULE_BATTERY_EVENT_UPDATE)
    {
        if (bc_module_battery_get_voltage(&voltage))
        {
            bc_data_stream_feed(&sm_voltage, &voltage);
        }
    }
}

void battery_measure_task(void *param)
{
    if (!bc_module_battery_measure())
    {
        bc_scheduler_plan_current_now();
    }
}

void tmp112_event_handler(bc_tmp112_t *self, bc_tmp112_event_t event, void *event_param)
{
    if (event == BC_TMP112_EVENT_UPDATE)
    {
        float temperature;

        if (bc_tmp112_get_temperature_celsius(self, &temperature))
        {
            bc_data_stream_feed(&sm_temperature, &temperature);
        }
    }
}

void soil_sensor_event_handler(bc_soil_sensor_t *self, uint64_t device_address, bc_soil_sensor_event_t event, void *event_param)
{
    if (event == BC_SOIL_SENSOR_EVENT_UPDATE)
    {
        int index = bc_soil_sensor_get_index_by_device_address(self, device_address);
        if (index < 0)
            return;

        float temperature;
        if (bc_soil_sensor_get_temperature_celsius(self, device_address, &temperature))
        {
            bc_data_stream_feed(sm_soil_temperature[index], &temperature);
        }

        uint16_t raw_cap_u16;
        if (bc_soil_sensor_get_cap_raw(self, device_address, &raw_cap_u16))
        {
            int raw_cap = (int)raw_cap_u16;
            bc_data_stream_feed(sm_soil_humidity_raw[index], &raw_cap);
            
            // Experimental - send percent moisture value based on sensor calibration
            //int moisture;
            //if(bc_soil_sensor_get_moisture(self, device_address, &moisture)) 
            //{
            //    bc_log_debug("Humidity = %i", moisture);
            //    snprintf(topic, sizeof(topic), "soil-sensor/%llx/moisture", device_address);
            //    bc_radio_pub_int(topic, &moisture);
            //}
        }
    }
    else if (event == BC_SOIL_SENSOR_EVENT_ERROR)
    {
        //int error = bc_soil_sensor_get_error(self);
        //bc_atci_printf("$ERROR: \"Soil Sensor Error: %d\",", error);
    }
}

void lora_callback(bc_cmwx1zzabz_t *self, bc_cmwx1zzabz_event_t event, void *event_param)
{
    if (event == BC_CMWX1ZZABZ_EVENT_ERROR)
    {
        bc_led_set_mode(&led, BC_LED_MODE_BLINK_FAST);
    }
    else if (event == BC_CMWX1ZZABZ_EVENT_SEND_MESSAGE_START)
    {
        bc_led_set_mode(&led, BC_LED_MODE_ON);

        bc_scheduler_plan_relative(battery_measure_task_id, 20);
    }
    else if (event == BC_CMWX1ZZABZ_EVENT_SEND_MESSAGE_DONE)
    {
        bc_led_set_mode(&led, BC_LED_MODE_OFF);
    }
    else if (event == BC_CMWX1ZZABZ_EVENT_READY)
    {
        bc_led_set_mode(&led, BC_LED_MODE_OFF);
    }
    else if (event == BC_CMWX1ZZABZ_EVENT_JOIN_SUCCESS)
    {
        bc_atci_printf("$JOIN_OK");
    }
    else if (event == BC_CMWX1ZZABZ_EVENT_JOIN_ERROR)
    {
        bc_atci_printf("$JOIN_ERROR");
    }
}

bool at_send(void)
{
    bc_scheduler_plan_now(0);

    return true;
}

bool at_status(void)
{
    float value_avg = NAN;

    if (bc_data_stream_get_average(&sm_voltage, &value_avg))
    {
        bc_atci_printf("$STATUS: \"Voltage\",%.1f", value_avg);
    }
    else
    {
        bc_atci_printf("$STATUS: \"Voltage\",");
    }

    bc_atci_printf("$STATUS: \"Door\",%s", is_closed ? "closed" : "open");

    if (bc_data_stream_get_average(&sm_temperature, &value_avg))
    {
        bc_atci_printf("$STATUS: \"Temperature\",%.1f", value_avg);
    }
    else
    {
        bc_atci_printf("$STATUS: \"Temperature\",");
    }

    int sensors_count = bc_soil_sensor_get_sensor_found(&soil_sensor);
    bc_atci_printf("$STATUS: \"Soil Sensors Found\",%d", sensors_count);

    for (int i = 0; i < sensors_count; i++) {
        if(bc_data_stream_get_average(sm_soil_temperature[i], &value_avg))
        {
            bc_atci_printf("$STATUS: \"[%d] Soil Temperature\",%.1f", i, value_avg);
        } 
        else
        {
            bc_atci_printf("$STATUS: \"[%d] Soil Temperature\",", i);
        }
        
        int humidity_avg;
        if(bc_data_stream_get_average(sm_soil_humidity_raw[i], &humidity_avg))
        {
            bc_atci_printf("$STATUS: \"[%d] Soil Raw humidity\",%i", i, humidity_avg);
        } 
        else
        {
            bc_atci_printf("$STATUS: \"[%d] Soil Raw humidity\",", i);
        }
    }

    return true;
}

void application_init(void)
{
    bc_log_init(BC_LOG_LEVEL_DUMP, BC_LOG_TIMESTAMP_ABS);

    bc_data_stream_init(&sm_voltage, 1, &sm_voltage_buffer);
    bc_data_stream_init(&sm_temperature, 1, &sm_temperature_buffer);

    // Init stream buffers for soil sensors
    bc_data_stream_init(&sm_soil_temperature_0, 1, &sm_soil_temperature_buffer_0);
    bc_data_stream_init(&sm_soil_temperature_1, 1, &sm_soil_temperature_buffer_1);
    bc_data_stream_init(&sm_soil_temperature_2, 1, &sm_soil_temperature_buffer_2);
    bc_data_stream_init(&sm_soil_temperature_3, 1, &sm_soil_temperature_buffer_3);
    bc_data_stream_init(&sm_soil_temperature_4, 1, &sm_soil_temperature_buffer_4);
    bc_data_stream_init(&sm_soil_humidity_raw_0, 1, &sm_soil_humidity_raw_buffer_0);
    bc_data_stream_init(&sm_soil_humidity_raw_1, 1, &sm_soil_humidity_raw_buffer_1);
    bc_data_stream_init(&sm_soil_humidity_raw_2, 1, &sm_soil_humidity_raw_buffer_2);
    bc_data_stream_init(&sm_soil_humidity_raw_3, 1, &sm_soil_humidity_raw_buffer_3);
    bc_data_stream_init(&sm_soil_humidity_raw_4, 1, &sm_soil_humidity_raw_buffer_4);

    // Initialize LED
    bc_led_init(&led, BC_GPIO_LED, false, false);
    bc_led_set_mode(&led, BC_LED_MODE_ON);  

    // Initialize button
    bc_button_init(&button, BC_GPIO_BUTTON, BC_GPIO_PULL_DOWN, false);
    bc_button_set_event_handler(&button, button_event_handler, NULL);

    // Initialize reed sensor
    bc_button_init(&door_sensor, BC_GPIO_P9, BC_GPIO_PULL_DOWN, false);
    bc_button_set_event_handler(&door_sensor, door_event_handler, NULL);

    // Initialize thermometer sensor on core module
    bc_tmp112_init(&tmp112, BC_I2C_I2C0, 0x49);
    bc_tmp112_set_event_handler(&tmp112, tmp112_event_handler, NULL);
    bc_tmp112_set_update_interval(&tmp112, MEASURE_INTERVAL);

    // Initialize soil sensor
    bc_soil_sensor_init_multiple(&soil_sensor, sensors, MAX_SOIL_SENSORS);
    bc_soil_sensor_set_event_handler(&soil_sensor, soil_sensor_event_handler, NULL);
    bc_soil_sensor_set_update_interval(&soil_sensor, MEASURE_INTERVAL);

    // Initialize battery
    bc_module_battery_init();
    bc_module_battery_set_event_handler(battery_event_handler, NULL);
    battery_measure_task_id = bc_scheduler_register(battery_measure_task, NULL, 2020);

    // Initialize lora module
    bc_cmwx1zzabz_init(&lora, BC_UART_UART1);
    bc_cmwx1zzabz_set_event_handler(&lora, lora_callback, NULL);
    bc_cmwx1zzabz_set_mode(&lora, BC_CMWX1ZZABZ_CONFIG_MODE_ABP);
    bc_cmwx1zzabz_set_class(&lora, BC_CMWX1ZZABZ_CONFIG_CLASS_A);

    // Initialize AT command interface
    at_init(&led, &lora);
    static const bc_atci_command_t commands[] = {
            AT_LORA_COMMANDS,
            {"$SEND", at_send, NULL, NULL, NULL, "Immediately send packet"},
            {"$STATUS", at_status, NULL, NULL, NULL, "Show status"},
            AT_LED_COMMANDS,
            BC_ATCI_COMMAND_CLAC,
            BC_ATCI_COMMAND_HELP
    };
    bc_atci_init(commands, BC_ATCI_COMMANDS_LENGTH(commands));

    // Plan application_task to be run after 10 seconds
    bc_scheduler_plan_current_relative(10 * 1000);
}

void application_task(void)
{
    if (!bc_cmwx1zzabz_is_ready(&lora))
    {
        bc_scheduler_plan_current_relative(100);

        return;
    }
               
    // Byte (header) + Byte (Voltage) + Byte (Door State) + Short (Core Module Temperature) + Sensors * (2 bytes (temp) + 2 bytes (cap))
    static uint8_t buffer[1 + 1 + 1 + 2 + 4 * MAX_SOIL_SENSORS]; 

    memset(buffer, 0xff, sizeof(buffer));

    buffer[0] = header;

    float voltage_avg = NAN;
    bc_data_stream_get_average(&sm_voltage, &voltage_avg);

    if (!isnan(voltage_avg))
    {
        buffer[1] = ceil(voltage_avg * 10.f);
    }

    // door open/closed?
    buffer[2] = is_closed;

    // temperature on Core module
    float temperature_avg = NAN;
    bc_data_stream_get_average(&sm_temperature, &temperature_avg);
    if (!isnan(temperature_avg))
    {
        int16_t temperature_i16 = (int16_t) (temperature_avg * 10.f);

        buffer[3] = temperature_i16 >> 8;
        buffer[4] = temperature_i16;
    }

    size_t pos = 5;

    // Soil sensors
    int sensors_count = bc_soil_sensor_get_sensor_found(&soil_sensor);
    for (int i = 0; i < sensors_count; i++) {
        // temperature
        float soil_temperature_avg = NAN;
        bc_data_stream_get_average(sm_soil_temperature[i], &soil_temperature_avg);

        if (!isnan(soil_temperature_avg))
        {
            int16_t soil_temperature_i16 = (int16_t) (soil_temperature_avg * 10.f);

            buffer[pos++] = soil_temperature_i16 >> 8;
            buffer[pos++] = soil_temperature_i16;
        } 
        else
        {
            pos += 2;
        }
        
        // soil 
        int soil_humidity_raw_avg;
        if(bc_data_stream_get_average(sm_soil_humidity_raw[i], &soil_humidity_raw_avg))
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

    bc_cmwx1zzabz_send_message(&lora, buffer, sizeof(buffer));

    static char tmp[sizeof(buffer) * 2 + 1];
    for (size_t i = 0; i < sizeof(buffer); i++)
    {
        sprintf(tmp + i * 2, "%02x", buffer[i]);
    }

    bc_atci_printf("$SEND: %s", tmp);

    header = HEADER_UPDATE;

    bc_scheduler_plan_current_relative(SEND_DATA_INTERVAL);
}
