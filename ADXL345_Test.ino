#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>
#include <math.h>

Adafruit_ADXL345_Unified accel(34501);

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  if (!accel.begin(0x53) && !accel.begin(0x1D)) {
    Serial.println("ADXL345 NOT FOUND. Check SDA=21, SCL=22, 3.3V and soldered headers.");
    while (true) delay(1000);
  }
  accel.setRange(ADXL345_RANGE_16_G);
}

void loop() {
  sensors_event_t e;
  accel.getEvent(&e);
  const float magnitude = sqrtf(e.acceleration.x * e.acceleration.x +
                                 e.acceleration.y * e.acceleration.y +
                                 e.acceleration.z * e.acceleration.z);
  Serial.print("X: "); Serial.print(e.acceleration.x, 2);
  Serial.print(" Y: "); Serial.print(e.acceleration.y, 2);
  Serial.print(" Z: "); Serial.print(e.acceleration.z, 2);
  Serial.print(" | Magnitude: "); Serial.print(magnitude, 2);
  Serial.println(" m/s^2 (includes gravity)");
  delay(500);
}
