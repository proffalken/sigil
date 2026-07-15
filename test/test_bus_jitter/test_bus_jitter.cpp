#include <unity.h>
#include "SigilHash.h"

// SigilHash.h has no Arduino dependency, so these tests exercise the exact
// function SigilRelay::_jitterMs() uses (sigilHash(_relayId) % (max + 1))
// without needing SigilRelay itself, which isn't natively testable (see
// SigilRelay.cpp's HardwareSerial/Preferences coupling).

static const uint32_t kJitterMax = 20; // mirrors SIGIL_BUS_JITTER_MAX_MS default

void setUp(void) {}
void tearDown(void) {}

void test_hash_is_deterministic(void) {
    TEST_ASSERT_EQUAL_UINT32(sigilHash("relay01"), sigilHash("relay01"));
}

void test_hash_differs_for_different_ids(void) {
    // Spot check, not a universal guarantee — hash collisions are possible
    // in principle, just rare for a handful of distinct relay ids.
    TEST_ASSERT_NOT_EQUAL(sigilHash("relay01"), sigilHash("relay02"));
    TEST_ASSERT_NOT_EQUAL(sigilHash("office-relay"), sigilHash("workshop-relay"));
}

void test_jitter_is_bounded(void) {
    const char* ids[] = {"relay01", "relay02", "office-relay", "workshop-relay", ""};
    for (const char* id : ids) {
        uint32_t jitter = sigilHash(id) % (kJitterMax + 1);
        TEST_ASSERT_TRUE(jitter <= kJitterMax);
    }
}

void test_jitter_is_stable_per_relay(void) {
    uint32_t jitterA = sigilHash("relay01") % (kJitterMax + 1);
    uint32_t jitterB = sigilHash("relay01") % (kJitterMax + 1);
    TEST_ASSERT_EQUAL_UINT32(jitterA, jitterB);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_hash_is_deterministic);
    RUN_TEST(test_hash_differs_for_different_ids);
    RUN_TEST(test_jitter_is_bounded);
    RUN_TEST(test_jitter_is_stable_per_relay);
    return UNITY_END();
}
