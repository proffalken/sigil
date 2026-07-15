#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <Stream.h>
#include "SigilAttributes.h"
#include "SigilHash.h"

// ── Limits (override before including this header if needed) ───────────────
#ifndef SIGIL_BUS_QUIET_MS
#define SIGIL_BUS_QUIET_MS 5
#endif
#ifndef SIGIL_BUS_JITTER_MAX_MS
#define SIGIL_BUS_JITTER_MAX_MS 20
#endif

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
// ── RS485 bus contention ─────────────────────────────────────────────────
//
// Multiple relays typically share one RS485 bus, so upstream writes are not
// sent immediately. Instead update() holds at most one outgoing message and
// writes it once the bus has been quiet for SIGIL_BUS_QUIET_MS *and* a
// per-relay jitter delay (a deterministic hash of relayId, up to
// SIGIL_BUS_JITTER_MAX_MS) has elapsed. This reduces the chance of two
// relays transmitting at the same instant — RS485 half-duplex has no true
// collision detection here, so it's avoidance, not elimination. A message
// waiting to go out simply delays reading the next one from the device;
// nothing is dropped.
//
// ── Optional OTel metrics ──────────────────────────────────────────────────
//
// Define SIGIL_OTEL_ENABLED in your build flags to enable OpenTelemetry
// metrics. When enabled, the relay:
//   • Connects to WiFi via WiFiManager (captive portal on first boot,
//     stored credentials on subsequent boots — no hardcoded passwords).
//   • Syncs time via NTP (required for accurate OTel timestamps).
//   • Emits the following metrics to your OTEL_COLLECTOR_BASE_URL every
//     60 seconds (configurable via setMetricsInterval()):
//       sigil.relay.messages.upstream    - device → RS485 (cumulative)
//       sigil.relay.messages.downstream  - RS485 → device (cumulative)
//       sigil.relay.messages.dropped     - JSON parse failures (cumulative)
//       sigil.relay.registrations        - register messages seen (cumulative)
//       sigil.relay.uptime_seconds       - time since begin() (gauge)
//       sigil.relay.last_message_age_ms  - ms since last upstream msg (gauge)
//
// Required extra lib_deps when SIGIL_OTEL_ENABLED is set:
//   tzapu/WiFiManager
//   https://github.com/proffalken/otel-embedded-cpp.git
//
// Required extra build_flags:
//   -D OTEL_COLLECTOR_BASE_URL='"http://your-collector:4318"'
//
// Usage:
//   HardwareSerial rs485(1);
//   HardwareSerial deviceSerial(2);
//   SigilRelay relay("relay01");
//
//   void setup() {
//     relay.addAttribute("location", "office");
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
    // When SIGIL_OTEL_ENABLED is defined, also starts WiFiManager and NTP.
    void begin(HardwareSerial& rs485Serial,   uint32_t rs485Baud,   int rs485Rx,  int rs485Tx,
               HardwareSerial& deviceSerial,  uint32_t deviceBaud,  int deviceRx, int deviceTx);

    // Optional: direct relay debug output to a Stream (e.g. Serial).
    // When set, logs every received message, parse errors, and forwarded
    // output so you can trace exactly what the relay is doing.
    // Call before begin().
    void setDebugStream(Stream& stream);

    // Call once per loop().
    void update();

#ifdef SIGIL_OTEL_ENABLED
    // Set how often OTel metrics are emitted (ms). Default: 60 000 (60 s).
    // Call before or after begin(). Pass 0 to disable periodic emission.
    void setMetricsInterval(uint32_t ms);
#endif

private:
    const char*     _relayId;
    HardwareSerial* _rs485;
    HardwareSerial* _device;
    SigilAttributes _attrs;
    Stream*         _debug;   // nullptr = debug off

    // Bus-contention avoidance — see class comment above.
    String        _pendingTx;         // serialized message awaiting bus access; empty = none pending
    unsigned long _pendingTxReadyMs;  // earliest millis() we may attempt to send _pendingTx
    unsigned long _lastRs485ActivityMs; // millis() bytes were last seen on _rs485
#ifdef SIGIL_OTEL_ENABLED
    bool _pendingIsRegister; // whether _pendingTx is a register message, for _registrationsSeen
#endif

    bool     _busClear(unsigned long now) const;
    uint32_t _jitterMs() const;
    void     _flushPendingTx();

    void _forwardToRS485(const String& json);
    void _forwardToDevice(const String& json);
    void _debugln(const String& msg);

    // Config message handling and NVS persistence.
    // A config message addressed to this relay's device_id is consumed here
    // and never forwarded to the end device.
    // Attributes are stored in NVS namespace "sigil_r" under key "attrs"
    // as a serialised JSON object, and reloaded on every boot.
    void _loadConfig();
    void _persistConfig();
    void _handleConfig(JsonDocument& doc);

#ifdef SIGIL_OTEL_ENABLED
    bool          _otelReady;          // true once WiFi + NTP + OTel are up
    uint32_t      _msgsUpstream;       // device → RS485, cumulative
    uint32_t      _msgsDownstream;     // RS485 → device, cumulative
    uint32_t      _msgsDropped;        // JSON parse failures, cumulative
    uint32_t      _registrationsSeen;  // register msg_type, cumulative
    unsigned long _lastMessageMs;      // millis() of last upstream message
    unsigned long _lastMetricsMs;      // millis() of last OTel emission
    uint32_t      _metricsIntervalMs;  // emission period (default 60 000)
    unsigned long _startMs;            // millis() captured in begin()

    void _initOtel();
    void _emitMetrics();
#endif
};
