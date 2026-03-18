/*
 * A02YYUW Ultrasonic Sensor for Arduino Nano Every
 * 
 * Connections:
 * Sensor VCC -> Nano Every 5V
 * Sensor GND -> Nano Every GND
 * Sensor TX  -> Nano Every Pin 0 (RX)
 * Sensor RX  -> Nano Every Pin 1 (TX) or Disconnected (for Auto-Reading)
 */

unsigned char data[4] = {0};
float distance = 0.0;
unsigned long lastPrintTime = 0;

void setup() {
  Serial.begin(115200);   // USB Debugging
  Serial1.begin(9600);    // hardware Serial on Pins 0 & 1
  
  while (!Serial); // Wait for USB Serial
  
  Serial.println("--- A02YYUW Nano Every Initialized ---");
}

void loop() {
  if (Serial1.available() >= 4) {
    if (Serial1.read() == 0xFF) {
      data[0] = 0xFF;
      data[1] = Serial1.read();
      data[2] = Serial1.read();
      data[3] = Serial1.read();

      // Checksum validation
      unsigned char sum = (data[0] + data[1] + data[2]) & 0xFF;
      
      if (sum == data[3]) {
        int distance_mm = (data[1] << 8) | data[2];
        distance = distance_mm / 10.0; // cm

        // Print distance every 500ms
        if (millis() - lastPrintTime >= 500) {
          lastPrintTime = millis();
          Serial.print("Distance: ");
          Serial.print(distance);
          Serial.println(" cm");
        }
      } else {
        Serial.println("Checksum Error");
      }
    }
  }
}
