/*
 * A02YYUW Ultrasonic Sensor for ESP32-C3 SuperMini
 * 
 * Connections:
 * Sensor VCC -> C3 SuperMini 5V
 * Sensor GND -> C3 SuperMini GND
 * Sensor TX  -> C3 SuperMini GPIO 4 (RX)
 * Sensor RX  -> Disconnected (For Auto-Reading)
 */

#define RX_PIN 4
#define TX_PIN -1 // Not used

unsigned char data[4] = {0};
float distance = 0.0;
unsigned long lastPrintTime = 0;

void setup() {
  // ESP32-C3 SuperMini uses internal USB for Serial
  Serial.begin(115200);
  
  // Initialize Hardware Serial 1
  // Pins: RX=4, TX=-1, Baud=9600
  Serial1.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  
  Serial.println("--- ESP32-C3 SuperMini A02YYUW Initialized ---");
}

void loop() {
  // Check if a full frame is available (4 bytes)
  if (Serial1.available() >= 4) {
    // Look for the header 0xFF
    if (Serial1.read() == 0xFF) {
      data[0] = 0xFF;
      data[1] = Serial1.read();
      data[2] = Serial1.read();
      data[3] = Serial1.read();

      // Checksum validation: (byte0 + byte1 + byte2) & 0xFF
      unsigned char sum = (data[0] + data[1] + data[2]) & 0xFF;
      
      if (sum == data[3]) {
        int distance_mm = (data[1] << 8) | data[2];
        distance = distance_mm / 10.0; // cm

        // Print frequency control
        if (millis() - lastPrintTime >= 1000) {
          lastPrintTime = millis();
          Serial.print("Distance: ");
          Serial.print(distance);
          Serial.println(" cm");
        }
      } else {
        // Serial.println("Checksum Error");
      }
    }
  }
}
