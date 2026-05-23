#include "SigilDevice.h"
#include <string.h>

// ── Constructor ────────────────────────────────────────────────────────────

SigilDevice::SigilDevice(const char* deviceId,
                         const char* deviceType,
                         const char* systemName)
    : _deviceId(deviceId)
    , _deviceType(deviceType)
    , _systemName(systemName)
    , _serial(nullptr)
    , _registered(false)
    , _capCount(0)
    , _handlerCount(0)
{}

// ── Public API ─────────────────────────────────────────────────────────────

void SigilDevice::addCapability(const char* name,
                                const char* description,
                                std::initializer_list<const char*> params) {
    if (_capCount >= SIGIL_MAX_CAPABILITIES) return;

    SigilCapability& cap = _capabilities[_capCount++];

    strncpy(cap.name,        name,        sizeof(cap.name)        - 1);
    strncpy(cap.description, description, sizeof(cap.description) - 1);
    cap.name       [sizeof(cap.name)        - 1] = '\0';
    cap.description[sizeof(cap.description) - 1] = '\0';

    cap.paramCount = 0;
    for (const char* p : params) {
        if (cap.paramCount >= SIGIL_MAX_PARAMS) break;
        strncpy(cap.params[cap.paramCount], p, sizeof(cap.params[0]) - 1);
        cap.params[cap.paramCount][sizeof(cap.params[0]) - 1] = '\0';
        cap.paramCount++;
    }
}

void SigilDevice::onCommand(const char* name, void (*handler)(JsonObjectConst)) {
    if (_handlerCount >= SIGIL_MAX_HANDLERS) return;
    strncpy(_handlers[_handlerCount].name, name, sizeof(_handlers[0].name) - 1);
    _handlers[_handlerCount].name[sizeof(_handlers[0].name) - 1] = '\0';
    _handlers[_handlerCount].fn = handler;
    _handlerCount++;
}

void SigilDevice::begin(HardwareSerial& serial,
                        uint32_t baud,
                        int rxPin,
                        int txPin,
                        uint32_t bootDelayMs) {
    _serial = &serial;
    _serial->begin(baud, SERIAL_8N1, rxPin, txPin);
    delay(bootDelayMs);   // give the relay time to start up before we register
}

void SigilDevice::update() {
    // Send registration exactly once, on the first call after begin()
    if (!_registered) {
        _sendRegister();
        _registered = true;
    }

    // Poll for incoming messages (commands, acks) from the relay
    while (_serial && _serial->available()) {
        String raw = _serial->readStringUntil('\n');
        int jsonStart = raw.indexOf('{');
        if (jsonStart != -1) {
            if (jsonStart > 0) raw = raw.substring(jsonStart);
            _handleMessage(raw);
        }
    }
}

// ── Private helpers ────────────────────────────────────────────────────────

void SigilDevice::_sendRegister() {
    JsonDocument doc;
    doc["msg_type"]    = "register";
    doc["device_id"]   = _deviceId;
    doc["device_type"] = _deviceType;
    doc["system_name"] = _systemName;

    JsonArray caps = doc["capabilities"].to<JsonArray>();
    for (uint8_t i = 0; i < _capCount; i++) {
        JsonObject cap  = caps.add<JsonObject>();
        cap["name"]        = _capabilities[i].name;
        cap["description"] = _capabilities[i].description;
        JsonArray paramsArr = cap["params"].to<JsonArray>();
        for (uint8_t j = 0; j < _capabilities[i].paramCount; j++) {
            paramsArr.add(_capabilities[i].params[j]);
        }
    }

    _sendJson(doc);
}

void SigilDevice::_handleMessage(const String& json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return;

    const char* msgType = doc["msg_type"] | "";

    if (strcmp(msgType, "command") == 0) {
        // Only respond to commands addressed to our system_name and device_id
        const char* sysName  = doc["system_name"] | "";
        const char* deviceId = doc["device_id"]   | "";

        if (strcmp(sysName,  _systemName) != 0) return;
        if (strcmp(deviceId, _deviceId)   != 0) return;

        const char*     command = doc["command"] | "";
        JsonObjectConst params  = doc["params"].as<JsonObjectConst>();

        for (uint8_t i = 0; i < _handlerCount; i++) {
            if (strcmp(_handlers[i].name, command) == 0) {
                _handlers[i].fn(params);
                return;
            }
        }
        // Unrecognised command — silently ignored
    }
    // ack and all other msg_types are silently ignored
}

void SigilDevice::_sendJson(JsonDocument& doc) {
    if (!_serial) return;
    String output;
    serializeJson(doc, output);
    output += '\n';
    _serial->print(output);
}
