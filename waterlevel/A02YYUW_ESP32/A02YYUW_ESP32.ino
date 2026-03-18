#include <HardwareSerial.h>

#define RXD2 16   // Sensor TX -> GPIO16

HardwareSerial SensorSerial(2);

void setup() {
  Serial.begin(115200);
  SensorSerial.begin(9600, SERIAL_8N1, RXD2, -1);

  Serial.println("A02YYUW Test Starting...");
}

void loop() {
  if (SensorSerial.available() >= 4) {

    // Look for header
    if (SensorSerial.read() == 0xFF) {

      int high = SensorSerial.read();
      int low = SensorSerial.read();
      int checksum = SensorSerial.read();

      // Validate checksum
      int sum = (0xFF + high + low) & 0xFF;

      if (sum == checksum) {
        int distance = (high << 8) | low;
        float cm = distance / 10.0;

        Serial.print("Distance: ");
        Serial.print(cm);
        Serial.println(" cm");
      } else {
        Serial.println("Checksum error");
      }
    }
  }
}