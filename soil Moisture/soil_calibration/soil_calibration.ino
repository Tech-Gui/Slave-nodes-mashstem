/*
 * Simple Soil Moisture Calibration Utility
 * 
 * Instructions:
 * 1. Open Serial Monitor at 115200 baud.
 * 2. Wipe your sensor completely clean and hold it in dry air. Note the "Raw ADC" average - this is your DRY_VALUE.
 * 3. Submerge it up to the MAX line in a glass of water. Note the "Raw ADC" average - this is your WET_VALUE.
 * 4. Update the values in your main program with the numbers you measured here.
 */

#define SOIL_SENSOR_PIN 34  // Analog data pin
#define POWER_PIN 4         // Power toggle pin
const unsigned long POWER_ON_DELAY = 100;

void setup() {
    Serial.begin(115200);
    Serial.println("=== Soil Sensor Calibration Mode ===");
    
    pinMode(SOIL_SENSOR_PIN, INPUT);
    pinMode(POWER_PIN, OUTPUT);
    digitalWrite(POWER_PIN, LOW); // Start powered off
}

void loop() {
    // 1. Power on the sensor to read
    digitalWrite(POWER_PIN, HIGH);
    delay(POWER_ON_DELAY);
    
    // 2. Take 10 rapid readings for better noise reduction
    int totalReading = 0;
    const int numReadings = 10;
    
    for (int i = 0; i < numReadings; i++) {
        totalReading += analogRead(SOIL_SENSOR_PIN);
        delay(10);
    }
    
    int avgReading = totalReading / numReadings;
    
    // 3. Power it back off to prevent electrolysis/corrosion
    digitalWrite(POWER_PIN, LOW);

    // 4. Print the raw results!
    Serial.print("🌱 Raw ADC Value: ");
    Serial.print(avgReading);
    
    // Estimate based on the current defaults just for reference
    float currentEstimate = map(avgReading, 3200, 1300, 0, 100);
    Serial.print("  | Current Code Est: ");
    Serial.print(currentEstimate, 1);
    Serial.println("%");

    // Wait 1 second before testing again
    delay(1000);
}
