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
        Serial.println("Gateway Connected");
    }

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        digitalWrite(STATUS_LED_PIN, LOW);
        Serial.println("Gateway Disconnected");
        BLEDevice::startAdvertising();
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
    BLEDevice::startAdvertising();

    Serial.println("BLE Ready. Payload: T:xx.x,H:yy.y,W:zz.z");
}

void loop() {
    unsigned long currentTime = millis();
    
    if (currentTime - lastReadingTime >= reportIntervalMs || lastReadingTime == 0) {
        lastReadingTime = currentTime;
        
        if (readSensors()) {
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "T:%.1f,H:%.1f,W:%.1f", 
                     sensorData.envValid ? sensorData.temperature : 0.0f,
                     sensorData.envValid ? sensorData.humidity : 0.0f,
                     sensorData.waterValid ? sensorData.waterLevel : 0.0f);
            
            pDataChar->setValue(buffer);
            if (deviceConnected) {
                pDataChar->notify();
                Serial.printf("Sent: %s\n", buffer);
            }
        }
    }

    // Status blink
    if (!deviceConnected) {
        static unsigned long lastBlink = 0;
        if (millis() - lastBlink >= 1000) {
            digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
            lastBlink = millis();
        }
    }
    
    delay(10);
}
