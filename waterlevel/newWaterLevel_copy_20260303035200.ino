/*
 * ESP32 Water Level Sensor Node
 *
 * This device measures distance using an ultrasonic sensor and reports
 * the value to a central gateway (nRF9160) via BLE notifications.
 * It acts as a BLE peripheral/server.
 *
 * All WiFi, HTTP, and BLE client logic has been removed.
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Hardware Configuration
#define TRIG_PIN 18
#define ECHO_PIN 19
#define STATUS_LED_PIN 2

// BLE Configuration
// Using standard Environmental Sensing service and a custom characteristic for simplicity
#define SENSOR_SERVICE_UUID         "0000181A-0000-1000-8000-00805f9b34fb"
#define DISTANCE_CHARACTERISTIC_UUID "00002A6C-0000-1000-8000-00805f9b34fb" // Elevation, repurposed for distance

// System Variables
BLECharacteristic* pDistanceCharacteristic = nullptr;
bool deviceConnected = false;
unsigned long lastReadingTime = 0;
const unsigned long READING_INTERVAL = 5000; // Send data every 5 seconds

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

// Function to read distance from HC-SR04 sensor
float readDistance() {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    
    // Reads the echoPin, returns the sound wave travel time in microseconds
    // Timeout set to 30ms, which is ~5 meters.
    long duration = pulseIn(ECHO_PIN, HIGH, 30000);
    
    if (duration == 0) {
        return -1.0; // Indicate a timeout or failed reading
    }
    
    // Calculating the distance in cm
    // Speed of sound wave divided by two (go and back)
    float distance = (duration * 0.0343) / 2;
    
    // Return 0 if sensor is out of range (e.g., > 400 cm)
    return (distance > 400) ? -1.0 : distance;
}


void setup() {
    Serial.begin(115200);
    Serial.println("=== ESP32 Water Level Sensor Node Starting ===");

    // Setup hardware pins
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    // Generate a unique device name
    uint64_t chipid = ESP.getEfuseMac();
    char macStr[5];
    sprintf(macStr, "%02X%02X", (uint8_t)(chipid >> 8), (uint8_t)chipid);
    String deviceName = "ESP32_Water_Sensor";

    Serial.print("Device Name: ");
    Serial.println(deviceName);

    // Initialize BLE
    BLEDevice::init(deviceName.c_str());

    // Create the BLE Server
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    // Create the BLE Service
    BLEService *pService = pServer->createService(SENSOR_SERVICE_UUID);

    // Create the Distance Characteristic
    pDistanceCharacteristic = pService->createCharacteristic(
                                  DISTANCE_CHARACTERISTIC_UUID,
                                  BLECharacteristic::PROPERTY_READ |
                                  BLECharacteristic::PROPERTY_NOTIFY
                              );

    // Add a descriptor for notifications
    pDistanceCharacteristic->addDescriptor(new BLE2902());

    // Start the service
    pService->start();

    // Start advertising
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SENSOR_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.println("========================================");
    Serial.println("BLE Peripheral Ready!");
    Serial.println("Waiting for nRF9160 to connect...");
    Serial.println("========================================");
}

void loop() {
    if (deviceConnected) {
        unsigned long currentTime = millis();
        if (currentTime - lastReadingTime >= READING_INTERVAL) {
            lastReadingTime = currentTime;

            float distance = readDistance();

            if (distance >= 0) {
                Serial.print("Distance reading: ");
                Serial.print(distance);
                Serial.println(" cm");

                // Format the distance to one decimal place
                char buffer[10];
                int len = snprintf(buffer, sizeof(buffer), "%.1f", distance);
                pDistanceCharacteristic->setValue((uint8_t*)buffer, len);
                pDistanceCharacteristic->notify();


                Serial.print("Sent notification: ");
                Serial.println(buffer);
            } else {
                Serial.println("Invalid distance reading.");
            }
        }
    }
    
    // A small delay to keep the loop from running too fast
    delay(100);
}