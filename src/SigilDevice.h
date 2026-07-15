#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <Stream.h>
#include <initializer_list>
#include "SigilAttributes.h"
#include "SigilEventMessage.h"

// ── Limits (override before including this header if needed) ───────────────
#ifndef SIGIL_MAX_CAPABILITIES
#define SIGIL_MAX_CAPABILITIES  8
#endif
#ifndef SIGIL_MAX_PARAMS
#define SIGIL_MAX_PARAMS        8
#endif
#ifndef SIGIL_MAX_HANDLERS
#define SIGIL_MAX_HANDLERS      8
#endif

// ── Internal storage types ─────────────────────────────────────────────────

struct SigilCapability {
    char    name[32];
    char    description[64];
    char    params[SIGIL_MAX_PARAMS][32];
    uint8_t paramCount;
};

struct SigilHandler {
    char name[32];
    void (*fn)(JsonObjectConst params);
};

// ── SigilDevice ────────────────────────────────────────────────────────────
//
// Represents a single end device on the Sigil network.
//
// Usage:
//   SigilDevice device("servo01", "actuator", "home_automation");
//
//   void setup() {
//     device.addAttribute("serial", "SV-001");
//     device.addCapability("set_angle", "Set servo angle", {"channel", "angle", "speed"});
//     device.onCommand("set_angle", handleSetAngle);
//     device.begin(Serial1, 115200, RX_PIN, TX_PIN);
//   }
//
//   void loop() {
//     device.update();
//     device.sendReading("position", myServo.read());
//   }
//
class SigilDevice {
public:
    // deviceId   : unique name for this device on the network (e.g. "servo01")
    // deviceType : "sensor", "actuator", or "sensor_actuator"
    // systemName : namespace — device only responds to commands matching this
    SigilDevice(const char* deviceId,
                const char* deviceType,
                const char* systemName);

    // Add a freeform attribute (key/value) that will be included in all
    // outgoing messages under the "attributes" object.
    // Call before begin(). Silently capped at SIGIL_MAX_ATTRIBUTES.
    void addAttribute(const char* key, const char* value);
    void removeAttribute(const char* key);

    // Declare a capability this device advertises on registration.
    // Call before begin(). Silently capped at SIGIL_MAX_CAPABILITIES.
    void addCapability(const char* name,
                       const char* description,
                       std::initializer_list<const char*> params = {});

    // Register a handler for an incoming command by name.
    // Silently capped at SIGIL_MAX_HANDLERS.
    void onCommand(const char* name, void (*handler)(JsonObjectConst params));

    // Initialise the serial link to the relay.
    // bootDelayMs: pause before sending the first register message.
    // 2000 ms is the default — long enough to clear ESP32 bootloader noise.
    void begin(HardwareSerial& serial,
               uint32_t baud,
               int rxPin,
               int txPin,
               uint32_t bootDelayMs = 2000);

    // Set how often the device re-sends its registration message (ms).
    // Default: 30000 (30 s). Set to 0 to disable periodic re-registration.
    // Call before or after begin().
    void setRegisterInterval(uint32_t ms);

    // Optional: direct device debug output to a Stream (e.g. Serial).
    // When set, logs every sent/received message, parse errors, and
    // command dispatch so you can trace exactly what the device is doing.
    // Call before begin().
    void setDebugStream(Stream& stream);

    // Call once per loop(). Sends register on first call (and periodically
    // thereafter), then polls for incoming commands/acks from the relay.
    void update();

    // Send a single sensor reading as a data message.
    // T may be any type ArduinoJson can serialise (int, float, bool, etc.)
    // Device attributes are included automatically under "attributes".
    template <typename T>
    void sendReading(const char* name, T value) {
        JsonDocument doc;
        doc["msg_type"]    = "data";
        doc["device_id"]   = _deviceId;
        doc["system_name"] = _systemName;
        _attrs.applyTo(doc);
        JsonArray readings = doc["readings"].to<JsonArray>();
        JsonObject r       = readings.add<JsonObject>();
        r["name"]          = name;
        r["value"]         = value;
        _sendJson(doc);
    }

    // Send a self-describing event as a single atomic message
    // (msg_type: "event") — a discrete "this happened, here's the one fact
    // about it" notification, distinct from a continuous sendReading()
    // stream. Device attributes are included automatically under
    // "attributes". T may be any type ArduinoJson can serialise.
    template <typename T>
    void sendEvent(const char* eventName, T value) {
        JsonDocument doc;
        sigilBuildEventDoc(doc, _deviceId, _systemName, eventName, value, _attrs);
        _sendJson(doc);
    }

    // Bare event with no associated value, e.g. sendEvent("cycle_started").
    void sendEvent(const char* eventName) {
        JsonDocument doc;
        sigilBuildEventDoc(doc, _deviceId, _systemName, eventName, _attrs);
        _sendJson(doc);
    }

private:
    const char*      _deviceId;
    const char*      _deviceType;
    const char*      _systemName;
    HardwareSerial*  _serial;
    bool             _registered;
    unsigned long    _lastRegisterMs;    // millis() when we last sent a register
    uint32_t         _registerIntervalMs;// 0 = no periodic re-registration
    Stream*          _debug;             // nullptr = debug off

    SigilAttributes  _attrs;

    SigilCapability  _capabilities[SIGIL_MAX_CAPABILITIES];
    uint8_t          _capCount;

    SigilHandler     _handlers[SIGIL_MAX_HANDLERS];
    uint8_t          _handlerCount;

    void _sendRegister();
    void _handleMessage(const String& json);
    void _sendJson(JsonDocument& doc);
    void _sendAck(const char* command, const char* status);
    void _debugln(const String& msg);

    // Config message handling and NVS persistence.
    // A config message addressed to this device_id is consumed here.
    // Attributes are stored in NVS namespace "sigil_d" under key "attrs"
    // as a serialised JSON object, and reloaded on every boot.
    void _loadConfig();
    void _persistConfig();
};
