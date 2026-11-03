#include <OneWire.h>
#include <DallasTemperature.h>
OneWire oneWire(4);
DallasTemperature sensor(&oneWire);
void setup() { Serial.begin(115200); sensor.begin(); }
void loop() {
  sensor.requestTemperatures();
  const float value = sensor.getTempCByIndex(0);
  if (value == DEVICE_DISCONNECTED_C) Serial.println("DS18B20: NOT FOUND");
  else Serial.printf("Temperature: %.2f C\n", value);
  delay(1000);
}
