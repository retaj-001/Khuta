#include <OneWire.h>
#include <DallasTemperature.h>

constexpr uint8_t DATA_PIN = 4;
OneWire wire(DATA_PIN);
DallasTemperature sensor(&wire);

void setup() {
  Serial.begin(115200);
  sensor.begin();
  Serial.print("DS18B20 devices: ");
  Serial.println(sensor.getDeviceCount());
}

void loop() {
  sensor.requestTemperatures();
  const float t = sensor.getTempCByIndex(0);
  if (t == DEVICE_DISCONNECTED_C) Serial.println("DS18B20 NOT FOUND");
  else {
    Serial.print("Surface temperature: ");
    Serial.print(t, 2);
    Serial.println(" C");
  }
  delay(1000);
}
