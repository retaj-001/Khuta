#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>
#include <TinyGPSPlus.h>

#include "secrets.h"

constexpr uint8_t TEMPERATURE_PIN = 4;
constexpr uint8_t I2C_SDA_PIN = 21;
constexpr uint8_t I2C_SCL_PIN = 22;
constexpr uint8_t GPS_RX_PIN = 26;
constexpr uint8_t GPS_TX_PIN = 27;
constexpr uint32_t GPS_BAUD = 9600;
constexpr uint32_t TEMPERATURE_INTERVAL_MS = 800;
constexpr uint32_t ACCELERATION_INTERVAL_MS = 200;
constexpr uint32_t PUBLISH_INTERVAL_MS = 2000;
constexpr uint32_t GPS_FRESHNESS_MS = 10000;
constexpr uint32_t HTTP_TIMEOUT_MS = 7000;
constexpr uint32_t RETRY_MIN_MS = 2000;
constexpr uint32_t RETRY_MAX_MS = 30000;

struct SensorReading {
  char readingId[49];
  float temperatureC;
  float accelerationMs2;
  float speedKmh;
  double latitude;
  double longitude;
  uint32_t satellites;
  bool hasTemperature;
  bool hasAcceleration;
  bool hasSpeed;
  bool hasLocation;
  bool hasSatellites;
};

OneWire oneWire(TEMPERATURE_PIN);
DallasTemperature temperatureSensor(&oneWire);
Adafruit_ADXL345_Unified accelerometer(34501);
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
QueueHandle_t uploadQueue = nullptr;
bool temperatureReady = false;
bool accelerometerReady = false;
float temperatureC = NAN;
float accelerationMs2 = NAN;
uint32_t lastTemperatureRead = 0;
uint32_t lastAccelerationRead = 0;
uint32_t lastPublish = 0;
uint32_t lastGpsWarning = 0;
uint32_t bootNonce = 0;
uint32_t readingSequence = 0;

void appendFloatOrNull(String &json, bool valid, double value, uint8_t digits) {
  if (valid && isfinite(value)) json += String(value, static_cast<unsigned int>(digits));
  else json += F("null");
}

String readingJson(const SensorReading &reading) {
  String json;
  json.reserve(360);
  json += F("{\"reading_id\":\"");
  json += reading.readingId;
  json += F("\",\"temperature_c\":");
  appendFloatOrNull(json, reading.hasTemperature, reading.temperatureC, 2);
  json += F(",\"acceleration_ms2\":");
  appendFloatOrNull(json, reading.hasAcceleration, reading.accelerationMs2, 3);
  json += F(",\"speed_kmh\":");
  appendFloatOrNull(json, reading.hasSpeed, reading.speedKmh, 2);
  json += F(",\"latitude\":");
  appendFloatOrNull(json, reading.hasLocation, reading.latitude, 6);
  json += F(",\"longitude\":");
  appendFloatOrNull(json, reading.hasLocation, reading.longitude, 6);
  json += F(",\"satellites\":");
  if (reading.hasSatellites) json += String(reading.satellites);
  else json += F("null");
  json += '}';
  return json;
}

bool validSupabaseConfiguration() {
  return strlen(SUPABASE_INGEST_URL) > 20 && strncmp(SUPABASE_INGEST_URL, "https://", 8) == 0 &&
         strlen(DEVICE_ID) >= 3 && strlen(DEVICE_API_KEY) >= 32 && strlen(SUPABASE_ROOT_CA) > 100;
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("WIFI: connecting to %s\n", WIFI_SSID);
}

bool waitForWiFi(uint32_t timeoutMs) {
  connectWiFi();
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < timeoutMs) vTaskDelay(pdMS_TO_TICKS(250));
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WIFI: connected, IP %s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.println("WIFI: connection timeout");
  return false;
}

bool postReading(const SensorReading &reading) {
  if (!waitForWiFi(10000)) return false;
  WiFiClientSecure tls;
  tls.setCACert(SUPABASE_ROOT_CA);
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);
  if (!http.begin(tls, SUPABASE_INGEST_URL)) {
    Serial.println("API: TLS setup failed");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
  http.addHeader("apikey", SUPABASE_ANON_KEY);
  http.addHeader("X-Device-Id", DEVICE_ID);
  http.addHeader("X-Device-Key", DEVICE_API_KEY);
  const int status = http.POST(readingJson(reading));
  const String response = http.getString();
  http.end();
  if (status >= 200 && status < 300) {
    Serial.printf("API: stored %s (HTTP %d)\n", reading.readingId, status);
    return true;
  }
  Serial.printf("API ERROR: HTTP %d, %s\n", status, response.c_str());
  return false;
}

