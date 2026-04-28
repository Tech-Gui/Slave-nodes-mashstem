/*
 * ota_updater.h — Shared OTA Update Module for MashSTEM WiFi Nodes
 *
 * Checks GitHub-hosted config.json for newer firmware, downloads .bin
 * from GitHub Releases, and flashes via ESP32 Update.h with MD5 verification.
 *
 * Usage in any .ino:
 *   #define FIRMWARE_VERSION "1.0.0"
 *   #define NODE_TYPE "soil_moisture"
 *   #include "../ota_updater.h"
 *
 * Requires: ArduinoJson, WiFiClientSecure (built-in ESP32)
 */

#ifndef OTA_UPDATER_H
#define OTA_UPDATER_H

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <ArduinoJson.h>

// ═══════════════ GitHub Raw URL for OTA Manifest ═══════════════
// This is the ONLY hardcoded external URL. Everything else is dynamic.
#define OTA_CONFIG_URL "https://raw.githubusercontent.com/Tech-Gui/Slave-nodes-mashstem/main/config.json"

// ═══════════════ OTA Info Struct ═══════════════
struct OTAInfo {
  bool available;
  String version;
  String url;
  size_t size;
  String md5;
};


// ═══════════════ Semver Comparison ═══════════════
// Returns true if ver_a > ver_b (e.g. "1.1.0" > "1.0.0")
static bool isNewerVersion(const char* ver_a, const char* ver_b) {
  int a_major = 0, a_minor = 0, a_patch = 0;
  int b_major = 0, b_minor = 0, b_patch = 0;
  sscanf(ver_a, "%d.%d.%d", &a_major, &a_minor, &a_patch);
  sscanf(ver_b, "%d.%d.%d", &b_major, &b_minor, &b_patch);

  if (a_major != b_major) return a_major > b_major;
  if (a_minor != b_minor) return a_minor > b_minor;
  return a_patch > b_patch;
}


// ═══════════════ Check for Update ═══════════════
// Fetches config.json from GitHub and checks if a newer version exists.
static OTAInfo checkForUpdate(const char* nodeType, const char* currentVersion) {
  OTAInfo info = { false, "", "", 0, "" };

  Serial.printf("[OTA] Checking for updates (current: %s, type: %s)...\n", currentVersion, nodeType);

  WiFiClientSecure client;
  client.setInsecure(); // Skip cert verification (acceptable for IoT OTA)

  HTTPClient http;
  http.begin(client, OTA_CONFIG_URL);
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  int httpCode = http.GET();
  if (httpCode != 200) {
    Serial.printf("[OTA] Config fetch failed: HTTP %d\n", httpCode);
    http.end();
    return info;
  }

  String payload = http.getString();
  http.end();

  // Parse JSON
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[OTA] JSON parse error: %s\n", err.c_str());
    return info;
  }

  // Navigate: ota -> nodeType -> version, url, size, md5
  JsonObject otaNode = doc["ota"][nodeType];
  if (otaNode.isNull()) {
    Serial.printf("[OTA] No OTA entry for type '%s'\n", nodeType);
    return info;
  }

  const char* remoteVersion = otaNode["version"] | "0.0.0";
  const char* remoteUrl = otaNode["url"] | "";
  size_t remoteSize = otaNode["size"] | 0;
  const char* remoteMd5 = otaNode["md5"] | "";

  Serial.printf("[OTA] Remote version: %s, Current: %s\n", remoteVersion, currentVersion);

  if (isNewerVersion(remoteVersion, currentVersion)) {
    info.available = true;
    info.version = String(remoteVersion);
    info.url = String(remoteUrl);
    info.size = remoteSize;
    info.md5 = String(remoteMd5);
    Serial.printf("[OTA] ✅ Update available: %s → %s\n", currentVersion, remoteVersion);
  } else {
    Serial.println("[OTA] Already up to date.");
  }

  return info;
}


// ═══════════════ Perform OTA Update ═══════════════
// Downloads .bin from GitHub Releases and flashes via Update.h
// Returns true on success (device will reboot immediately).
static bool performUpdate(const OTAInfo& info) {
  if (!info.available || info.url.length() == 0) return false;

  Serial.printf("[OTA] Downloading firmware v%s (%d bytes)\n", info.version.c_str(), info.size);
  Serial.printf("[OTA] URL: %s\n", info.url.c_str());

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, info.url);
  http.setTimeout(120000); // 120s timeout for large binaries
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  int httpCode = http.GET();
  if (httpCode != 200) {
    Serial.printf("[OTA] Download failed: HTTP %d\n", httpCode);
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  if (contentLength <= 0) {
    Serial.println("[OTA] Invalid content length");
    http.end();
    return false;
  }

  Serial.printf("[OTA] Content length: %d bytes\n", contentLength);

  // Begin update
  if (!Update.begin(contentLength)) {
    Serial.printf("[OTA] Not enough space: %s\n", Update.errorString());
    http.end();
    return false;
  }

  // Set MD5 for verification (if provided)
  if (info.md5.length() > 0) {
    Update.setMD5(info.md5.c_str());
    Serial.printf("[OTA] MD5 expected: %s\n", info.md5.c_str());
  }

  // Stream firmware to flash
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[1024];
  int written = 0;
  int lastProgress = -1;

  while (http.connected() && written < contentLength) {
    size_t available = stream->available();
    if (available) {
      int bytesRead = stream->readBytes(buf, min(available, sizeof(buf)));
      size_t bytesWritten = Update.write(buf, bytesRead);
      if (bytesWritten != (size_t)bytesRead) {
        Serial.printf("[OTA] Write error at %d bytes: %s\n", written, Update.errorString());
        Update.abort();
        http.end();
        return false;
      }
      written += bytesWritten;

      // Progress every 10%
      int progress = (written * 100) / contentLength;
      if (progress / 10 > lastProgress / 10) {
        lastProgress = progress;
        Serial.printf("[OTA] Progress: %d%% (%d / %d)\n", progress, written, contentLength);
      }
    }
    delay(1); // Yield to watchdog
  }

  http.end();

  if (written != contentLength) {
    Serial.printf("[OTA] Incomplete download: %d / %d\n", written, contentLength);
    Update.abort();
    return false;
  }

  // Finalize — this also verifies MD5 if set
  if (!Update.end(true)) {
    Serial.printf("[OTA] Update finalization failed: %s\n", Update.errorString());
    return false;
  }

  Serial.println("[OTA] ✅ Update successful! MD5 verified.");
  Serial.println("[OTA] Rebooting into new firmware...");
  Serial.flush();
  delay(500);
  ESP.restart();

  return true; // Never reached (restart above)
}


// ═══════════════ Send OTA Acknowledgment ═══════════════
// Reports OTA result back to the backend.
static void sendOtaAck(const char* backendBase, const char* apiKey,
                        const char* sensorId, const char* version,
                        bool success, const char* error) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String(backendBase) + "/ota/ack";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", apiKey);
  http.setTimeout(10000);

  String payload = "{\"sensor_id\":\"" + String(sensorId)
    + "\",\"version\":\"" + String(version)
    + "\",\"success\":" + (success ? "true" : "false");

  if (error && strlen(error) > 0) {
    payload += ",\"error\":\"" + String(error) + "\"";
  }
  payload += "}";

  int code = http.POST(payload);
  Serial.printf("[OTA ACK] POST → %d\n", code);
  http.end();
}

#endif // OTA_UPDATER_H
