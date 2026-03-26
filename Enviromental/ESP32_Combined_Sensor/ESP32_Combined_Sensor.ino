/*
 * ESP32 Combined Sensor Node (Environmental + Water Level)
 *
 * This device measures:
 * 1. Temperature and Humidity (DHT11 on GPIO 4)
 * 2. Water Level (HC-SR04 on Trig: GPIO 18, Echo: GPIO 19)
 *
 * Reporting interval is customizable via BLE (default 1 min).
 * Data format: "T:xx.x,H:yy.y,W:zz.z"
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <DHT.h>
#include <Preferences.h>

// Hardware Configuration
#define DHT_PIN 4
#define DHT_TYPE DHT11
#define TRIG_PIN 18
#define ECHO_PIN 19
#define STATUS_LED_PIN 2

// BLE Configuration
#define SENSOR_SERVICE_UUID           "0000181B-0000-1000-8000-00805f9b34fb"
#define DATA_CHARACTERISTIC_UUID      "00002A1F-0000-1000-8000-00805f9b34fb"
#define CONFIG_CHARACTERISTIC_UUID    "00002A3D-0000-1000-8000-00805f9b34fb" // Custom for interval

// Sensor Objects
DHT dht(DHT_PIN, DHT_TYPE);
Preferences preferences;

// System Variables
BLECharacteristic* pDataChar = nullptr;
BLECharacteristic* pConfigChar = nullptr;
bool deviceConnected = false;
bool dataSent = false;
unsigned long lastReadingTime = 0;
uint32_t reportIntervalMs = 60000; // Default 1 minute

// Sensor readings
struct {
    float temperature;
    float humidity;
    float waterLevel;
    bool envValid;
    bool waterValid;
} sensorData = {0.0f, 0.0f, 0.0f, false, false};

// Forward Declarations
bool readSensors();
void updateInterval(uint32_t newIntervalSec);

// BLE Server Callbacks
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        digitalWrite(STATUS_LED_PIN, HIGH);
        Serial.println("========================================");
        Serial.println("Central Gateway Connected");
        Serial.println("========================================");
    }

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        digitalWrite(STATUS_LED_PIN, LOW);
        Serial.println("Central Gateway Disconnected");
        // Restart advertising only if data transmission wasn't completed
        if (!dataSent) {
            delay(500); // Brief delay for BLE stack to settle
            BLEDevice::startAdvertising();
            Serial.println("Advertising restarted for reconnection");
        }
    }
};

// Config characteristic callbacks
class ConfigCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pChar) {
        String value = pChar->getValue();
        if (value.length() > 0) {
            uint32_t intervalSec = 0;
            if (value.length() == 4) {
                // If it's a raw 4-byte uint32 payload
                memcpy(&intervalSec, value.c_str(), 4);
            } else {
                // If it's a numeric string
                intervalSec = (uint32_t)value.toInt();
            }

            if (intervalSec >= 10 && intervalSec <= 3600) { // 10s to 1h
                updateInterval(intervalSec);
                Serial.printf("New interval set: %u seconds\n", intervalSec);
            }
        }
    }
};

void updateInterval(uint32_t intervalSec) {
    reportIntervalMs = intervalSec * 1000;
    preferences.begin("sensor-config", false);
    preferences.putUInt("interval", intervalSec);
    preferences.end();
}

bool readSensors() {
    // 1. Read DHT11
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (isnan(t) || isnan(h)) {
        Serial.println("DHT Read Failed");
        sensorData.envValid = false;
    } else {
        sensorData.temperature = t;
        sensorData.humidity = h;
        sensorData.envValid = true;
    }

    // 2. Read HC-SR04
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout
    if (duration == 0) {
        Serial.println("HC-SR04 Read Failed");
        sensorData.waterValid = false;
    } else {
        sensorData.waterLevel = (duration * 0.0343) / 2;
        sensorData.waterValid = (sensorData.waterLevel <= 400.0f);
    }

    Serial.printf("Readings: T=%.1f C, H=%.1f%%, W=%.1f cm\n", 
                  sensorData.temperature, sensorData.humidity, sensorData.waterLevel);
    return sensorData.envValid || sensorData.waterValid;
}

void setup() {
    Serial.begin(115200);
    Serial.println("ESP32 Combined Sensor Node Starting");

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    dht.begin();

    // Load interval from preferences
    preferences.begin("sensor-config", true);
    uint32_t savedInterval = preferences.getUInt("interval", 60); // Default 60s
    reportIntervalMs = savedInterval * 1000;
    preferences.end();
    Serial.printf("Reporting interval: %d seconds\n", savedInterval);

    BLEDevice::init("ESP32_Combined_Sensor");
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SENSOR_SERVICE_UUID);

    pDataChar = pService->createCharacteristic(
                    DATA_CHARACTERISTIC_UUID,
                    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
                );
    pDataChar->addDescriptor(new BLE2902());

    pConfigChar = pService->createCharacteristic(
                      CONFIG_CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
                  );
    pConfigChar->setCallbacks(new ConfigCallbacks());
    
    // Set initial config value
    pConfigChar->setValue((uint8_t*)&savedInterval, 4);

    pService->start();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SENSOR_SERVICE_UUID);
    // BLEDevice::startAdvertising(); // Start only when data is ready in loop

    Serial.println("BLE Ready. Waiting for connection...");
}

void loop() {
    unsigned long currentTime = millis();
    static unsigned long connectedSince = 0;
    static unsigned long lastAdvertiseRestart = 0;

    // 1. Periodically read the sensor
    if (currentTime - lastReadingTime >= reportIntervalMs || lastReadingTime == 0) {
        lastReadingTime = currentTime;
        
        Serial.println("Cycle start");
        if (readSensors()) {
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
        
        // Wait 2 seconds (like soil moisture) to let GATT discoveries finish
        if (!dataSent && (millis() - connectedSince >= 2000)) {
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "T:%.1f,H:%.1f,W:%.1f", 
                     sensorData.envValid ? sensorData.temperature : 0.0f,
                     sensorData.envValid ? sensorData.humidity : 0.0f,
                     sensorData.waterValid ? sensorData.waterLevel : 0.0f);
            
            Serial.print("Sending: ");
            Serial.println(buffer);

            pDataChar->setValue(buffer);
            pDataChar->notify();

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

    // 3. Reconnection watchdog: if disconnected for > 30s, restart advertising
    // Only kick in if we are supposed to be advertising (data hasn't been sent yet)
    if (!deviceConnected && !dataSent) {
        if (currentTime - lastAdvertiseRestart >= 30000) {
            lastAdvertiseRestart = currentTime;
            Serial.println("Watchdog: Restarting BLE advertising...");
            BLEDevice::startAdvertising();
        }
    } else {
        lastAdvertiseRestart = currentTime; // Reset timer while connected or strictly idle
    }

    // Status blink
    if (!deviceConnected && !dataSent) {
        static unsigned long lastBlink = 0;
        static bool ledState = false;
        if (millis() - lastBlink >= 1000) {
            ledState = !ledState;
            digitalWrite(STATUS_LED_PIN, ledState);
            lastBlink = millis();
        }
    } else if (dataSent) {
        digitalWrite(STATUS_LED_PIN, LOW); // Ensure LED is off during IDLE
    }
    
    delay(10);
}
