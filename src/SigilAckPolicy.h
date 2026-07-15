#pragma once

#include <stdint.h>

// ── SigilAckPolicy ─────────────────────────────────────────────────────────
//
// Pure decision logic for SigilRelay's command ack/retry tracking. No
// Arduino dependency (unsigned long is a plain C++ type), so it's unit
// tested natively — same pattern as SigilHash.h.

enum class SigilAckAction { Wait, Retry, GiveUp };

inline SigilAckAction sigilAckAction(unsigned long now, unsigned long sentMs,
                                      uint8_t retries, unsigned long timeoutMs,
                                      uint8_t maxRetries) {
    if (now - sentMs < timeoutMs) return SigilAckAction::Wait;
    if (retries < maxRetries) return SigilAckAction::Retry;
    return SigilAckAction::GiveUp;
}
