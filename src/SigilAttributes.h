#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>

// ── Limits ─────────────────────────────────────────────────────────────────
#ifndef SIGIL_MAX_ATTRIBUTES
#define SIGIL_MAX_ATTRIBUTES 16
#endif

// ── SigilAttributes ────────────────────────────────────────────────────────
//
// Stores a flat list of freeform key/value string attributes, inspired by
// the OpenTelemetry attributes model.
//
// Used by both SigilDevice and SigilRelay. When applied to a JsonDocument,
// attributes are written into a nested "attributes" object. If one already
// exists (e.g. device attributes carried through to the relay), the relay's
// attributes are merged in — relay values win on key collision.
//
class SigilAttributes {
public:
    SigilAttributes() : _count(0) {}

    // Add or update a freeform attribute.
    // If the key already exists its value is updated in place.
    // New keys are appended, silently capped at SIGIL_MAX_ATTRIBUTES.
    void add(const char* key, const char* value) {
        // Update in place if key already present
        for (uint8_t i = 0; i < _count; i++) {
            if (strncmp(_entries[i].key, key, sizeof(_entries[0].key)) == 0) {
                strncpy(_entries[i].value, value, sizeof(_entries[0].value) - 1);
                _entries[i].value[sizeof(_entries[0].value) - 1] = '\0';
                return;
            }
        }
        // New key — append
        if (_count >= SIGIL_MAX_ATTRIBUTES) return;
        strncpy(_entries[_count].key,   key,   sizeof(_entries[0].key)   - 1);
        strncpy(_entries[_count].value, value, sizeof(_entries[0].value) - 1);
        _entries[_count].key  [sizeof(_entries[0].key)   - 1] = '\0';
        _entries[_count].value[sizeof(_entries[0].value) - 1] = '\0';
        _count++;
    }

    // Merge all stored attributes into doc["attributes"].
    // Creates the nested object if it doesn't exist yet.
    // Existing keys are overwritten (caller wins).
    void applyTo(JsonDocument& doc) const {
        if (_count == 0) return;

        JsonObject attrs;
        if (doc["attributes"].is<JsonObject>()) {
            attrs = doc["attributes"].as<JsonObject>();
        } else {
            attrs = doc["attributes"].to<JsonObject>();
        }

        for (uint8_t i = 0; i < _count; i++) {
            attrs[_entries[i].key] = _entries[i].value;
        }
    }

    // Remove an attribute by key. No-op if the key is not present.
    void remove(const char* key) {
        for (uint8_t i = 0; i < _count; i++) {
            if (strncmp(_entries[i].key, key, sizeof(_entries[0].key)) == 0) {
                // Shift remaining entries down to fill the gap
                for (uint8_t j = i; j < _count - 1; j++) {
                    _entries[j] = _entries[j + 1];
                }
                _count--;
                return;
            }
        }
    }

    uint8_t count() const { return _count; }

private:
    struct Entry {
        char key  [32];
        char value[64];
    };

    Entry   _entries[SIGIL_MAX_ATTRIBUTES];
    uint8_t _count;
};
