/*
 * ESP32 Environmental Sensor Node
 *
 * This device measures temperature and humidity using DHT11/DHT22 sensor
 * and reports the values to a central gateway (nRF9160) via BLE notifications.
 * It acts as a BLE peripheral/server.
 *
 * MODIFIED: Data format changed to "T:xx.x,H:yy.y" to match nRF9160 gateway parser.
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <DHT.h>

// Hardware Configuration
#define DHT_PIN 4           // DHT11/DHT22 data pin
#define DHT_TYPE DHT11      // Change to DHT22 if using DHT22 sensor
#define STATUS_LED_PIN 2

// BLE Configuration
// Using Environmental Sensing service with custom characteristic for temp/humidity
#define ENVIRONMENTAL_SERVICE_UUID    "0000181B-0000-1000-8000-00805f9b34fb"  // Environmental Sensing
#define TEMP_HUMIDITY_CHAR_UUID       "00002A1F-0000-1000-8000-00805f9b34fb"  // Temperature Measurement, repurposed

// Sensor Objects
DHT dht(DHT_PIN, DHT_TYPE);

// System Variables
BLECharacteristic* pTempHumidityCharacteristic = nullptr;
bool deviceConnected = false;
unsigned long lastReadingTime = 0;
const unsigned long READING_INTERVAL = 10000; // Send data every 10 seconds

// Sensor readings
struct {
    float temperature;
    float humidity;
    bool valid;
    unsigned long lastUpdate;
} sensorData = {0};

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

// Function to read temperature and humidity from DHT sensor
bool readEnvironmentalData() {
    float temp = dht.readTemperature();
    float humidity = dht.readHumidity();
    
    // Check if readings are valid
    if (isnan(temp) || isnan(humidity)) {
        Serial.println("❌ Failed to read from DHT sensor!");
        sensorData.valid = false;
        return false;
    }
    
    // Update sensor data
    sensorData.temperature = temp;
    sensorData.humidity = humidity;
    sensorData.valid = true;
    sensorData.lastUpdate = millis();
    
    Serial.printf("📊 Environmental readings: T=%.1f°C, H=%.1f%%\n", temp, humidity);
    
    return true;
}

// Format sensor data for BLE transmission
String formatSensorData() {
    if (!sensorData.valid) {
        return "ERROR";
    }
    
    // --- CHANGE 1: Modified the format string to match the nRF9160 parser ---
    // The gateway expects "T:" and "H:" prefixes.
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "T:%.1f,H:%.1f", 
             sensorData.temperature, sensorData.humidity);
    
    return String(buffer);
}

void setup() {
    Serial.begin(115200);
    Serial.println("=== ESP32 Environmental Sensor Node Starting ===");
    Serial.printf("Sensor: DHT%d (Temperature + Humidity)\n", DHT_TYPE == DHT11 ? 11 : 22);

    // Setup hardware pins
    pinMode(DHT_PIN, INPUT_PULLUP);
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    // Initialize DHT sensor
    dht.begin();
    Serial.println("DHT sensor initialized");
    
    // Wait for sensor to stabilize
    delay(2000);

    // --- CHANGE 2: Set a clear, descriptive device name ---
    String deviceName = "ESP32_Environmental";

    Serial.print("Device Name: ");
    Serial.println(deviceName);

    // Test sensor before BLE initialization
    Serial.println("========================================");
    Serial.println("Testing environmental sensor...");
    
    if (readEnvironmentalData()) {
        Serial.printf("✅ Sensor test successful: %.1f°C, %.1f%%\n", 
                     sensorData.temperature, sensorData.humidity);
                     
        // Classify readings
        if (sensorData.temperature < 10) {
            Serial.println("🥶 Temperature: Cold");
        } else if (sensorData.temperature < 25) {
            Serial.println("🌡️  Temperature: Comfortable");
        } else {
            Serial.println("🔥 Temperature: Warm");
        }
        
        if (sensorData.humidity < 30) {
            Serial.println("🏜️  Humidity: Dry");
        } else if (sensorData.humidity < 60) {
            Serial.println("💧 Humidity: Comfortable");
        } else {
            Serial.println("💦 Humidity: Humid");
        }
    } else {
        Serial.println("❌ Sensor test failed - check DHT wiring!");
        Serial.println("Connections should be:");
        Serial.printf("   DHT VCC -> 3.3V\n");
        Serial.printf("   DHT GND -> GND\n");
        Serial.printf("   DHT DATA -> GPIO %d\n", DHT_PIN);
    }

    // // Initialize BLE
    // Serial.println("========================================");
    // Serial.println("Initializing BLE...");
    // BLEDevice::init(deviceName.c_str());

    // // Create the BLE Server
    // BLEServer *pServer = BLEDevice::createServer();
    // pServer->setCallbacks(new MyServerCallbacks());

    // // Create the BLE Service
    // BLEService *pService = pServer->createService(ENVIRONMENTAL_SERVICE_UUID);

    // // Create the Temperature/Humidity Characteristic
    // pTempHumidityCharacteristic = pService->createCharacteristic(
    //                                   TEMP_HUMIDITY_CHAR_UUID,
    //                                   BLECharacteristic::PROPERTY_READ |
    //                                   BLECharacteristic::PROPERTY_NOTIFY
    //                               );

    // // Add a descriptor for notifications
    // pTempHumidityCharacteristic->addDescriptor(new BLE2902());

    // // Start the service
    // pService->start();

    // // Start advertising
    // BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    // pAdvertising->addServiceUUID(ENVIRONMENTAL_SERVICE_UUID);
    // pAdvertising->setScanResponse(true);
    // pAdvertising->setMinPreferred(0x06);
    // pAdvertising->setMaxPreferred(0x12);
    // BLEDevice::startAdvertising();

    // Serial.println("========================================");
    // Serial.println("🌡️💧 BLE Environmental Sensor Ready!");
    // Serial.println("Waiting for nRF9160 to connect...");
    
    // // --- CHANGE 3: Corrected the log message to reflect the new data format ---
    // Serial.println("Data format: T:xx.x,H:yy.y");
    // Serial.println("========================================");
}

void loop() {
    // if (deviceConnected) {
        unsigned long currentTime = millis();
        if (currentTime - lastReadingTime >= READING_INTERVAL) {
            lastReadingTime = currentTime;

            // Read environmental data
            if (readEnvironmentalData()) {
                String dataString = formatSensorData();
                
                Serial.print("Environmental data: ");
                Serial.println(dataString);

                // // Set characteristic value and notify the central
                pTempHumidityCharacteristic->setValue(dataString.c_str());
                pTempHumidityCharacteristic->notify();

                Serial.print("Sent notification: ");
                Serial.println(dataString);
                
                // // Provide environmental context
                // if (sensorData.temperature > 30 && sensorData.humidity > 70) {
                //     Serial.println("🌿 Conditions: Hot and humid - good for tropical plants");
                // } else if (sensorData.temperature < 15 && sensorData.humidity < 40) {
                //     Serial.println("🌵 Conditions: Cool and dry - monitor plant watering");
                // } else if (sensorData.humidity > 80) {
                //     Serial.println("🍄 Conditions: Very humid - watch for mold/fungus");
                // } else if (sensorData.humidity < 30) {
                //     Serial.println("💨 Conditions: Very dry - plants may need more water");
                // }
                
            } else {
                Serial.println("Failed to read environmental data, skipping transmission.");
            }
        }
    // }
    
    // // Status LED blink when not connected
    // if (!deviceConnected) {
    //     static unsigned long lastBlink = 0;
    //     static bool ledState = false;
        
    //     if (millis() - lastBlink >= 1000) {
    //         ledState = !ledState;
    //         digitalWrite(STATUS_LED_PIN, ledState);
    //         lastBlink = millis();
    //     }
    // }
    
    delay(100);
}