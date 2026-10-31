#include <TinyGPSPlus.h>

TinyGPSPlus gps;
HardwareSerial gpsPort(2);
uint32_t lastPrint = 0;

void setup() {
  Serial.begin(115200);
  gpsPort.setRxBufferSize(1024);
  gpsPort.begin(9600, SERIAL_8N1, 26, 27); // GPS TX -> GPIO26, GPS RX -> GPIO27 (optional)
  Serial.println("NEO-6M GPS test. Test outdoors with open view of the sky.");
}

void loop() {
  while (gpsPort.available()) gps.encode(gpsPort.read());
  if (millis() - lastPrint < 2000) return;
  lastPrint = millis();

  if (gps.location.isValid() && gps.location.age() < 10000) {
    Serial.print("Latitude: "); Serial.println(gps.location.lat(), 6);
    Serial.print("Longitude: "); Serial.println(gps.location.lng(), 6);
  } else Serial.println("Waiting for GPS fix...");

  if (gps.speed.isValid() && gps.speed.age() < 10000) {
    Serial.print("Speed: "); Serial.print(gps.speed.kmph(), 2); Serial.println(" km/h");
  } else Serial.println("Speed: unavailable");

  Serial.print("Satellites: ");
  if (gps.satellites.isValid() && gps.satellites.age() < 10000) Serial.println(gps.satellites.value());
  else Serial.println("unavailable");

  if (millis() > 10000 && gps.charsProcessed() < 10)
    Serial.println("No GPS serial data: check TX of GPS to GPIO26 and common GND.");
}
