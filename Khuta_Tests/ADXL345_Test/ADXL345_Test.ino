#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>
Adafruit_ADXL345_Unified sensor(34501);
void setup() {
  Serial.begin(115200); Wire.begin(21, 22);
  if (!sensor.begin(0x53) && !sensor.begin(0x1D)) { Serial.println("ADXL345: NOT FOUND"); while (true) delay(1000); }
  sensor.setRange(ADXL345_RANGE_16_G);
}
void loop() {
  sensors_event_t event; sensor.getEvent(&event);
  const float magnitude = sqrtf(event.acceleration.x * event.acceleration.x + event.acceleration.y * event.acceleration.y + event.acceleration.z * event.acceleration.z);
  Serial.printf("X %.3f Y %.3f Z %.3f | magnitude %.3f m/s^2\n", event.acceleration.x, event.acceleration.y, event.acceleration.z, magnitude);
  delay(250);
}
