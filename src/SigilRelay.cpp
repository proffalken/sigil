#include "SigilRelay.h"

// ── Constructor ────────────────────────────────────────────────────────────

SigilRelay::SigilRelay(const char* relayId)
    : _relayId(relayId)
    , _rs485(nullptr)
    , _device(nullptr)
    , _attrs()
    , _debug(nullptr)
{}

// ── Public API ─────────────────────────────────────────────────────────────

void SigilRelay::addAttribute(const char* key, const char* value) {
    _attrs.add(key, value);
}

void SigilRelay::setDebugStream(Stream& stream) {
    _debug = &stream;
    _debugln("[sigil] debug enabled");
}

void SigilRelay::begin(HardwareSerial& rs485Serial,  uint32_t rs485Baud,  int rs485Rx,  int rs485Tx,
                       HardwareSerial& deviceSerial, uint32_t deviceBaud, int deviceRx, int deviceTx) {
    _rs485  = &rs485Serial;
    _device = &deviceSerial;

    _rs485->begin (rs485Baud,  SERIAL_8N1, rs485Rx,  rs485Tx);
    _device->begin(deviceBaud, SERIAL_8N1, deviceRx, deviceTx);

    // Flush any startup noise before the end device begins transmitting
    delay(100);
    while (_device->available()) _device->read();

    _debugln("[sigil] relay ready");
}

// String::indexOf() stops at embedded NUL bytes, which appear in bootloader
// noise from the end device. This helper scans byte-by-byte via operator[]
// so it is unaffected by nulls embedded in the string data.
static int findBrace(const String& s) {
    for (unsigned int i = 0; i < s.length(); i++) {
        if (s[i] == '{') return (int)i;
    }
    return -1;
}

void SigilRelay::update() {
    // --- Device → RS485 bus ---
    if (_device && _device->available()) {
        String raw = _device->readStringUntil('\n');
        _debugln("[sigil] rx from device: " + raw);

        int jsonStart = findBrace(raw);
        if (jsonStart != -1) {
            if (jsonStart > 0) raw = raw.substring(jsonStart);
            _forwardToRS485(raw);
        } else {
            _debugln("[sigil] no JSON found in device message");
        }
    }

    // --- RS485 bus → Device ---
    if (_rs485 && _rs485->available()) {
        String raw = _rs485->readStringUntil('\n');
        _debugln("[sigil] rx from RS485: " + raw);

        int jsonStart = findBrace(raw);
        if (jsonStart != -1) {
            if (jsonStart > 0) raw = raw.substring(jsonStart);
            _forwardToDevice(raw);
        } else {
            _debugln("[sigil] no JSON found in RS485 message");
        }
    }
}

// ── Private helpers ────────────────────────────────────────────────────────

void SigilRelay::_forwardToRS485(const String& json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);

    if (err) {
        _debugln("[sigil] JSON parse error: " + String(err.c_str()));
        _debugln("[sigil] failed input: " + json);
        return;
    }

    // Protocol fields at top level
    doc["relay_id"] = _relayId;
    doc["relay_ts"] = millis();

    // Merge relay attributes into the "attributes" object.
    // Any device attributes already present are preserved;
    // relay values win on key collision.
    _attrs.applyTo(doc);

    String output;
    serializeJson(doc, output);
    output += '\n';
    _rs485->print(output);
    _debugln("[sigil] forwarded to RS485: " + output);
}

void SigilRelay::_forwardToDevice(const String& json) {
    if (_device) {
        _device->print(json + '\n');
        _debugln("[sigil] forwarded to device: " + json);
    }
}

void SigilRelay::_debugln(const String& msg) {
    if (_debug) _debug->println(msg);
}
