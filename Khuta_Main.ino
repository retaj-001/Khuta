#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>
#include <TinyGPSPlus.h>

#define ENABLE_USER_AUTH
#define ENABLE_DATABASE
#include <FirebaseClient.h>

#include "secrets.h"

constexpr uint8_t TEMPERATURE_PIN = 4;
constexpr uint8_t I2C_SDA_PIN = 21;
constexpr uint8_t I2C_SCL_PIN = 22;
constexpr uint8_t GPS_RX_PIN = 26;  // ESP32 RX2 <- NEO-6M TX
constexpr uint8_t GPS_TX_PIN = 27;  // ESP32 TX2 -> NEO-6M RX
constexpr uint32_t GPS_BAUD = 9600;

constexpr uint32_t TEMPERATURE_INTERVAL_MS = 800;
constexpr uint32_t ACCELERATION_INTERVAL_MS = 200;
constexpr uint32_t UPLOAD_INTERVAL_MS = 2000;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
constexpr uint32_t GPS_FRESHNESS_MS = 10000;
constexpr char DEVICE_PATH[] = "/devices/khuta-esp32/live";

OneWire oneWire(TEMPERATURE_PIN);
DallasTemperature temperatureSensor(&oneWire);
Adafruit_ADXL345_Unified accelerometer(34501);
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

WiFiClientSecure secureClient;
AsyncClientClass firebaseClient(secureClient);
UserAuth deviceAuth(FIREBASE_API_KEY, FIREBASE_USER_EMAIL, FIREBASE_USER_PASSWORD, 3000);
FirebaseApp firebaseApp;
RealtimeDatabase database;

bool temperatureReady = false;
bool accelerometerReady = false;
float surfaceTemperatureC = NAN;
float accelerationMagnitudeMs2 = NAN;
uint32_t lastTemperatureRead = 0;
uint32_t lastAccelerationRead = 0;
uint32_t lastUpload = 0;
uint32_t lastWiFiAttempt = 0;
uint32_t lastGpsWarning = 0;

void printFirebaseResult(AsyncResult &result) {
  if (!result.isResult()) return;

  if (result.isError()) {
    Firebase.printf("FIREBASE ERROR [%s]: %s (code %d)\n",
                    result.uid().c_str(), result.error().message().c_str(),
                    result.error().code());
  } else if (result.isEvent()) {
    Firebase.printf("FIREBASE EVENT [%s]: %s\n",
                    result.uid().c_str(), result.eventLog().message().c_str());
  } else if (result.available()) {
    Firebase.printf("FIREBASE OK [%s]\n", result.uid().c_str());
  }
}

void beginWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWiFiAttempt = millis();
  Serial.printf("WIFI: connecting to %s\n", WIFI_SSID);
}

void maintainWiFi(uint32_t now) {
  if (WiFi.status() == WL_CONNECTED) return;
  if (now - lastWiFiAttempt < WIFI_RETRY_INTERVAL_MS) return;

  lastWiFiAttempt = now;
  Serial.println("WIFI: reconnecting...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void appendNumberOrNull(String &json, float value, uint8_t decimals) {
  if (isfinite(value)) json += String(value, static_cast<unsigned int>(decimals));
  else json += "null";
}

String buildReadingJson() {
  const bool locationValid = gps.location.isValid() && gps.location.age() < GPS_FRESHNESS_MS;
  const bool speedValid = gps.speed.isValid() && gps.speed.age() < GPS_FRESHNESS_MS;
  const bool satellitesValid = gps.satellites.isValid() && gps.satellites.age() < GPS_FRESHNESS_MS;

  String json;
  json.reserve(300);
  json += "{\"device_id\":\"khuta-esp32\",\"temperature_c\":";
  appendNumberOrNull(json, surfaceTemperatureC, 2);
  json += ",\"acceleration_ms2\":";
  appendNumberOrNull(json, accelerationMagnitudeMs2, 3);
  json += ",\"speed_kmh\":";
  if (speedValid) json += String(gps.speed.kmph(), 2);
  else json += "null";
  json += ",\"latitude\":";
  if (locationValid) json += String(gps.location.lat(), 6);
  else json += "null";
  json += ",\"longitude\":";
  if (locationValid) json += String(gps.location.lng(), 6);
  else json += "null";
  json += ",\"satellites\":";
  if (satellitesValid) json += String(gps.satellites.value());
  else json += "null";
  json += ",\"timestamp\":{\".sv\":\"timestamp\"}}";
  return json;
}

void uploadReading(uint32_t now) {
  if (now - lastUpload < UPLOAD_INTERVAL_MS) return;
  lastUpload = now;

  const String json = buildReadingJson();
  Serial.println(json);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("FIREBASE: skipped (Wi-Fi offline)");
    return;
  }
  if (!firebaseApp.ready()) {
    Serial.println("FIREBASE: authentication not ready");
    return;
  }

  database.set<object_t>(firebaseClient, DEVICE_PATH, object_t(json),
                         printFirebaseResult, "khutaUpload");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nKhuta Smart Horse Monitor");
  Serial.println("DS18B20 GPIO4 | ADXL345 SDA21/SCL22 | GPS TX->GPIO26 RX->GPIO27");

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  temperatureSensor.begin();
  temperatureReady = temperatureSensor.getDeviceCount() > 0;
  temperatureSensor.setResolution(11);
  temperatureSensor.setWaitForConversion(false);
  if (temperatureReady) temperatureSensor.requestTemperatures();
  else Serial.println("DS18B20: NOT FOUND");

  accelerometerReady = accelerometer.begin(0x53);
  if (!accelerometerReady) accelerometerReady = accelerometer.begin(0x1D);
  if (accelerometerReady) accelerometer.setRange(ADXL345_RANGE_16_G);
  else Serial.println("ADXL345: NOT FOUND");

  gpsSerial.setRxBufferSize(1024);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  beginWiFi();

  secureClient.setInsecure();
  initializeApp(firebaseClient, firebaseApp, getAuth(deviceAuth),
                printFirebaseResult, "firebaseAuth");
  firebaseApp.getApp<RealtimeDatabase>(database);
  database.url(FIREBASE_DATABASE_URL);
}

void loop() {
  while (gpsSerial.available() > 0) gps.encode(gpsSerial.read());

  const uint32_t now = millis();
  maintainWiFi(now);
  firebaseApp.loop();

  if (temperatureReady && now - lastTemperatureRead >= TEMPERATURE_INTERVAL_MS) {
    lastTemperatureRead = now;
    const float value = temperatureSensor.getTempCByIndex(0);
    surfaceTemperatureC =
        value != DEVICE_DISCONNECTED_C && value > -55.0f && value < 125.0f
            ? value
            : NAN;
    temperatureSensor.requestTemperatures();
  }

  if (accelerometerReady && now - lastAccelerationRead >= ACCELERATION_INTERVAL_MS) {
    lastAccelerationRead = now;
    sensors_event_t event;
    accelerometer.getEvent(&event);
    const float x = event.acceleration.x;
    const float y = event.acceleration.y;
    const float z = event.acceleration.z;
    accelerationMagnitudeMs2 = sqrtf(x * x + y * y + z * z);
  }

  if (now > 10000 && gps.charsProcessed() < 10 &&
      now - lastGpsWarning >= 10000) {
    lastGpsWarning = now;
    Serial.println("GPS SERIAL: NO DATA (check GPS TX->GPIO26, common GND, and 9600 baud)");
  }

  uploadReading(now);
}
