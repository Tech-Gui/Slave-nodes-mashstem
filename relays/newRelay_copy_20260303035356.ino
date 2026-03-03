/*
 * ESP32 Water Pump Relay Node
 *
 * This device acts as a BLE peripheral that listens for commands
 * from a central gateway (nRF9160) to control a water pump relay.
 * Used for water level-based pump control.
 */
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Hardware Configuration
#define RELAY_PIN 4
#define LED_PIN 2
#define STATUS_PIN 5  // Optional: pin to show relay status with external LED

// BLE Configuration
#define WATER_RELAY_SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define WATER_COMMAND_CHARACTERISTIC_UUID "87654321-4321-4321-4321-cba987654321"

// System Variables
bool deviceConnected = false;
bool pumpState = false;  // Track pump state
unsigned long lastStatusTime = 0;

// BLE Server Callbacks
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      digitalWrite(LED_PIN, HIGH); // Turn on LED to indicate connection
      Serial.println("========================================");
      Serial.println("Central Gateway Connected");
      Serial.println("========================================");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      // LED shows pump state when disconnected
      digitalWrite(LED_PIN, pumpState ? HIGH : LOW);
      Serial.println("========================================");
      Serial.println("Central Gateway Disconnected");
      Serial.println("========================================");
      // Restart advertising to allow for reconnection
      BLEDevice::startAdvertising();
      Serial.println("Advertising restarted...");
    }
};

// BLE Characteristic Callbacks
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue();
      
      if (value.length() > 0) {
        Serial.print("Received Command: ");
        Serial.println(value);
        
        if (value == "RELAY_ON") {
          // Turn ON water pump (assuming active LOW relay)
          digitalWrite(RELAY_PIN, LOW);
          digitalWrite(STATUS_PIN, HIGH);
          pumpState = true;
          
          // LED is always on when connected, blink to show command received
          digitalWrite(LED_PIN, LOW);
          delay(100);
          digitalWrite(LED_PIN, HIGH);
          
          pCharacteristic->setValue("ACK_PUMP_ON");
          pCharacteristic->notify();
          Serial.println("💧 Water pump turned ON");
        } 
        else if (value == "RELAY_OFF") {
          // Turn OFF water pump (assuming active LOW relay)
          digitalWrite(RELAY_PIN, HIGH);
          digitalWrite(STATUS_PIN, LOW);
          pumpState = false;
          
          // LED blink to show command received
          digitalWrite(LED_PIN, LOW);
          delay(100);
          digitalWrite(LED_PIN, HIGH);
          
          pCharacteristic->setValue("ACK_PUMP_OFF");
          pCharacteristic->notify();
          Serial.println("🛑 Water pump turned OFF");
        } 
        else {
          Serial.println("Unknown Command: " + value);
          pCharacteristic->setValue("ERROR_UNKNOWN_CMD");
          pCharacteristic->notify();
        }
        
        // Print status after command
        Serial.print("Water Pump Status: ");
        Serial.println(pumpState ? "RUNNING (Pumping water)" : "STOPPED");
      }
    }
};

void setup() {
  Serial.begin(115200);
  Serial.println("=== ESP32 Water Pump Relay Node Starting ===");
  
  // Setup hardware pins
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(STATUS_PIN, OUTPUT);
  
  // Start with pump OFF (HIGH for active-low relay)
  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(STATUS_PIN, LOW);
  pumpState = false;
  
  // Create the BLE Device
  BLEDevice::init("ESP32_Water_Relay");
  
  Serial.print("Device Name: ESP32_Water_Relay");
  Serial.println();
  
  // Create the BLE Server
  BLEServer* pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  // Create the BLE Service
  BLEService *pService = pServer->createService(WATER_RELAY_SERVICE_UUID);
  
  // Create a BLE Characteristic
  BLECharacteristic* pCharacteristic = pService->createCharacteristic(
                      WATER_COMMAND_CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY // For sending ACKs
                    );

  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(WATER_RELAY_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  
  Serial.println("========================================");
  Serial.println("💧 BLE Water Pump Control Ready!");
  Serial.println("Waiting for nRF9160 to connect...");
  Serial.println("Commands:");
  Serial.println("  RELAY_ON  - Start water pump");
  Serial.println("  RELAY_OFF - Stop water pump");
  Serial.println("========================================");
}

void loop() {
  // Status reporting and connection status LED
  unsigned long currentTime = millis();
  
  // Print status every 10 seconds
  if (currentTime - lastStatusTime >= 10000) {
    lastStatusTime = currentTime;
    Serial.print("Status: ");
    Serial.print(deviceConnected ? "CONNECTED" : "DISCONNECTED");
    Serial.print(" | Water Pump: ");
    Serial.print(pumpState ? "RUNNING 💧" : "STOPPED 🛑");
    Serial.print(" | Uptime: ");
    Serial.print(millis() / 1000);
    Serial.println("s");
    
    // Show water level reminder
    if (pumpState) {
      Serial.println("🏊 Water pump is filling the tank");
    }
  }
  
  // Handle LED when not connected (blink to show alive)
  if (!deviceConnected) {
    static unsigned long lastBlink = 0;
    static bool ledState = false;
    
    if (currentTime - lastBlink >= 1000) {  // Blink every second
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
      lastBlink = currentTime;
    }
  }
  
  delay(100);
}