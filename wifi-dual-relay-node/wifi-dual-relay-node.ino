/*
 * ESP32 Wi-Fi Dual Relay Node
 *
 * Relays:
 *   - Channel 1: Irrigation Pump   (GPIO 26)
 *   - Channel 2: Water Tank Pump   (GPIO 27)
 *
 * Auth:
 *   - relay_id is auto-generated from ESP32 MAC address
 *   - x-api-key header sent with every request for multi-tenancy
 *
 * Operation:
 *   - Polls GET  /api/relay/pending?relay_id=ESP32_XXXXXXXXXXXX every 5s (Static)
 *   - Reports POST /api/relay/status after each poll
 *   - 30-minute safety auto-off watchdog per channel
 *
 * Requires: ArduinoJson library (install via Library Manager)
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "esp_mac.h"
#include "../ota_updater.h"

// ═══════════════ Firmware Identity ═══════════════
#define FIRMWARE_VERSION "1.0.0"
#define NODE_TYPE "dual_relay"


// ═══════════════ Wi-Fi Configuration ═══════════════
const char* ssid     = "mashnetwork";
const char* password = "mash2026";


// ═══════════════ Backend Configuration ═══════════════
const char* backendBase = "http://wifi-nodes-backend-rfq.app.cern.ch/api";
const char* apiKey      = "mash_908b7eb9a7c8e2d7200dcb4f7e3e9f96d2bc1a4a59538aab"; // From /api/auth/register

// ═══════════════ Relay ID (auto from MAC) ═══════════════
String relayId;

// ═══════════════ Hardware ═══════════════
#define IRRIGATION_RELAY_PIN 26
#define WATER_TANK_RELAY_PIN 27
#define LED_PIN              2

// Most relay modules for ESP32 are active LOW (LOW=ON, HIGH=OFF)
#define RELAY_ON  HIGH
#define RELAY_OFF LOW

// ═══════════════ State ═══════════════
bool irrigationState = false;
bool waterTankState  = false;

// ═══════════════ Timing ═══════════════
uint32_t pollIntervalMs = 5000; // Static 5s for dual relay (Sync with backend config disabled)
const unsigned long AUTO_OFF_TIMEOUT = 30 * 60 * 1000UL; // 30 min
unsigned long lastPollTime = 0;
unsigned long irrigationOnSince = 0;
unsigned long waterTankOnSince  = 0;
int consecutiveFailures = 0;
bool lastPollSuccessful = true;

// ═══════════════ OTA Settings (from backend config) ═══════════════
bool otaEnabled = true;
uint32_t otaCheckIntervalMs = 3600000; // Default 1 hour
unsigned long lastOtaCheckTime = 0;


// ───────────── Generate relay ID from MAC ─────────────

String getMacRelayId() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA); // Read directly from hardware eFuse
  char id[20];
  sprintf(id, "ESP32_%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(id);
}



// ───────────── Relay Control ─────────────

void setIrrigation(bool state) {
  irrigationState = state;
  digitalWrite(IRRIGATION_RELAY_PIN, state ? RELAY_ON : RELAY_OFF);
  delay(100); // Power stabilization
  if (state) irrigationOnSince = millis();
  Serial.printf("🌱 Irrigation: %s (at %lu)\n", state ? "ON" : "OFF", millis());
}

void setWaterTank(bool state) {
  waterTankState = state;
  digitalWrite(WATER_TANK_RELAY_PIN, state ? RELAY_ON : RELAY_OFF);
  delay(100); // Power stabilization
  if (state) waterTankOnSince = millis();
  Serial.printf("💧 Water Tank: %s (at %lu)\n", state ? "ON" : "OFF", millis());
}

// ───────────── Poll Backend ─────────────

bool pollCommands() {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = String(backendBase) + "/relay/pending?relay_id=" + relayId;
  http.begin(url);
  http.addHeader("x-api-key", apiKey);
  http.setTimeout(10000); // 10s timeout to prevent hangs
  int code = http.GET();

  bool success = false;

  if (code == 200) {
    success = true;
    String body = http.getString();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
      Serial.printf("JSON parse error: %s\n", err.c_str());
      http.end();
      return false;
    }

    int count = doc["count"] | 0;
    if (count > 0) {
      JsonArray commands = doc["data"].as<JsonArray>();
      for (JsonObject cmd : commands) {
        const char* channel = cmd["channel"];
        const char* action  = cmd["action"];

        Serial.printf("[CMD] channel=%s action=%s\n", channel, action);

        if (strcmp(channel, "irrigation") == 0) {
          setIrrigation(strcmp(action, "ON") == 0);
        } else if (strcmp(channel, "water_tank") == 0) {
          setWaterTank(strcmp(action, "ON") == 0);
        }
      }
    }
  } else {
    Serial.printf("Poll error: %d\n", code);
  }
  http.end();
  return success;
}

// ───────────── Fetch Configuration (Interval) ─────────────

void fetchConfiguration() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String(backendBase) + "/sensors/" + relayId + "/config";
  http.begin(url);
  http.addHeader("x-api-key", apiKey);
  http.setTimeout(10000); // 10s timeout to prevent hangs

  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    int idx = payload.indexOf("\"reportInterval\":");
    if (idx != -1) {
      String sub = payload.substring(idx + 17);
      int endIdx = sub.indexOf(",");
      if (endIdx == -1) endIdx = sub.indexOf("}");
      if (endIdx != -1) {
        uint32_t intervalSec = sub.substring(0, endIdx).toInt();
        if (intervalSec < 5) intervalSec = 5;
        pollIntervalMs = intervalSec * 1000;
        Serial.printf("[Config] New poll interval: %d ms\n", pollIntervalMs);
      }
    }

    // Parse OTA settings (merged by backend: type policy + per-node override)
    int otaIdx = payload.indexOf("\"otaEnabled\":");
    if (otaIdx != -1) {
      String sub = payload.substring(otaIdx + 13);
      otaEnabled = sub.startsWith("true");
    }
    int otaIntIdx = payload.indexOf("\"otaCheckInterval\":");
    if (otaIntIdx != -1) {
      String sub = payload.substring(otaIntIdx + 19);
      int endIdx = sub.indexOf(",");
      if (endIdx == -1) endIdx = sub.indexOf("}");
      if (endIdx != -1) {
        uint32_t intervalSec = sub.substring(0, endIdx).toInt();
        if (intervalSec < 300) intervalSec = 300; // Floor: 5 minutes
        otaCheckIntervalMs = intervalSec * 1000UL;
        Serial.printf("[Config] OTA check interval: %d sec, enabled: %s\n", intervalSec, otaEnabled ? "YES" : "NO");
      }
    }
  }
  http.end();
}

// ───────────── Status Reporting ─────────────


// ───────────── Status Reporting ─────────────

void reportStatus() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String(backendBase) + "/relay/status";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", apiKey);
  http.setTimeout(10000); // 10s timeout to prevent hangs

  String payload = "{\"relay_id\":\"" + relayId
    + "\",\"irrigation_state\":" + (irrigationState ? "true" : "false")
    + ",\"water_tank_state\":" + (waterTankState ? "true" : "false") + "}";

  int code = http.POST(payload);
  http.end();
}

// ───────────── Setup ─────────────

void setup() {
  Serial.begin(115200);
  Serial.println("=== ESP32 Dual Relay Node (Wi-Fi) ===");

  digitalWrite(IRRIGATION_RELAY_PIN, RELAY_OFF);
  digitalWrite(WATER_TANK_RELAY_PIN, RELAY_OFF);
  pinMode(IRRIGATION_RELAY_PIN, OUTPUT);
  pinMode(WATER_TANK_RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  WiFi.mode(WIFI_STA); // Ensure radio is ready for MAC reading

  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());
  digitalWrite(LED_PIN, HIGH);

  relayId = getMacRelayId();
  Serial.print("Relay ID: ");
  Serial.println(relayId);
  Serial.printf("Firmware: v%s\n", FIRMWARE_VERSION);

  // Fetch config (includes OTA settings)
  fetchConfiguration();
  reportStatus();

  // Check for OTA on boot
  if (otaEnabled) {
    OTAInfo otaInfo = checkForUpdate(NODE_TYPE, FIRMWARE_VERSION);
    if (otaInfo.available) {
      sendOtaAck(backendBase, apiKey, relayId.c_str(), otaInfo.version.c_str(), false, "starting");
      if (performUpdate(otaInfo)) {
        // Never reaches here — device reboots
      } else {
        sendOtaAck(backendBase, apiKey, relayId.c_str(), otaInfo.version.c_str(), false, "flash_failed");
      }
    } else {
      sendOtaAck(backendBase, apiKey, relayId.c_str(), FIRMWARE_VERSION, true, "up_to_date");
    }
    lastOtaCheckTime = millis();
  }

  Serial.println("Polling backend for relay commands...");
}


// ───────────── Loop ─────────────

void loop() {
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost. Reconnecting...");
    WiFi.reconnect();
    delay(2000);
    return;
  }

  unsigned long currentInterval = lastPollSuccessful ? pollIntervalMs : 1000; // Aggressive 1s retry on fail 

  if (now - lastPollTime >= currentInterval) {
    lastPollTime = now;
    lastPollSuccessful = pollCommands();
    
    if (!lastPollSuccessful) {
      consecutiveFailures++;
      if (consecutiveFailures >= 3) {
        Serial.println("🔄 3 consecutive failures. Resetting Wi-Fi stack...");
        WiFi.disconnect();
        WiFi.reconnect();
        consecutiveFailures = 0;
      }
    } else {
      consecutiveFailures = 0;
      reportStatus(); // Success! Confirm status immediately
    }
  }


  // Safety Watchdog: Re-check time *after* networking to prevent lag-induced trigger
  now = millis(); 
  if (irrigationState && (now - irrigationOnSince >= AUTO_OFF_TIMEOUT)) {
    Serial.printf("🚨 WATCHDOG: Irrigation triggered. Uptime: %lu, Started: %lu, Elapsed: %lu\n", now, irrigationOnSince, now - irrigationOnSince);
    setIrrigation(false);
    reportStatus();
  }

  now = millis();
  if (waterTankState && (now - waterTankOnSince >= AUTO_OFF_TIMEOUT)) {
    Serial.printf("🚨 WATCHDOG: Water Tank triggered. Uptime: %lu, Started: %lu, Elapsed: %lu\n", now, waterTankOnSince, now - waterTankOnSince);
    setWaterTank(false);
    reportStatus();
  }

  delay(100);

  // ═══════════════ Periodic OTA Check ═══════════════
  now = millis();
  if (otaEnabled && (now - lastOtaCheckTime >= otaCheckIntervalMs)) {
    lastOtaCheckTime = now;
    Serial.println("[OTA] Periodic check...");
    OTAInfo otaInfo = checkForUpdate(NODE_TYPE, FIRMWARE_VERSION);
    if (otaInfo.available) {
      sendOtaAck(backendBase, apiKey, relayId.c_str(), otaInfo.version.c_str(), false, "starting");
      if (performUpdate(otaInfo)) {
        // Never reached — reboots
      } else {
        sendOtaAck(backendBase, apiKey, relayId.c_str(), otaInfo.version.c_str(), false, "flash_failed");
      }
    } else {
      sendOtaAck(backendBase, apiKey, relayId.c_str(), FIRMWARE_VERSION, true, "up_to_date");
    }
  }
}
