#include <Arduino.h>

void setup() {
  pinMode(2, OUTPUT);
}

void loop() {
  digitalWrite(2, HIGH);
  delay(200);  // Changed from 1000 to 200
  digitalWrite(2, LOW);
  delay(200);  // Faster blinking!
}