#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <Stream.h>
#include "SigilAttributes.h"

// ── SigilRelay ─────────────────────────────────────────────────────────────
//
// Bridges a single end device (connected via pogo pins) to the RS485 bus.
// Augments every upstream message with relay_id, relay_ts, and any
// attributes added via addAttribute().
//
// Relay attributes are merged into the "attributes" object alongside any
// device attributes already in the message. On key collision, the relay
// value wins.
//
// The relay is deliberately namespace-agnostic: it forwards everything and
// lets end devices filter for themselves.
//
// Usage:
//   HardwareSerial rs485(1);
//   HardwareSerial deviceSerial(2);
//   SigilRelay relay("relay01");
//
//   void setup() {
//     relay.addAttribute("location", "office");
//     relay.addAttribute("area", "desk");
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
    // relayId: unique identifier for this relay on the network.
    // Location and other metadata are set via addAttribute().
    explicit SigilRelay(const char* relayId);

    // Add a freeform attribute (key/value) that will be merged into the
    // "attributes" object of every message forwarded upstream.
    // Call before begin(). Silently capped at SIGIL_MAX_ATTRIBUTES.
    void addAttribute(const char* key, const char* value);

    // Initialise both UARTs.
    //   rs485Serial / rs485Baud / rs485Rx / rs485Tx  : RS485 bus connection
    //   deviceSerial / deviceBaud / deviceRx / deviceTx : end-device pogo-pin connection
    void begin(HardwareSerial& rs485Serial,   uint32_t rs485Baud,   int rs485Rx,  int rs485Tx,
               HardwareSerial& deviceSerial,  uint32_t deviceBaud,  int deviceRx, int deviceTx);

    // Optional: direct relay debug output to a Stream (e.g. Serial).
    // When set, logs every received message, parse errors, and forwarded
    // output so you can trace exactly what the relay is doing.
    // Call before begin().
    void setDebugStream(Stream& stream);

    // Call once per loop().
    void update();

private:
    const char*     _relayId;
    HardwareSerial* _rs485;
    HardwareSerial* _device;
    SigilAttributes _attrs;
    Stream*         _debug;   // nullptr = debug off

    void _forwardToRS485(const String& json);
    void _forwardToDevice(const String& json);
    void _debugln(const String& msg);
};
