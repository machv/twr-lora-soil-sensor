function decodeUplink(input) {
  var bytes = input.bytes;
  var header = bytes[0];

  var headerValue = "?";
  switch (header) {
      case 0:
          headerValue = "BOOT";
          break;
      case 1:
          headerValue = "UPDATE";
          break;
      case 2:
          headerValue = "BUTTON_CLICK";
          break;
      case 3:
          headerValue = "BUTTON_HOLD";
          break;
      case 4:
          headerValue = "DOOR_OPEN";
          break;
      case 5:
          headerValue = "DOOR_CLOSE";
          break;
  }

  var voltage = null;
  if (bytes[1] != 255)
    voltage = bytes[1] / 10.0;

  var doorState = bytes[2] == 1 ? "closed" : "open";
  var temperature = ((bytes[3] << 8) | bytes[4]) / 10.0;

  var decoded = {
      header: header,
      headerInfo: headerValue,
      voltage: voltage,
      door: doorState,
      temperature: temperature,
      soilSensors: []
  };

  // soil sensors
  var pos = 5;
  var sensorsCount = (bytes.length - 5) / 4;
  for (i = 0; i < sensorsCount; i++) {
      var soilTemperature = ((bytes[pos++] << 8) | bytes[pos++]);
      var soilHumidity = ((bytes[pos++] << 8) | bytes[pos++]);
        if (soilHumidity == 65535 || soilTemperature == 65535)
          continue;

        decoded.soilSensors.push({
          temperature: soilTemperature / 10,
          humidity: soilHumidity
        });
  }

  return decoded;
}
