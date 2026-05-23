#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>

// ── SigilRelay ─────────────────────────────────────────────────────────────
//
// Bridges a single end device (connected via pogo pins) to the RS485 bus.
// Augments every upstream message with location, relay_id, and relay_ts.
// Passes downstream messages (commands, acks) through to the device unchanged.
//
// The relay is deliberately namespace-agnostic: it forwards everything and
// lets end devices filter for themselves. This means any device from any
// system_name can be plugged into any relay.
//
// Usage:
//   HardwareSerial rs485(1);
//   HardwareSerial deviceSerial(2);
//   SigilRelay relay("relay01", "office");
//
//   void setup() {
//     Serial.begin(115200);
//     relay.begin(rs485,       115200, 22, 21,
//                 deviceSerial, 115200, 16, 17);
//   }
//
//   void loop() {
//     relay.update();
//   }
//
class SigilRelay {
public:
    // relayId  : unique identifier for this relay on the network
    // location : human-readable physical location (e.g. "office", "kitchen")
    SigilRelay(const char* relayId, const char* location);

    // Initialise both UARTs.
    //   rs485Serial / rs485Baud / rs485Rx / rs485Tx  : RS485 bus connection
    //   deviceSerial / deviceBaud / deviceRx / deviceTx : end-device pogo-pin connection
    void begin(HardwareSerial& rs485Serial,   uint32_t rs485Baud,   int rs485Rx,  int rs485Tx,
               HardwareSerial& deviceSerial,  uint32_t deviceBaud,  int deviceRx, int deviceTx);

    // Call once per loop().
    void update();

private:
    const char*     _relayId;
    const char*     _location;
    HardwareSerial* _rs485;
    HardwareSerial* _device;

    void _forwardToRS485(const String& json);
    void _forwardToDevice(const String& json);
};
