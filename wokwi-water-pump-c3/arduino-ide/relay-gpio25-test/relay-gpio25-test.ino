// ESP32 DevKit V1 raw relay test.
// DISCONNECT ALL 230/240 V WIRING before running this test.
// Wiring: GPIO25 -> IN, 5V -> VCC, GND -> GND.

static const int RELAY_PIN = 25;

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  Serial.println("GPIO25 raw relay test started; mains must be disconnected.");
}

void loop() {
  digitalWrite(RELAY_PIN, LOW);
  delay(20);
  Serial.printf("GPIO25 commanded LOW, readback=%d (hold 3 seconds)\n", digitalRead(RELAY_PIN));
  delay(3000);

  digitalWrite(RELAY_PIN, HIGH);
  delay(20);
  Serial.printf("GPIO25 commanded HIGH, readback=%d (hold 3 seconds)\n", digitalRead(RELAY_PIN));
  delay(3000);
}
