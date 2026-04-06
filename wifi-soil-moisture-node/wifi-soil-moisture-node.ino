/*
 * ESP32 Wi-Fi Soil Moisture Node
 *
 * Sensor:
 *   - Capacitive soil moisture sensor on GPIO 34 (analog)
 *   - Power control on GPIO 4 (to reduce corrosion)
 *
 * Auth:
 *   - sensor_id is auto-generated from ESP32 MAC address
 *   - x-api-key header sent with every request for multi-tenancy
 *
 * Endpoint:
 *   POST /api/soil-moisture  { "sensor_id": "ESP32_XXXXXXXXXXXX", "value": xx.x }
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_mac.h"


// ═══════════════ Wi-Fi Configuration ═══════════════
const char* ssid     = "mashnetwork";
const char* password = "mash2026";


// ═══════════════ Backend Configuration ═══════════════
const char* backendBase = "http://wifi-nodes-backend-rfq.app.cern.ch/api";
const char* apiKey      = "mash_908b7eb9a7c8e2d7200dcb4f7e3e9f96d2bc1a4a59538aab"; // From /api/auth/register

// ═══════════════ Sensor ID (auto from MAC) ═══════════════
String sensorId;

// ═══════════════ Soil Sensor ═══════════════
#define SOIL_SENSOR_PIN 34
#define POWER_PIN       4
int dryValue = 4095;
int wetValue = 2000;

// ═══════════════ LED ═══════════════
#define STATUS_LED_PIN 2

// ═══════════════ Timing & Sleep ═══════════════
const unsigned long DEFAULT_INTERVAL_SEC = 300; // 10 min fallback
uint32_t reportIntervalSec = DEFAULT_INTERVAL_SEC;

// ───────────── Generate sensor ID from MAC ─────────────

String getMacSensorId() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA); // Read directly from hardware eFuse
  char id[20];
  sprintf(id, "ESP32_%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(id);
}



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
    
    // Simple manual parsing to avoid large ArduinoJson dependency if possible, 
    // but these sensors usually have it. Let's use basic string search for speed.
    auto getInt = [&](String key, int& val) {
      int idx = payload.indexOf("\"" + key + "\":");
      if (idx != -1) {
        String sub = payload.substring(idx + key.length() + 3);
        int endIdx = sub.indexOf(",");
        if (endIdx == -1) endIdx = sub.indexOf("}");
        if (endIdx != -1) val = sub.substring(0, endIdx).toInt();
      }
    };

    getInt("reportInterval", (int&)reportIntervalSec);
    getInt("soilDryRawValue", dryValue);
    getInt("soilWetRawValue", wetValue);

    if (reportIntervalSec < 10) reportIntervalSec = 10;
    Serial.printf("[Config] Interval: %d, Dry: %d, Wet: %d\n", reportIntervalSec, dryValue, wetValue);
  } else {
    Serial.printf("[Config] Failed to fetch (Code: %d)\n", code);
  }
  http.end();
}

// ───────────── Sensor Reading ─────────────

float readSoilMoisture() {
  digitalWrite(POWER_PIN, HIGH);
  delay(100);

  int total = 0;
  const int numReadings = 5;
  for (int i = 0; i < numReadings; i++) {
    total += analogRead(SOIL_SENSOR_PIN);
    delay(10);
  }
  digitalWrite(POWER_PIN, LOW);

  int avg = total / numReadings;
  // Send raw ADC value — percentage mapping done on frontend
  Serial.printf("  Raw ADC: %d (sending raw)\n", avg);
  return (float)avg;
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
  Serial.println("\n=== ESP32 Deep Sleep Node (Soil Moisture) ===");

  pinMode(SOIL_SENSOR_PIN, INPUT);
  pinMode(POWER_PIN, OUTPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(POWER_PIN, LOW);
  digitalWrite(STATUS_LED_PIN, LOW);
  WiFi.mode(WIFI_STA); // Ensure radio is ready for MAC reading

  // Generate MAC-based sensor ID

  sensorId = getMacSensorId();
  Serial.print("Sensor ID: ");
  Serial.println(sensorId);

  // Connect Wi-Fi with aggressive retry
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) { // Increased attempts
    delay(500);
    Serial.print(".");
    attempts++;
    
    // Force reset every 10 attempts if stuck
    if (attempts % 10 == 0) {
      Serial.print(" (Retrying...) ");
      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
    digitalWrite(STATUS_LED_PIN, HIGH);

    // 1. Fetch Config first
    fetchConfiguration();

    // 2. Read and Send Data
    float moisture = readSoilMoisture();
    sendReading("/soil-moisture", moisture);

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

void loop() {
  // Never reached in Deep Sleep mode
}

