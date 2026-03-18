#include <WiFi.h>
#include <WiFiClient.h>
#include <HardwareSerial.h>
 
HardwareSerial Ultrasonic_Sensor(2); //   TX2 (pin 17), RX2 (pin 16)
// Define connections to sensor
int pinRX = 16;  // Choose a suitable pin for RX
int pinTX = 17;  // Choose a suitable pin for TX 
 
 
// Array to store incoming serial data
unsigned char data_buffer[4] = {0};
 
int distance;
 
// Variable to hold checksum
unsigned char CS;
 
 
void setup()
{
  Serial.begin(115200); // Initialize the serial monitor
  Ultrasonic_Sensor.begin(9600, SERIAL_8N1, pinRX, pinTX); // Initialize the hardware serial
 
}
 
void loop()
{
 
    // Run if data available
  if (Ultrasonic_Sensor.available() > 0) {
 
    delay(4);
 
    // Check for packet header character 0xff
    if (Ultrasonic_Sensor.read() == 0xff) {
      // Insert header into array
      data_buffer[0] = 0xff;
      // Read remaining 3 characters of data and insert into array
      for (int i = 1; i < 4; i++) {
        data_buffer[i] = Ultrasonic_Sensor.read();
      }
 
      //Compute checksum
      CS = data_buffer[0] + data_buffer[1] + data_buffer[2];
      // If checksum is valid compose distance from data
      if (data_buffer[3] == CS) {
        distance = (data_buffer[1] << 8) + data_buffer[2];
        // Print to serial monitor
     distance= distance / 10; // cm
 
      }
    }
  }
 
  Serial.print("Distance: ");
  Serial.println(distance);
}
