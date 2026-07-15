#pragma once

#include <stdint.h>

// ── SigilHash ──────────────────────────────────────────────────────────────
//
// A tiny, dependency-free FNV-1a hash of a NUL-terminated C-string.
// Deliberately has no Arduino dependency (unlike the rest of src/) so it
// can be unit tested natively without pulling in hardware mocks.
//
// Used by SigilRelay to derive a deterministic per-relay jitter offset for
// RS485 bus-contention avoidance — see SigilRelay.h.

inline uint32_t sigilHash(const char* s) {
    uint32_t hash = 2166136261u;
    while (*s) {
        hash ^= (uint8_t)*s++;
        hash *= 16777619u;
    }
    return hash;
}
