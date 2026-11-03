#include <TinyGPSPlus.h>
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
void setup() { Serial.begin(115200); gpsSerial.begin(9600, SERIAL_8N1, 26, 27); Serial.println("NEO-6M: waiting for NMEA on GPIO26"); }
void loop() {
  while (gpsSerial.available()) { const char value = gpsSerial.read(); gps.encode(value); Serial.write(value); }
  static uint32_t last = 0;
  if (millis() - last >= 2000) {
    last = millis();
    const String satellites = gps.satellites.isValid() ? String(gps.satellites.value()) : "unavailable";
    Serial.printf("\nchars=%lu fix=%s satellites=%s\n", static_cast<unsigned long>(gps.charsProcessed()), gps.location.isValid() ? "yes" : "no", satellites.c_str());
  }
}
