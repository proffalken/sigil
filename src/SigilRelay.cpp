#include "SigilRelay.h"

// ── Constructor ────────────────────────────────────────────────────────────

SigilRelay::SigilRelay(const char* relayId)
    : _relayId(relayId)
    , _rs485(nullptr)
    , _device(nullptr)
    , _attrs()
{}

// ── Public API ─────────────────────────────────────────────────────────────

void SigilRelay::addAttribute(const char* key, const char* value) {
    _attrs.add(key, value);
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
}

void SigilRelay::update() {
    // --- Device → RS485 bus ---
    if (_device && _device->available()) {
        String raw = _device->readStringUntil('\n');
        int jsonStart = raw.indexOf('{');
        if (jsonStart != -1) {
            if (jsonStart > 0) raw = raw.substring(jsonStart);
            _forwardToRS485(raw);
        }
    }

    // --- RS485 bus → Device ---
    if (_rs485 && _rs485->available()) {
        String raw = _rs485->readStringUntil('\n');
        int jsonStart = raw.indexOf('{');
        if (jsonStart != -1) {
            if (jsonStart > 0) raw = raw.substring(jsonStart);
            _forwardToDevice(raw);
        }
    }
}

// ── Private helpers ────────────────────────────────────────────────────────

void SigilRelay::_forwardToRS485(const String& json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return;

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
}

void SigilRelay::_forwardToDevice(const String& json) {
    if (_device) _device->print(json + '\n');
}
