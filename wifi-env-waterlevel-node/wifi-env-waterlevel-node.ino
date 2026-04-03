/*
 * ESP32 Wi-Fi Environment + Water Level Node
 *
 * Sensors:
 *   - DHT11/DHT22 on GPIO 4   → Temperature & Humidity
 *   - HC-SR04 on GPIO 18/19   → Water Level (distance in cm)
 *
 * Auth:
 *   - sensor_id is auto-generated from ESP32 MAC address
 *   - x-api-key header sent with every request for multi-tenancy
 *
 * Endpoints:
 *   POST /api/temperature   { "sensor_id": "ESP32_XXXXXXXXXXXX", "value": xx.x }
 *   POST /api/humidity      { "sensor_id": "ESP32_XXXXXXXXXXXX", "value": xx.x }
 *   POST /api/water-level   { "sensor_id": "ESP32_XXXXXXXXXXXX", "value": xx.x }
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include "esp_mac.h"


// ═══════════════ Wi-Fi Configuration ═══════════════
const char* ssid     = "mashnetwork";
const char* password = "mash2026";

// ═══════════════ Backend Configuration ═══════════════
const char* backendBase = "http://wifi-nodes-backend-rfq.app.cern.ch/api";
const char* apiKey      = "mash_908b7eb9a7c8e2d7200dcb4f7e3e9f96d2bc1a4a59538aab"; // From /api/auth/register

// ═══════════════ Sensor ID (auto from MAC) ═══════════════
String sensorId;

// ═══════════════ DHT Sensor ═══════════════
#define DHT_PIN   4
#define DHT_TYPE  DHT11
DHT dht(DHT_PIN, DHT_TYPE);

// ═══════════════ HC-SR04 Ultrasonic ═══════════════
#define TRIG_PIN  18
#define ECHO_PIN  19
const int maxDistance = 400;

// ═══════════════ LED ═══════════════
#define STATUS_LED_PIN 2

// ═══════════════ Timing & Sleep ═══════════════
const unsigned long DEFAULT_INTERVAL_SEC = 600; // 10 min fallback
uint32_t reportIntervalSec = DEFAULT_INTERVAL_SEC;

// ───────────── Fetch Configuration (Interval) ─────────────

void fetchConfiguration() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String(backendBase) + "/sensors/" + sensorId + "/config";
  http.begin(url);
  http.addHeader("x-api-key", apiKey);

  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    // Simple manual parsing to avoid heavy JSON library if possible, 
    // but since we need it, we'll use a basic search.
    int idx = payload.indexOf("\"reportInterval\":");
    if (idx != -1) {
      String sub = payload.substring(idx + 17);
      int endIdx = sub.indexOf(",");
      if (endIdx == -1) endIdx = sub.indexOf("}");
      if (endIdx != -1) {
        reportIntervalSec = sub.substring(0, endIdx).toInt();
        if (reportIntervalSec < 10) reportIntervalSec = 10; // Safety floor
        Serial.printf("[Config] New interval: %d sec\n", reportIntervalSec);
      }
    }
  } else {
    Serial.printf("[Config] Failed to fetch (Code: %d)\n", code);
  }
  http.end();
}

// ───────────── HTTP POST with API Key ─────────────

void sendReading(const char* endpoint, float value) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String(backendBase) + endpoint;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", apiKey);

  String payload = "{\"sensor_id\":\"" + sensorId + "\",\"value\":" + String(value, 1) + "}";
  int code = http.POST(payload);

  if (code > 0) {
    Serial.printf("  POST %s → %d\n", endpoint, code);
  } else {
    Serial.printf("  ERROR %s → %s\n", endpoint, http.errorToString(code).c_str());
  }
  http.end();
}

// ───────────── Setup ─────────────

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 Deep Sleep Node (Env + Water) ===");

  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  WiFi.mode(WIFI_STA); // Ensure radio is ready for MAC reading

  // Generate MAC-based sensor ID

  sensorId = getMacSensorId();
  Serial.print("Sensor ID: ");
  Serial.println(sensorId);

  // Connect Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
    digitalWrite(STATUS_LED_PIN, HIGH);

    // 1. Fetch Config first
    fetchConfiguration();

    // 2. Read and Send Data
    dht.begin();
    delay(2000); // Wait for DHT stable
    
    float temp = dht.readTemperature();
    float hum  = dht.readHumidity();
    float dist = readDistance();

    if (!isnan(temp)) sendReading("/temperature", temp);
    if (!isnan(hum))  sendReading("/humidity", hum);
    if (dist >= 0)    sendReading("/water-level", dist);

    Serial.println("Data sent. Entering sleep...");
  } else {
    Serial.println("\nWiFi Failed. Sleeping anyway...");
  }

  // 3. Enter Deep Sleep
  uint64_t sleepTimeUs = (uint64_t)reportIntervalSec * 1000000ULL;
  esp_sleep_enable_timer_wakeup(sleepTimeUs);
  Serial.printf("Sleeping for %d seconds\n", reportIntervalSec);
  Serial.flush();
  esp_deep_sleep_start();
}

String getMacSensorId() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA); // Read directly from hardware eFuse
  char buf[20];
  sprintf(buf, "ESP32_%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}


float readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout
  if (duration == 0) return -1;
  
  float distanceCm = duration * 0.034 / 2;
  if (distanceCm > maxDistance) return -1;
  return distanceCm;
}

void loop() {
  // Never reached in Deep Sleep mode
}
