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
const unsigned long READING_INTERVAL = 300000; // Send data every 5 minutes (300,000 ms)

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
        // Advertising is now managed explicitly in the loop()
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
    // pinMode(DHT_PIN, INPUT_PULLUP); // REMOVED: Managed by DHT library internally.
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
    
    // Try to read a few times to ensure sensor is ready
    bool sensorReady = false;
    for (int i = 0; i < 3; i++) {
        if (readEnvironmentalData()) {
            sensorReady = true;
            break;
        }
        Serial.println("Retrying sensor read...");
        delay(2000);
    }

    if (sensorReady) {
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

    // Initialize BLE
    Serial.println("========================================");
    Serial.println("Initializing BLE...");
    BLEDevice::init(deviceName.c_str());

    // Create the BLE Server
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    // Create the BLE Service
    BLEService *pService = pServer->createService(ENVIRONMENTAL_SERVICE_UUID);

    // Create the Temperature/Humidity Characteristic
    pTempHumidityCharacteristic = pService->createCharacteristic(
                                      TEMP_HUMIDITY_CHAR_UUID,
                                      BLECharacteristic::PROPERTY_READ |
                                      BLECharacteristic::PROPERTY_NOTIFY
                                  );

    // Add a descriptor for notifications
    pTempHumidityCharacteristic->addDescriptor(new BLE2902());

    // Start the service
    pService->start();

    // Start advertising (Wait! We now start this only when data is ready in loop)
    // BLEDevice::startAdvertising();

    Serial.println("========================================");
    Serial.println("🌡️💧 BLE Environmental Sensor Ready (Radio Idle)");
    Serial.println("Waiting for nRF9160 to connect...");
    
    // --- CHANGE 3: Corrected the log message to reflect the new data format ---
    Serial.println("Data format: T:xx.x,H:yy.y");
    Serial.println("========================================");
}

void loop() {
    unsigned long currentTime = millis();
    static unsigned long connectedSince = 0;
    
    // 1. Periodically read the sensor (every 5 minutes)
    if (currentTime - lastReadingTime >= READING_INTERVAL || lastReadingTime == 0) {
        lastReadingTime = currentTime;
        
        Serial.println("\n--- Periodic Sensor Cycle Start ---");
        // Read environmental data with retry
        bool success = false;
        for (int i = 0; i < 3; i++) {
            if (readEnvironmentalData()) {
                success = true;
                break;
            }
            delay(2000);
        }
        
        if (success) {
            Serial.println("Sensor read successful. Starting radio (advertising)...");
            BLEDevice::startAdvertising(); // Radio ON
        } else {
            Serial.println("Failed to read sensor this cycle. Radio remains OFF.");
        }
    }

    // 2. Handle transmission and duty cycle completion
    // Wait at least 2 seconds after connection to ensure gateway has subscribed!
    if (deviceConnected) {
        if (connectedSince == 0) connectedSince = millis();
        
        if (sensorData.valid && (millis() - connectedSince >= 2000)) {
            String dataString = formatSensorData();
            
            Serial.print("Environmental data: ");
            Serial.println(dataString);

            // Set characteristic value
            pTempHumidityCharacteristic->setValue(dataString.c_str());
            
            // Reverted to compatible notify() call to fix compilation error
            pTempHumidityCharacteristic->notify();

            Serial.println("Sent notification. Closing connection and stopping radio in 5s...");
            delay(5000); // Increased delay to ensure gateway processes the data

            // Active Disconnect and Radio OFF
            BLEDevice::getServer()->disconnect(0); 
            BLEDevice::getAdvertising()->stop(); // Radio OFF
            
            deviceConnected = false; 
            connectedSince = 0;
            sensorData.valid = false; // Mark data as "sent"
            Serial.println("Cycle complete. Radio IDLE for 5 minutes.");
        }
    } else {
        connectedSince = 0; // Reset connection timer when disconnected
    }
    
    // Status LED blink when not connected (advertising)
    if (!deviceConnected) {
        static unsigned long lastBlink = 0;
        static bool ledState = false;
        
        if (millis() - lastBlink >= 1000) {
            ledState = !ledState;
            digitalWrite(STATUS_LED_PIN, ledState);
            lastBlink = millis();
        }
    }
    
    delay(10);
}