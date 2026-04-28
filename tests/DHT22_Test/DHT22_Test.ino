/*
 * DHT22/AM2302 "Comfort Sense" Test Project
 * ----------------------------------------
 * This is a standalone diagnostic and monitoring project for the 
 * CMU ASAIR ORIG DHT22/AM2302 sensor.
 * 
 * Features:
 *  - Temperature & Humidity readings every 2 seconds.
 *  - Heat Index (Feels Like) calculation.
 *  - Comfort Level assessment (Too Cold, Comfortable, Too Hot, Humid).
 *  - Serial Plotter support.
 *  - Hardware health check with LED status.
 *
 * Wiring (DHT22 Front View - Pins left to right):
 *  - Pin 1 (VCC):  3.3V or 5V (3.3V recommended for ESP32)
 *  - Pin 2 (DATA): GPIO 4 (includes 10k Pull-up if using bare sensor)
 *  - Pin 3 (NC):   Not Connected
 *  - Pin 4 (GND):  GND
 */

#include "DHT.h"

// Configuration
#define DHTPIN 4           // Digital pin connected to the DHT sensor
#define DHTTYPE DHT22      // DHT 22 (AM2302), AM2321
#define LED_PIN 2          // ESP32 Build-in LED

DHT dht(DHTPIN, DHTTYPE);

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n===================================="));
  Serial.println(F("     DHT22 Comfort Sense Test      "));
  Serial.println(F("====================================\n"));

  pinMode(LED_PIN, OUTPUT);
  
  dht.begin();
  Serial.println(F("DHT22 Sensor initialized..."));
}

void loop() {
  // Wait a few seconds between measurements (DHT22 needs ~2s)
  delay(2000);

  // Reading temperature or humidity takes about 250 milliseconds!
  float h = dht.readHumidity();
  // Read temperature as Celsius (the default)
  float t = dht.readTemperature();

  // Check if any reads failed and exit early (to try again).
  if (isnan(h) || isnan(t)) {
    Serial.println(F("[-] Failed to read from DHT sensor!"));
    digitalWrite(LED_PIN, LOW); // LED OFF on error
    return;
  }

  // Success indicator blink
  digitalWrite(LED_PIN, HIGH);
  delay(50);
  digitalWrite(LED_PIN, LOW);

  // Compute heat index in Celsius (isFahreheit = false)
  float hic = dht.computeHeatIndex(t, h, false);

  // --- Output for Human Readers ---
  Serial.print(F("[+] Humidity: "));
  Serial.print(h);
  Serial.print(F("%  |  Temperature: "));
  Serial.print(t);
  Serial.print(F("°C  |  Feels Like: "));
  Serial.print(hic);
  Serial.println(F("°C"));

  // --- Comfort Assessment ---
  Serial.print(F("    Status: "));
  if (t < 18) Serial.print(F("CHILLY ❄️"));
  else if (t > 28) Serial.print(F("HOT ☀️"));
  else if (h > 70) Serial.print(F("MUGGY/HUMID ⛱️"));
  else if (h < 30) Serial.print(F("DRY 🌵"));
  else Serial.print(F("COMFORTABLE ✅"));
  Serial.println(F("\n------------------------------------"));

  // --- Output for Serial Plotter (Optional) ---
  // To use: Tools > Serial Plotter
  // Serial.print(t); Serial.print(","); Serial.print(h); Serial.print(","); Serial.println(hic);
}
