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
bool oldDeviceConnected = false;
bool irrigationPumpState = false;
bool waterTankPumpState = false;
unsigned long lastStatusTime = 0;
unsigned long lastSyncTime = 0;      // Timer for periodic status sync to gateway
const unsigned long SYNC_INTERVAL = 30000; // 30 seconds sync interval
const unsigned long ADVERTISING_WATCHDOG_INTERVAL = 30000;
const unsigned long BLE_RECOVERY_REBOOT_TIMEOUT = 90000;

// 30-minute Safety Watchdog Timers
unsigned long lastIrrigationOnTime = 0;
unsigned long lastWaterTankOnTime = 0;
const unsigned long AUTO_OFF_TIMEOUT = 30 * 60 * 1000UL; // 30 minutes in ms
unsigned long disconnectedSince = 0;

BLECharacteristic* pIrrigationChar = NULL;
BLECharacteristic* pWaterTankChar  = NULL;
BLEServer* pServer = NULL;
BLEAdvertising* pAdvertising = NULL;

// Forward declarations
void blinkLED();
void printStatus();

// ═══════════════════════════════════════════════
// BLE Server Callbacks
// ═══════════════════════════════════════════════
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
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
          digitalWrite(IRRIGATION_RELAY_PIN, HIGH);  // Changed to Active HIGH
          irrigationPumpState = true;
          lastIrrigationOnTime = millis(); // Reset safety watchdog
          blinkLED();
          pCharacteristic->setValue("ACK_IR_ON");
          pCharacteristic->notify();
          Serial.println("🌱 Irrigation pump turned ON");
        }
        else if (value == "IR_OFF") {
          digitalWrite(IRRIGATION_RELAY_PIN, LOW);   // Changed to Active HIGH
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
          digitalWrite(WATER_TANK_RELAY_PIN, HIGH);  // Changed to Active HIGH
          waterTankPumpState = true;
          lastWaterTankOnTime = millis(); // Reset safety watchdog
          blinkLED();
          pCharacteristic->setValue("ACK_WT_ON");
          pCharacteristic->notify();
          Serial.println("💧 Water tank pump turned ON");
        }
        else if (value == "WT_OFF") {
          digitalWrite(WATER_TANK_RELAY_PIN, LOW);   // Changed to Active HIGH
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

  // Start with both pumps OFF
  // Assuming ACTIVE HIGH based on user report (HIGH turned them ON at boot)
  digitalWrite(IRRIGATION_RELAY_PIN, LOW);
  digitalWrite(WATER_TANK_RELAY_PIN, LOW);
  pinMode(IRRIGATION_RELAY_PIN, OUTPUT);
  pinMode(WATER_TANK_RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  irrigationPumpState = false;
  waterTankPumpState = false;

  // Create the BLE Device
  BLEDevice::init("ESP32_Dual_Relay");

  Serial.println("Device Name: ESP32_Dual_Relay");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
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
  pAdvertising = BLEDevice::getAdvertising();
  // DO NOT add the 128-bit Service UUID to advertising. It pushes the device name
  // out of the 31-byte primary packet. The gateway connects by Name anyway.
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06); // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
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
  static unsigned long lastAdvertiseRestart = 0;

  // Handle Disconnection Edge
  if (!deviceConnected && oldDeviceConnected) {
    disconnectedSince = millis();
    digitalWrite(LED_PIN, LOW);
    Serial.println("========================================");
    Serial.println("Central Gateway Disconnected");
    Serial.println("========================================");
    
    // Give the bluetooth stack the chance to get things ready
    delay(500);
    pServer->startAdvertising(); // restart advertising
    Serial.println("Advertising restarted for reconnection");
    
    oldDeviceConnected = deviceConnected;
  }

  // Handle Connection Edge
  if (deviceConnected && !oldDeviceConnected) {
    disconnectedSince = 0;
    digitalWrite(LED_PIN, HIGH);
    Serial.println("========================================");
    Serial.println("Central Gateway Connected");
    Serial.println("========================================");
    
    oldDeviceConnected = deviceConnected;
  }

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

  // Reconnection watchdog: if disconnected for > 30s, restart advertising
  // Handles cases where the BLE stack gets stuck after an unclean gateway disconnect
  if (!deviceConnected) {
    if (disconnectedSince == 0) {
      disconnectedSince = currentTime;
    }

    if (currentTime - lastAdvertiseRestart >= ADVERTISING_WATCHDOG_INTERVAL) {
      lastAdvertiseRestart = currentTime;
      Serial.println("Watchdog: Restarting BLE advertising...");
      pServer->startAdvertising();
      Serial.println("BLE recovery: watchdog timeout");
    }

    if (currentTime - disconnectedSince >= BLE_RECOVERY_REBOOT_TIMEOUT) {
      Serial.println("BLE watchdog: Relay stuck disconnected after recovery attempts. Rebooting ESP32...");
      delay(200);
      ESP.restart();
    }
  } else {
    disconnectedSince = 0;
    lastAdvertiseRestart = currentTime; // Reset timer while connected
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

  // Periodic Status Synchronization to Gateway (Phase 2)
  if (deviceConnected && (currentTime - lastSyncTime >= SYNC_INTERVAL)) {
    lastSyncTime = currentTime;
    Serial.println("🔄 Periodic Status Sync: Sending notifications...");
    
    // Notify target characteristic for Irrigation
    pIrrigationChar->setValue(irrigationPumpState ? "ACK_IR_ON" : "ACK_IR_OFF");
    pIrrigationChar->notify();
    
    // Notify target characteristic for Water Tank
    pWaterTankChar->setValue(waterTankPumpState ? "ACK_WT_ON" : "ACK_WT_OFF");
    pWaterTankChar->notify();
    
    blinkLED();
    Serial.println("Status sync complete.");
  }

  // Safety Pump Auto-Off Watchdog
  if (irrigationPumpState && (currentTime - lastIrrigationOnTime >= AUTO_OFF_TIMEOUT)) {
    Serial.println("🚨 WATCHDOG TRIGGERED: Irrigation Pump running for >30 mins! Auto-stopping.");
    digitalWrite(IRRIGATION_RELAY_PIN, LOW);
    irrigationPumpState = false;
    if (deviceConnected && pIrrigationChar) {
        pIrrigationChar->setValue("ACK_IR_OFF");
        pIrrigationChar->notify();
    }
  }

  if (waterTankPumpState && (currentTime - lastWaterTankOnTime >= AUTO_OFF_TIMEOUT)) {
    Serial.println("🚨 WATCHDOG TRIGGERED: Water Tank Pump running for >30 mins! Auto-stopping.");
    digitalWrite(WATER_TANK_RELAY_PIN, LOW);
    waterTankPumpState = false;
    if (deviceConnected && pWaterTankChar) {
        pWaterTankChar->setValue("ACK_WT_OFF");
        pWaterTankChar->notify();
    }
  }

  delay(100);
}
