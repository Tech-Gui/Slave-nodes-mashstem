/*
 * ESP32 Soil Moisture Sensor Node
 *
 * This device measures soil moisture using a capacitive or resistive sensor
 * and reports the value to a central gateway (nRF9160) via BLE notifications.
 * It acts as a BLE peripheral/server.
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Hardware Configuration
#define SOIL_SENSOR_PIN 34  // Analog pin for soil moisture sensor
#define STATUS_LED_PIN 2
#define POWER_PIN 4         // Pin to power the sensor (to reduce corrosion)

// BLE Configuration
// Using Environmental Sensing service with custom characteristic for soil moisture
#define SOIL_SERVICE_UUID         "0000181F-0000-1000-8000-00805f9b34fb"  // Environmental Sensing
#define MOISTURE_CHARACTERISTIC_UUID "00002A9F-0000-1000-8000-00805f9b34fb" // Moisture, repurposed

// Calibration values - adjust these based on your sensor
// #define DRY_VALUE 4095     // ADC value when sensor is in dry air
// #define WET_VALUE 1500     // ADC value when sensor is in water

const int DRY_VALUE = 3200;
const int WET_VALUE = 1300;
#define MIN_MOISTURE 0     // Minimum moisture percentage
#define MAX_MOISTURE 100   // Maximum moisture percentage

// System Variables
BLECharacteristic* pMoistureCharacteristic = nullptr;
bool deviceConnected = false;
unsigned long lastReadingTime = 0;
const unsigned long READING_INTERVAL = 10000; // Send data every 10 seconds
const unsigned long POWER_ON_DELAY = 100;     // Delay after powering sensor

// BLE Server Callbacks
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        digitalWrite(STATUS_LED_PIN, HIGH);
        Serial.println("========================================");
        Serial.println("Central Gateway Connected");
        Serial.println("========================================");
    };

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        digitalWrite(STATUS_LED_PIN, LOW);
        Serial.println("========================================");
        Serial.println("Central Gateway Disconnected");
        Serial.println("========================================");
        // Restart advertising to allow for reconnection
        BLEDevice::startAdvertising();
        Serial.println("Advertising restarted...");
    }
};

// Function to read soil moisture percentage
float readSoilMoisture() {
    // Power on the sensor
    digitalWrite(POWER_PIN, HIGH);
    delay(POWER_ON_DELAY);
    
    // Take multiple readings and average them
    int totalReading = 0;
    const int numReadings = 5;
    
    for (int i = 0; i < numReadings; i++) {
        totalReading += analogRead(SOIL_SENSOR_PIN);
        delay(10);
    }
    
    // Power off the sensor to prevent corrosion
    digitalWrite(POWER_PIN, LOW);
    
    int avgReading = totalReading / numReadings;
    
    // Convert ADC reading to moisture percentage
    // Higher ADC values usually mean drier soil for capacitive sensors
    float moisture = map(avgReading, DRY_VALUE, WET_VALUE, MIN_MOISTURE, MAX_MOISTURE);
    
    // Constrain to valid range
    moisture = constrain(moisture, MIN_MOISTURE, MAX_MOISTURE);
    
    Serial.print("Raw ADC: ");
    Serial.print(avgReading);
    Serial.print(" -> Moisture: ");
    Serial.print(moisture);
    Serial.println("%");
    
    return moisture;
}

void setup() {
    Serial.begin(115200);
    Serial.println("=== ESP32 Soil Moisture Sensor Node Starting ===");

    // Setup hardware pins
    pinMode(SOIL_SENSOR_PIN, INPUT);
    pinMode(STATUS_LED_PIN, OUTPUT);
    pinMode(POWER_PIN, OUTPUT);
    
    digitalWrite(STATUS_LED_PIN, LOW);
    digitalWrite(POWER_PIN, LOW); // Start with sensor powered off

    // Generate a unique device name
    uint64_t chipid = ESP.getEfuseMac();
    char macStr[5];
    sprintf(macStr, "%02X%02X", (uint8_t)(chipid >> 8), (uint8_t)chipid);
    String deviceName = "ESP32_Soil_Sensor_" + String(macStr);

    Serial.print("Device Name: ");
    Serial.println(deviceName);

    // Print calibration values
    Serial.println("========================================");
    Serial.println("Sensor Calibration Values:");
    Serial.print("Dry Value (0%): ");
    Serial.println(DRY_VALUE);
    Serial.print("Wet Value (100%): ");
    Serial.println(WET_VALUE);
    Serial.println("Adjust DRY_VALUE and WET_VALUE in code");
    Serial.println("based on your sensor's characteristics");
    Serial.println("========================================");

    // Initialize BLE
    BLEDevice::init(deviceName.c_str());

    // Create the BLE Server
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    // Create the BLE Service
    BLEService *pService = pServer->createService(SOIL_SERVICE_UUID);

    // Create the Moisture Characteristic
    pMoistureCharacteristic = pService->createCharacteristic(
                                  MOISTURE_CHARACTERISTIC_UUID,
                                  BLECharacteristic::PROPERTY_READ |
                                  BLECharacteristic::PROPERTY_NOTIFY
                              );

    // Add a descriptor for notifications
    pMoistureCharacteristic->addDescriptor(new BLE2902());

    // Start the service
    pService->start();

    // Start advertising
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SOIL_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.println("========================================");
    Serial.println("BLE Peripheral Ready!");
    Serial.println("Waiting for nRF9160 to connect...");
    Serial.println("========================================");

    // Take an initial reading for calibration check
    delay(1000);
    float initialReading = readSoilMoisture();
    Serial.print("Initial moisture reading: ");
    Serial.print(initialReading);
    Serial.println("%");
    
    if (initialReading < 0 || initialReading > 100) {
        Serial.println("WARNING: Reading is out of range!");
        Serial.println("Check sensor connections and calibration values.");
    }
}

void loop() {
    if (deviceConnected) {
        unsigned long currentTime = millis();
        if (currentTime - lastReadingTime >= READING_INTERVAL) {
            lastReadingTime = currentTime;

            float moisturePercent = readSoilMoisture();

            // Validate reading
            if (moisturePercent >= 0 && moisturePercent <= 100) {
                Serial.print("Soil moisture reading: ");
                Serial.print(moisturePercent);
                Serial.println("%");

                // Format the moisture to one decimal place
                char buffer[10];
                dtostrf(moisturePercent, 4, 1, buffer);

                // Set characteristic value and notify the central
                pMoistureCharacteristic->setValue(buffer);
                pMoistureCharacteristic->notify();

                Serial.print("Sent notification: ");
                Serial.println(buffer);
            } else {
                Serial.println("Invalid moisture reading, skipping transmission.");
            }
        }
    }
    
    // Status LED blink when not connected
    if (!deviceConnected) {
        static unsigned long lastBlink = 0;
        static bool ledState = false;
        
        if (millis() - lastBlink >= 1000) {
            ledState = !ledState;
            digitalWrite(STATUS_LED_PIN, ledState);
            lastBlink = millis();
        }
    }
    
    delay(100);
}