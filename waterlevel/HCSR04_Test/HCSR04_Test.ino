/*
 * HC-SR04 Ultrasonic Sensor Test (ESP32)
 * 
 * Connections:
 * - VCC: 5V (or 3.3V depends on sensor version)
 * - GND: GND
 * - TRIG: GPIO 18
 * - ECHO: GPIO 19
 */

const int trigPin = 18;
const int echoPin = 19;

// Maximum distance we expect to read (in cm)
const int maxDistance = 400;

void setup() {
  Serial.begin(115200);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  
  Serial.println("--- HC-SR04 Ultrasonic Test Started ---");
  Serial.println("Trig Pin: 18, Echo Pin: 19");
}

void loop() {
  // Clear the trigPin
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  
  // Sets the trigPin on HIGH state for 10 micro seconds
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  // Reads the echoPin, returns the sound wave travel time in microseconds
  long duration = pulseIn(echoPin, HIGH, 30000); // 30ms timeout
  
  // Calculating the distance
  // Speed of sound is 0.034 cm/us
  float distanceCm = duration * 0.034 / 2;
  
  if (duration == 0 || distanceCm > maxDistance) {
    Serial.println("Error: Out of range or no sensor detected");
  } else {
    Serial.print("Distance: ");
    Serial.print(distanceCm);
    Serial.println(" cm");
  }
  
  delay(1000); // Wait 1 second before next reading
}
