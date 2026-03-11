/*
 * ESP32 Battery Test — Sensor Node
 *
 * Connects to Logger via BLE → sends dummy data → disconnects →
 * waits 5 minutes → repeats. No sleep modes.
 *
 * Brownout detector disabled as temporary fix while sorting power.
 */

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEClient.h>

// ── BLE UUIDs (must match Logger Node) ──────────────────────────
#define SERVICE_UUID        "0000181B-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "00002A1F-0000-1000-8000-00805f9b34fb"
#define LOGGER_NAME         "ESP32_Logger"

// ── Hardware ────────────────────────────────────────────────────
#define STATUS_LED 2

// ── Interval between sends (ms) ─────────────────────────────────
#define SEND_INTERVAL_MS (5UL * 60UL * 1000UL)   // 5 minutes

// ── Cycle counter ────────────────────────────────────────────────
int cycleCount = 0;

// ── BLE globals ──────────────────────────────────────────────────
static BLEAddress* pLoggerAddress = nullptr;
static bool        foundLogger    = false;

// ════════════════════════════════════════════════════════════════
class AdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    if (advertisedDevice.getName() == LOGGER_NAME) {
      Serial.println("   ✅ Logger found!");
      advertisedDevice.getScan()->stop();
      pLoggerAddress = new BLEAddress(advertisedDevice.getAddress());
      foundLogger = true;
    }
  }
};

// ════════════════════════════════════════════════════════════════
bool sendToLogger(float temp, float hum) {
  foundLogger    = false;
  pLoggerAddress = nullptr;

  // ── Scan ────────────────────────────────────────────────────
  Serial.println("📡 Scanning for Logger...");
  BLEScan* pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks());
  pScan->setActiveScan(true);
  pScan->start(10);
  pScan->clearResults();

  if (!foundLogger || pLoggerAddress == nullptr) {
    Serial.println("❌ Logger not found.");
    return false;
  }

  // ── Connect ─────────────────────────────────────────────────
  Serial.println("🔗 Connecting...");
  BLEClient* pClient = BLEDevice::createClient();

  if (!pClient->connect(*pLoggerAddress)) {
    Serial.println("❌ Connect failed.");
    delete pClient;
    return false;
  }
  Serial.println("   Connected!");

  // ── Service ─────────────────────────────────────────────────
  BLERemoteService* pService = pClient->getService(SERVICE_UUID);
  if (pService == nullptr) {
    Serial.println("❌ Service not found.");
    pClient->disconnect();
    delete pClient;
    return false;
  }

  // ── Characteristic ──────────────────────────────────────────
  BLERemoteCharacteristic* pChar = pService->getCharacteristic(CHARACTERISTIC_UUID);
  if (pChar == nullptr) {
    Serial.println("❌ Characteristic not found.");
    pClient->disconnect();
    delete pClient;
    return false;
  }

  // ── Write ────────────────────────────────────────────────────
  char payload[32];
  snprintf(payload, sizeof(payload), "T:%.1f,H:%.1f", temp, hum);
  Serial.printf("   📤 Sending: %s\n", payload);
  pChar->writeValue(payload, strlen(payload));
  delay(200);

  // ── Disconnect ───────────────────────────────────────────────
  pClient->disconnect();
  delay(200);
  delete pClient;
  Serial.println("   Disconnected.");
  return true;
}

// ════════════════════════════════════════════════════════════════
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  // disable brownout

  Serial.begin(115200);
  delay(500);

  Serial.println("================================");
  Serial.println("  ESP32 Sensor Node");
  Serial.println("================================");

  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, HIGH);

  BLEDevice::init("ESP32_Sensor");
  Serial.printf("   Free heap: %lu bytes\n", ESP.getFreeHeap());
  Serial.println("   Ready. First send in 5 seconds...");
  delay(5000);
}

// ════════════════════════════════════════════════════════════════
void loop() {
  cycleCount++;

  Serial.println("================================");
  Serial.printf("── Cycle #%d ──\n", cycleCount);

  digitalWrite(STATUS_LED, HIGH);

  // Dummy values — replace with real sensor later
  float temp = 23.5;
  float hum  = 58.0;

  if (sendToLogger(temp, hum)) {
    Serial.println("✅ Done.");
  } else {
    Serial.println("⚠️  Send failed — will retry next cycle.");
  }

  digitalWrite(STATUS_LED, LOW);

  Serial.printf("⏳ Waiting %lu minutes...\n", SEND_INTERVAL_MS / 60000UL);
  delay(SEND_INTERVAL_MS);
}