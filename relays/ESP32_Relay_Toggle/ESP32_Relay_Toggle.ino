/*
 * ESP32 Simple Relay Control
 * 
 * This sketch toggles a relay on and off every 5 seconds.
 * 
 * Connections:
 * Relay VCC -> ESP32 5V (or 3.3V depending on relay module)
 * Relay GND -> ESP32 GND
 * Relay IN  -> ESP32 GPIO 26
 * 
 * Note: Most relay modules are "Active LOW," meaning:
 * digitalWrite(IR_PIN, LOW)  -> Relay turns ON
 * digitalWrite(IR_PIN, HIGH) -> Relay turns OFF
 */

#define IR_PIN 26
#define WT_PIN 27

void setup() {
  Serial.begin(115200);
  
  // Initialize the relay pin as an output
  pinMode(IR_PIN, OUTPUT);
  pinMode(WT_PIN, OUTPUT);
  
  // Set initial state (OFF for Active LOW relays)
  digitalWrite(IR_PIN, HIGH);
  digitalWrite(WT_PIN, HIGH);
  
  Serial.println("--- ESP32 Relay Control Initialized ---");
}

void loop() {
  Serial.println("Relay: ON");
  digitalWrite(IR_PIN, LOW);   // Turn relay ON (Active LOW)
  digitalWrite(WT_PIN, HIGH);   // Turn relay ON (Active LOW)
  delay(5000);                    // Wait 5 seconds
  
  Serial.println("Relay: OFF");
  digitalWrite(IR_PIN, HIGH);  // Turn relay OFF (Active LOW)
  digitalWrite(WT_PIN, LOW);  // Turn relay OFF (Active LOW)
  delay(5000);                    // Wait 5 seconds
}
