#pragma once

#include <ArduinoJson.h>
#include "SigilAttributes.h"

// ── SigilEventMessage ──────────────────────────────────────────────────────
//
// Builds the JSON shape for a Sigil "event" message. Pulled out of
// SigilDevice::sendEvent() so the exact wire shape (field placement, value
// serialization, attribute merging) can be unit tested natively — a real
// HardwareSerial can't be constructed or captured under ArduinoFake (see
// test/test_event_message), so anything that needs a live serial link stays
// untestable there, but the doc-building step here has no such dependency.

template <typename T>
void sigilBuildEventDoc(JsonDocument& doc,
                         const char* deviceId,
                         const char* systemName,
                         const char* eventName,
                         T value,
                         const SigilAttributes& attrs) {
    doc["msg_type"]    = "event";
    doc["device_id"]   = deviceId;
    doc["system_name"] = systemName;
    doc["event"]       = eventName;
    doc["value"]       = value;
    attrs.applyTo(doc);
}

// Overload for a bare event with no associated value, e.g. "cycle_started".
inline void sigilBuildEventDoc(JsonDocument& doc,
                                const char* deviceId,
                                const char* systemName,
                                const char* eventName,
                                const SigilAttributes& attrs) {
    doc["msg_type"]    = "event";
    doc["device_id"]   = deviceId;
    doc["system_name"] = systemName;
    doc["event"]       = eventName;
    attrs.applyTo(doc);
}
