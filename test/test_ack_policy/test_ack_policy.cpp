#include <unity.h>
#include "SigilAckPolicy.h"

// SigilAckPolicy.h has no Arduino dependency, so these tests exercise the
// exact decision function SigilRelay::_checkAckRetry() uses, without
// needing SigilRelay itself (HardwareSerial/Preferences coupling — see
// SigilRelay.cpp).

static const unsigned long kTimeout    = 2000;
static const uint8_t       kMaxRetries = 3;

void setUp(void) {}
void tearDown(void) {}

void test_waits_before_timeout(void) {
    SigilAckAction action = sigilAckAction(/*now=*/1000, /*sentMs=*/0, /*retries=*/0,
                                            kTimeout, kMaxRetries);
    TEST_ASSERT_EQUAL(SigilAckAction::Wait, action);
}

void test_retries_after_timeout_with_retries_remaining(void) {
    SigilAckAction action = sigilAckAction(/*now=*/2000, /*sentMs=*/0, /*retries=*/0,
                                            kTimeout, kMaxRetries);
    TEST_ASSERT_EQUAL(SigilAckAction::Retry, action);
}

void test_retries_until_max_reached(void) {
    SigilAckAction action = sigilAckAction(/*now=*/2000, /*sentMs=*/0, /*retries=*/2,
                                            kTimeout, kMaxRetries);
    TEST_ASSERT_EQUAL(SigilAckAction::Retry, action);
}

void test_gives_up_once_retries_exhausted(void) {
    SigilAckAction action = sigilAckAction(/*now=*/2000, /*sentMs=*/0, /*retries=*/3,
                                            kTimeout, kMaxRetries);
    TEST_ASSERT_EQUAL(SigilAckAction::GiveUp, action);
}

void test_boundary_exactly_at_timeout_is_not_wait(void) {
    // now - sentMs == timeoutMs should act, not wait — the check is
    // `< timeoutMs`, so the boundary itself is due.
    SigilAckAction action = sigilAckAction(/*now=*/2000, /*sentMs=*/0, /*retries=*/0,
                                            kTimeout, kMaxRetries);
    TEST_ASSERT_NOT_EQUAL(SigilAckAction::Wait, action);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_waits_before_timeout);
    RUN_TEST(test_retries_after_timeout_with_retries_remaining);
    RUN_TEST(test_retries_until_max_reached);
    RUN_TEST(test_gives_up_once_retries_exhausted);
    RUN_TEST(test_boundary_exactly_at_timeout_is_not_wait);
    return UNITY_END();
}
