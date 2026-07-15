#include "SigilDevice.h"
#include "SigilAttributes.h"
#include <string.h>
#include <Preferences.h>

// ── Constructor ────────────────────────────────────────────────────────────

SigilDevice::SigilDevice(const char* deviceId,
                         const char* deviceType,
                         const char* systemName)
    : _deviceId(deviceId)
    , _deviceType(deviceType)
    , _systemName(systemName)
    , _serial(nullptr)
    , _registered(false)
    , _lastRegisterMs(0)
    , _registerIntervalMs(30000)
    , _debug(nullptr)
    , _capCount(0)
    , _handlerCount(0)
    , _attrs()
{}

// ── Public API ─────────────────────────────────────────────────────────────

void SigilDevice::addAttribute(const char* key, const char* value) {
    _attrs.add(key, value);
}

void SigilDevice::removeAttribute(const char* key) {
    _attrs.remove(key);
}

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

    // Load any previously persisted attributes (location, area, etc.)
    _loadConfig();

    delay(bootDelayMs);   // wait for bootloader noise to clear before registering

    _debugln("[sigil] device ready");
}

void SigilDevice::setRegisterInterval(uint32_t ms) {
    _registerIntervalMs = ms;
}

void SigilDevice::setDebugStream(Stream& stream) {
    _debug = &stream;
    _debugln("[sigil] debug enabled");
}

// String::indexOf() stops at embedded NUL bytes, which appear in bootloader
// noise. This helper scans byte-by-byte so it is unaffected by embedded nulls.
static int findBrace(const String& s) {
    for (unsigned int i = 0; i < s.length(); i++) {
        if (s[i] == '{') return (int)i;
    }
    return -1;
}

void SigilDevice::update() {
    unsigned long now = millis();

    // Send registration on first call, then periodically thereafter so that
    // a late-starting console or a relay reboot will still pick us up.
    bool due = !_registered
               || (_registerIntervalMs > 0
                   && (now - _lastRegisterMs) >= _registerIntervalMs);
    if (due) {
        _sendRegister();
        _registered     = true;
        _lastRegisterMs = now;
    }

    // Poll for incoming messages (commands, acks) from the relay
    while (_serial && _serial->available()) {
        String raw = _serial->readStringUntil('\n');
        _debugln("[sigil] rx from relay: " + raw);

        int jsonStart = findBrace(raw);
        if (jsonStart != -1) {
            if (jsonStart > 0) raw = raw.substring(jsonStart);
            _handleMessage(raw);
        } else {
            _debugln("[sigil] no JSON found in message");
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
    _attrs.applyTo(doc);

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
    if (err) {
        _debugln("[sigil] JSON parse error: " + String(err.c_str()));
        _debugln("[sigil] failed input: " + json);
        return;
    }

    const char* msgType = doc["msg_type"] | "";

    if (strcmp(msgType, "config") == 0) {
        // Only apply config addressed to this device
        const char* deviceId = doc["device_id"] | "";
        if (strcmp(deviceId, _deviceId) != 0) return;

        JsonObjectConst incoming = doc["attributes"].as<JsonObjectConst>();
        if (!incoming.isNull()) {
            for (JsonPairConst kv : incoming) {
                _attrs.add(kv.key().c_str(), kv.value().as<const char*>());
            }
            _persistConfig();
            _debugln("[sigil] config applied and persisted");
        }
        return;
    }

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
                _debugln("[sigil] command dispatched: " + String(command));
                _handlers[i].fn(params);
                _sendAck(command, "ok");
                return;
            }
        }
        // Unrecognised command — nack'd upstream, not just logged, so the
        // sender knows definitively rather than the command vanishing.
        _debugln("[sigil] unrecognized command: " + String(command));
        _sendAck(command, "unrecognized");
    }
    // ack and all other msg_types are silently ignored
}

void SigilDevice::_sendJson(JsonDocument& doc) {
    if (!_serial) return;
    String output;
    serializeJson(doc, output);
    output += '\n';
    _serial->print(output);
    _debugln("[sigil] sent: " + output);
}

void SigilDevice::_sendAck(const char* command, const char* status) {
    JsonDocument doc;
    doc["msg_type"]    = "ack";
    doc["device_id"]   = _deviceId;
    doc["system_name"] = _systemName;
    doc["command"]     = command;
    doc["status"]      = status;
    _sendJson(doc);
}

void SigilDevice::_debugln(const String& msg) {
    if (_debug) _debug->println(msg);
}

// ── Config messages and NVS persistence ───────────────────────────────────

void SigilDevice::_loadConfig() {
    Preferences prefs;
    prefs.begin("sigil_d", /*readOnly=*/true);
    String json = prefs.getString("attrs", "{}");
    prefs.end();

    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    for (JsonPairConst kv : doc.as<JsonObjectConst>()) {
        _attrs.add(kv.key().c_str(), kv.value().as<const char*>());
    }
    _debugln("[sigil] loaded persisted config");
}

void SigilDevice::_persistConfig() {
    JsonDocument tmp;
    _attrs.applyTo(tmp);                         // writes into tmp["attributes"]
    String json;
    serializeJson(tmp["attributes"], json);

    Preferences prefs;
    prefs.begin("sigil_d", /*readOnly=*/false);
    prefs.putString("attrs", json);
    prefs.end();
}
