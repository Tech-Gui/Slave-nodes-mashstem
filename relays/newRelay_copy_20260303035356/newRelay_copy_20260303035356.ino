/*
 * ESP32 Dual-Channel Relay Node
 *
 * This device acts as a BLE peripheral that listens for commands
 * from a central gateway (nRF9160) to control TWO relay channels:
 *   - Channel 1: Irrigation Pump     (GPIO 26)
 *   - Channel 2: Water Tank Pump     (GPIO 27)
 *
 * Single BLE service with two characteristics (one per channel).
 */
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ───────────── Hardware Configuration ─────────────
#define IRRIGATION_RELAY_PIN 26   // Channel 1: Irrigation pump
#define WATER_TANK_RELAY_PIN 27   // Channel 2: Water tank pump
#define LED_PIN              2    // Onboard LED

// ───────────── BLE Configuration ─────────────
// Service UUID (shared for both channels)
#define DUAL_RELAY_SERVICE_UUID             "12345678-1234-1234-1234-123456789abc"

// Characteristic UUIDs (one per channel)
#define IRRIGATION_CMD_CHARACTERISTIC_UUID  "87654321-4321-4321-4321-cba987654322"
#define WATER_TANK_CMD_CHARACTERISTIC_UUID  "87654321-4321-4321-4321-cba987654321"

// ───────────── System Variables ─────────────
bool deviceConnected = false;
bool irrigationPumpState = false;
bool waterTankPumpState = false;
unsigned long lastStatusTime = 0;

BLECharacteristic* pIrrigationChar = NULL;
BLECharacteristic* pWaterTankChar  = NULL;

// Forward declarations
void blinkLED();
void printStatus();

// ═══════════════════════════════════════════════
// BLE Server Callbacks
// ═══════════════════════════════════════════════
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      digitalWrite(LED_PIN, HIGH);
      Serial.println("========================================");
      Serial.println("Central Gateway Connected");
      Serial.println("========================================");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      digitalWrite(LED_PIN, LOW);
      Serial.println("========================================");
      Serial.println("Central Gateway Disconnected");
      Serial.println("========================================");
      // Restart advertising to allow for reconnection
      BLEDevice::startAdvertising();
      Serial.println("Advertising restarted...");
    }
};

// ═══════════════════════════════════════════════
// Irrigation Channel Callbacks
// ═══════════════════════════════════════════════
class IrrigationCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue();

      if (value.length() > 0) {
        Serial.print("[Irrigation] Received: ");
        Serial.println(value);

        if (value == "IR_ON") {
          digitalWrite(IRRIGATION_RELAY_PIN, LOW);  // Active LOW
          irrigationPumpState = true;
          blinkLED();
          pCharacteristic->setValue("ACK_IR_ON");
          pCharacteristic->notify();
          Serial.println("🌱 Irrigation pump turned ON");
        }
        else if (value == "IR_OFF") {
          digitalWrite(IRRIGATION_RELAY_PIN, HIGH);  // Active LOW
          irrigationPumpState = false;
          blinkLED();
          pCharacteristic->setValue("ACK_IR_OFF");
          pCharacteristic->notify();
          Serial.println("🛑 Irrigation pump turned OFF");
        }
        else {
          Serial.println("[Irrigation] Unknown Command: " + value);
          pCharacteristic->setValue("ERROR_UNKNOWN_CMD");
          pCharacteristic->notify();
        }

        printStatus();
      }
    }
};

// ═══════════════════════════════════════════════
// Water Tank Channel Callbacks
// ═══════════════════════════════════════════════
class WaterTankCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue();

      if (value.length() > 0) {
        Serial.print("[Water Tank] Received: ");
        Serial.println(value);

        if (value == "WT_ON") {
          digitalWrite(WATER_TANK_RELAY_PIN, LOW);  // Active LOW
          waterTankPumpState = true;
          blinkLED();
          pCharacteristic->setValue("ACK_WT_ON");
          pCharacteristic->notify();
          Serial.println("💧 Water tank pump turned ON");
        }
        else if (value == "WT_OFF") {
          digitalWrite(WATER_TANK_RELAY_PIN, HIGH);  // Active LOW
          waterTankPumpState = false;
          blinkLED();
          pCharacteristic->setValue("ACK_WT_OFF");
          pCharacteristic->notify();
          Serial.println("🛑 Water tank pump turned OFF");
        }
        else {
          Serial.println("[Water Tank] Unknown Command: " + value);
          pCharacteristic->setValue("ERROR_UNKNOWN_CMD");
          pCharacteristic->notify();
        }

        printStatus();
      }
    }
};

