#include <Arduino.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>
#include <TinyGPSPlus.h>
#include <math.h>

constexpr uint8_t TEMPERATURE_PIN = 4;
constexpr uint8_t I2C_SDA_PIN = 21;
constexpr uint8_t I2C_SCL_PIN = 22;
constexpr uint8_t GPS_RX_PIN = 26;  // ESP32 RX2 <- GPS TX
constexpr uint8_t GPS_TX_PIN = 27;  // ESP32 TX2 -> GPS RX (optional)
constexpr uint32_t GPS_BAUD = 9600;

OneWire oneWire(TEMPERATURE_PIN);
DallasTemperature temperatureSensor(&oneWire);
Adafruit_ADXL345_Unified accelerometer(34501);
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

bool accelerometerReady = false;
bool temperatureReady = false;
float surfaceTemperatureC = NAN;
float accelerationMagnitudeMs2 = NAN;
uint32_t lastTempRead = 0;
uint32_t lastAccelRead = 0;
uint32_t lastPrint = 0;

void printFloatOrNull(float value, uint8_t precision) {
  if (isfinite(value)) Serial.print(value, precision);
  else Serial.print("null");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  temperatureSensor.begin();
  temperatureReady = (temperatureSensor.getDeviceCount() > 0);
  temperatureSensor.setResolution(11);
  temperatureSensor.setWaitForConversion(false);
  if (temperatureReady) temperatureSensor.requestTemperatures();

  accelerometerReady = accelerometer.begin(0x53);
  if (!accelerometerReady) accelerometerReady = accelerometer.begin(0x1D);
  if (accelerometerReady) accelerometer.setRange(ADXL345_RANGE_16_G);

  gpsSerial.setRxBufferSize(1024);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  Serial.println("Khuta Smart Horse Monitor");
  Serial.println("ESP32: DS18B20 GPIO4, ADXL345 SDA21/SCL22, GPS RX26/TX27");
  if (!temperatureReady) Serial.println("DS18B20 NOT FOUND: check wiring");
  if (!accelerometerReady) Serial.println("ADXL345 NOT FOUND: check I2C and soldered headers");
}

void loop() {
  while (gpsSerial.available() > 0) gps.encode(gpsSerial.read());
  const uint32_t now = millis();

  if (temperatureReady && now - lastTempRead >= 800) {
    lastTempRead = now;
    float t = temperatureSensor.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C && t > -55.0f && t < 125.0f) surfaceTemperatureC = t;
    else surfaceTemperatureC = NAN;
    temperatureSensor.requestTemperatures();
  }

  if (accelerometerReady && now - lastAccelRead >= 500) {
    lastAccelRead = now;
    sensors_event_t event;
    accelerometer.getEvent(&event);
    const float x = event.acceleration.x;
    const float y = event.acceleration.y;
    const float z = event.acceleration.z;
    accelerationMagnitudeMs2 = sqrtf(x * x + y * y + z * z);
  }

  if (now - lastPrint >= 2000) {
    lastPrint = now;
    Serial.print("{\"temperature_c\":");
    printFloatOrNull(surfaceTemperatureC, 2);
    Serial.print(",\"acceleration_ms2\":");
    printFloatOrNull(accelerationMagnitudeMs2, 2);
    Serial.print(",\"speed_kmh\":");
    if (gps.speed.isValid() && gps.speed.age() < 10000) Serial.print(gps.speed.kmph(), 2);
    else Serial.print("null");
    Serial.print(",\"latitude\":");
    if (gps.location.isValid() && gps.location.age() < 10000) Serial.print(gps.location.lat(), 6);
    else Serial.print("null");
    Serial.print(",\"longitude\":");
    if (gps.location.isValid() && gps.location.age() < 10000) Serial.print(gps.location.lng(), 6);
    else Serial.print("null");
    Serial.print(",\"satellites\":");
    if (gps.satellites.isValid() && gps.satellites.age() < 10000) Serial.print(gps.satellites.value());
    else Serial.print("null");
    Serial.println("}");
  }
}
