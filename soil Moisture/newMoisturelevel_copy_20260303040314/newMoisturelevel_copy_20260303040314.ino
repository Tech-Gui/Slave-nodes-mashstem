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
const unsigned long READING_INTERVAL = 300000; // Send data every 5 minutes (300,000 ms)
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
        Serial.println("Central Gateway Disconnected");
        // Radio is now managed explicitly in the loop()
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
    Serial.println("ESP32 Soil Moisture Node Starting");

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

    // Start advertising (Wait! We now start this only when data is ready in loop)
    // BLEDevice::startAdvertising();

    Serial.println("Ready. Waiting for connection...");

    // Take an initial reading
    delay(1000);
    float initialReading = readSoilMoisture();
    Serial.printf("Test reading: %.1f%%\n", initialReading);
}

void loop() {
    unsigned long currentTime = millis();
    static unsigned long connectedSince = 0;
    static bool dataSent = false;
    
    // 1. Periodically read the sensor (every 5 minutes)
    if (currentTime - lastReadingTime >= READING_INTERVAL || lastReadingTime == 0) {
        lastReadingTime = currentTime;
        
        Serial.println("Cycle start");
        float moisturePercent = readSoilMoisture();
        
        if (moisturePercent >= 0 && moisturePercent <= 100) {
            Serial.println("Reading success. Radio ON.");
            BLEDevice::startAdvertising(); // Radio ON
            dataSent = false;
        } else {
            Serial.println("Reading failed. Radio OFF.");
        }
    }

    // 2. Handle transmission and duty cycle completion
    if (deviceConnected) {
        if (connectedSince == 0) connectedSince = millis();
        
        if (!dataSent && (millis() - connectedSince >= 2000)) {
            float moisturePercent = readSoilMoisture();
            char buffer[10];
            dtostrf(moisturePercent, 4, 1, buffer);

            Serial.print("Sending: ");
            Serial.println(buffer);

            pMoistureCharacteristic->setValue(buffer);
            pMoistureCharacteristic->notify();

            Serial.println("Notified. Disconnecting in 5s.");
            dataSent = true;
            delay(5000);

            // Active Disconnect and Radio OFF
            BLEDevice::getServer()->disconnect(0); 
            BLEDevice::getAdvertising()->stop(); // Radio OFF
            
            deviceConnected = false; 
            connectedSince = 0;
            Serial.println("Cycle complete. Radio IDLE.");
        }
    } else {
        connectedSince = 0;
    }
    
    // Status LED blink when advertising
    if (!deviceConnected) {
        static unsigned long lastBlink = 0;
        static bool ledState = false;
        
        // Blink only if we are actually advertising (roughly between reading success and connection)
        // Simplified check for now
        if (millis() - lastBlink >= 1000) {
            ledState = !ledState;
            digitalWrite(STATUS_LED_PIN, ledState);
            lastBlink = millis();
        }
    }
    
    delay(10);
}