void uploadTask(void *) {
  if (!validSupabaseConfiguration()) Serial.println("API: complete the Supabase values in secrets.h");
  SensorReading pending{};
  bool hasPending = false;
  uint32_t retryMs = RETRY_MIN_MS;
  for (;;) {
    if (!hasPending && xQueueReceive(uploadQueue, &pending, portMAX_DELAY) == pdTRUE) {
      hasPending = true;
      retryMs = RETRY_MIN_MS;
    }
    if (!hasPending) continue;
    if (!validSupabaseConfiguration()) {
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }
    if (postReading(pending)) {
      hasPending = false;
      retryMs = RETRY_MIN_MS;
    } else {
      vTaskDelay(pdMS_TO_TICKS(retryMs));
      retryMs = min(retryMs * 2, RETRY_MAX_MS);
    }
  }
}

SensorReading snapshotReading() {
  SensorReading reading{};
  snprintf(reading.readingId, sizeof(reading.readingId), "%08lx-%08lx-%08lx",
           static_cast<unsigned long>(bootNonce), static_cast<unsigned long>(readingSequence++),
           static_cast<unsigned long>(millis()));
  reading.hasTemperature = isfinite(temperatureC);
  reading.temperatureC = temperatureC;
  reading.hasAcceleration = isfinite(accelerationMs2);
  reading.accelerationMs2 = accelerationMs2;
  reading.hasLocation = gps.location.isValid() && gps.location.age() < GPS_FRESHNESS_MS;
  reading.latitude = reading.hasLocation ? gps.location.lat() : 0.0;
  reading.longitude = reading.hasLocation ? gps.location.lng() : 0.0;
  reading.hasSpeed = reading.hasLocation && gps.speed.isValid() && gps.speed.age() < GPS_FRESHNESS_MS;
  reading.speedKmh = reading.hasSpeed ? gps.speed.kmph() : 0.0f;
  reading.hasSatellites = gps.satellites.isValid() && gps.satellites.age() < GPS_FRESHNESS_MS;
  reading.satellites = reading.hasSatellites ? gps.satellites.value() : 0;
  return reading;
}

void queueLatestReading() {
  SensorReading reading = snapshotReading();
  Serial.println(readingJson(reading));
  if (xQueueSend(uploadQueue, &reading, 0) != pdTRUE) {
    SensorReading discarded{};
    xQueueReceive(uploadQueue, &discarded, 0);
    xQueueSend(uploadQueue, &reading, 0);
    Serial.println("API QUEUE: replaced an older unsent sample");
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nKhuta Smart Horse Monitor / Supabase");
  Serial.println("DS18B20 GPIO4 | ADXL345 SDA21/SCL22 | GPS TX->GPIO26 RX->GPIO27");
  bootNonce = esp_random();
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
  uploadQueue = xQueueCreate(1, sizeof(SensorReading));
  xTaskCreatePinnedToCore(uploadTask, "khuta-api", 12288, nullptr, 1, nullptr, 0);
}

void loop() {
  while (gpsSerial.available() > 0) gps.encode(gpsSerial.read());
  const uint32_t now = millis();
  if (temperatureReady && now - lastTemperatureRead >= TEMPERATURE_INTERVAL_MS) {
    lastTemperatureRead = now;
    const float value = temperatureSensor.getTempCByIndex(0);
    temperatureC = value != DEVICE_DISCONNECTED_C && value > -55.0f && value < 125.0f ? value : NAN;
    temperatureSensor.requestTemperatures();
  }
  if (accelerometerReady && now - lastAccelerationRead >= ACCELERATION_INTERVAL_MS) {
    lastAccelerationRead = now;
    sensors_event_t event;
    accelerometer.getEvent(&event);
    accelerationMs2 = sqrtf(event.acceleration.x * event.acceleration.x + event.acceleration.y * event.acceleration.y + event.acceleration.z * event.acceleration.z);
  }
  if (now > 10000 && gps.charsProcessed() < 10 && now - lastGpsWarning >= 10000) {
    lastGpsWarning = now;
    Serial.println("GPS SERIAL: NO DATA (check GPS TX->GPIO26, common GND, and 9600 baud)");
  }
  if (now - lastPublish >= PUBLISH_INTERVAL_MS) {
    lastPublish = now;
    queueLatestReading();
  }
  delay(2);
}
