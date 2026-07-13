// Minimal SigilDevice example, built by CI on every PR to verify the
// library compiles for ESP32/Arduino.
#include <Sigil.h>

#define RELAY_RX_PIN 9
#define RELAY_TX_PIN 10

SigilDevice device("example01", "sensor", "home_automation");

void handlePing(JsonObjectConst params) {
    device.sendReading("pong", true);
}

void setup() {
    device.addAttribute("serial", "EX-001");
    device.addCapability("ping", "Respond with a pong reading");
    device.onCommand("ping", handlePing);
    device.begin(Serial1, 115200, RELAY_RX_PIN, RELAY_TX_PIN);
}

void loop() {
    device.update();
}
