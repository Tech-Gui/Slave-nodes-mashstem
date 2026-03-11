/*
 * ESP32 Battery Test — Logger Node (BLE Server)
 *
 * Stays powered on and advertises a BLE service. The Sensor Node
 * connects periodically and writes temperature/humidity data to
 * the characteristic. Each write is logged to Serial.
 *
 * Keep this ESP32 USB-powered (or on a separate supply) since it
 * must stay on continuously.
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ── BLE UUIDs (must match Sensor Node) ──────────────────────────
#define SERVICE_UUID        "0000181B-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "00002A1F-0000-1000-8000-00805f9b34fb"

// ── Hardware ────────────────────────────────────────────────────
#define STATUS_LED  2

// ── Globals ─────────────────────────────────────────────────────
BLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected    = false;
bool oldDeviceConnected = false;
int  readingCount       = 0;

// ── Connection callbacks ────────────────────────────────────────
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    digitalWrite(STATUS_LED, HIGH);
    Serial.println("── Sensor Node connected ──");
  }

  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    digitalWrite(STATUS_LED, LOW);
    Serial.println("── Sensor Node disconnected ──");
  }
};

// ── Write callback — fires whenever the Sensor writes data ─────
class WriteCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    String value = pChar->getValue();
    if (value.length() == 0) return;

    readingCount++;

    // Parse T:xx.x,H:yy.y
    float temp = 0, hum = 0;
    if (sscanf(value.c_str(), "T:%f,H:%f", &temp, &hum) == 2) {
      unsigned long uptime = millis() / 1000;
      Serial.println("========================================");
      Serial.printf("  📥 Reading #%d  (uptime %lu s)\n", readingCount, uptime);
      Serial.printf("  🌡️  Temperature : %.1f °C\n", temp);
      Serial.printf("  💧 Humidity    : %.1f %%\n", hum);
      Serial.println("========================================");
    } else {
      Serial.printf("⚠️  Raw data received: %s\n", value.c_str());
    }
  }
};

// ════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("========================================");
  Serial.println("  Battery Test — Logger Node");
  Serial.println("========================================");

  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);

  // ── Initialise BLE Server ─────────────────────────────────────
  BLEDevice::init("ESP32_Logger");

  BLEServer* pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ  |
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );

  pCharacteristic->setCallbacks(new WriteCallback());
  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setValue("waiting");

  pService->start();

  // ── Start advertising ─────────────────────────────────────────
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("✅ BLE Logger Ready — waiting for Sensor Node...");
  Serial.printf("   Service UUID: %s\n", SERVICE_UUID);
  Serial.printf("   Readings received so far: %d\n", readingCount);
  Serial.println("========================================");
}

void loop() {
  // Restart advertising after a disconnect so the Sensor can
  // reconnect on its next wake cycle.
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    BLEDevice::startAdvertising();
    Serial.println("📡 Re-advertising...");
    oldDeviceConnected = false;
  }

  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = true;
  }

  delay(100);
}