// ═══════════════════════════════════════════════
// Helper Functions
// ═══════════════════════════════════════════════
void blinkLED() {
  digitalWrite(LED_PIN, LOW);
  delay(100);
  digitalWrite(LED_PIN, HIGH);
}

void printStatus() {
  Serial.print("  Irrigation: ");
  Serial.print(irrigationPumpState ? "ON 🌱" : "OFF");
  Serial.print(" | Water Tank: ");
  Serial.println(waterTankPumpState ? "ON 💧" : "OFF");
}

// ═══════════════════════════════════════════════
// Setup
// ═══════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial.println("=== ESP32 Dual-Channel Relay Node Starting ===");

  // Setup hardware pins
  pinMode(IRRIGATION_RELAY_PIN, OUTPUT);
  pinMode(WATER_TANK_RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  // Start with both pumps OFF (HIGH for active-low relay)
  digitalWrite(IRRIGATION_RELAY_PIN, HIGH);
  digitalWrite(WATER_TANK_RELAY_PIN, HIGH);
  digitalWrite(LED_PIN, LOW);
  irrigationPumpState = false;
  waterTankPumpState = false;

  // Create the BLE Device
  BLEDevice::init("ESP32_Dual_Relay");

  Serial.println("Device Name: ESP32_Dual_Relay");

  // Create the BLE Server
  BLEServer* pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(BLEUUID(DUAL_RELAY_SERVICE_UUID), 20);

  // ── Irrigation Characteristic ──
  pIrrigationChar = pService->createCharacteristic(
                      IRRIGATION_CMD_CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pIrrigationChar->setCallbacks(new IrrigationCallbacks());
  pIrrigationChar->addDescriptor(new BLE2902());

  // ── Water Tank Characteristic ──
  pWaterTankChar = pService->createCharacteristic(
                      WATER_TANK_CMD_CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pWaterTankChar->setCallbacks(new WaterTankCallbacks());
  pWaterTankChar->addDescriptor(new BLE2902());

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(DUAL_RELAY_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("========================================");
  Serial.println("🔌 BLE Dual-Channel Relay Control Ready!");
  Serial.println("Waiting for nRF9160 to connect...");
  Serial.println("Commands:");
  Serial.println("  IR_ON  / IR_OFF  - Irrigation pump");
  Serial.println("  WT_ON  / WT_OFF  - Water tank pump");
  Serial.println("========================================");
}

// ═══════════════════════════════════════════════
// Main Loop
// ═══════════════════════════════════════════════
void loop() {
  unsigned long currentTime = millis();

  // Print status every 10 seconds
  if (currentTime - lastStatusTime >= 10000) {
    lastStatusTime = currentTime;
    Serial.print("Status: ");
    Serial.print(deviceConnected ? "CONNECTED" : "DISCONNECTED");
    Serial.print(" | Irrigation: ");
    Serial.print(irrigationPumpState ? "ON 🌱" : "OFF");
    Serial.print(" | Water Tank: ");
    Serial.print(waterTankPumpState ? "ON 💧" : "OFF");
    Serial.print(" | Uptime: ");
    Serial.print(millis() / 1000);
    Serial.println("s");
  }

  // Handle LED when not connected (blink to show alive)
  if (!deviceConnected) {
    static unsigned long lastBlink = 0;
    static bool ledState = false;

    if (currentTime - lastBlink >= 1000) {
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
      lastBlink = currentTime;
    }
  }

  delay(100);
}