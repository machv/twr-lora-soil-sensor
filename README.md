# Firmware for HARDWARIO Lora Soil Sensor

## Description

Soil sensor monitoring using Lora. In addition to measuring soil and temperature, it also reports status of magnetic reed sensor, which can be attached to P9 (e. g. to monitor if door to the greenhouse is open).

Values are sent every 15 minutes over LoRaWAN. Values are the arithmetic mean of the measured values since the last send.

Measure interval is 5m, the battery is measured during transmission.

## Buffer
big endian

| Byte    | Name        | Type   | Multiple | Unit   | Note
| ------: | ----------- | ------ | -------- | ------ | ---------
|       0 | HEADER      | uint8  |          |        |
|       1 | BATTERY     | uint8  | 10       | V      |
|       2 | DOOR STATUS | bool   |          |        | State of the reed sensor (true = closed, false = open)
|  3 -  4 | TEMPERATURE | int16  | 10       | °C     | Temperature on the Core module
|  5 -  6 | TEMPERATURE | int16  | 10       | °C     | Temperature in the Soil Sensor module
|  7 -  8 | SOIL RAW    | uint16 |          |        | Raw measurement of the Soil Sensor module

### Header

* 0 - boot
* 1 - update
* 2 - button click
* 3 - button hold
* 4 - door opened
* 5 - door closed

## AT

```sh
picocom -b 115200 --omap crcrlf  --echo /dev/ttyUSB0
```
