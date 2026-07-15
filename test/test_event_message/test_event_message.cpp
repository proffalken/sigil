#include <unity.h>
#include <ArduinoJson.h>
#include "SigilAttributes.h"
#include "SigilEventMessage.h"

void setUp(void) {}
void tearDown(void) {}

// ── envelope + value shape ──────────────────────────────────────────────

void test_event_sets_msg_type_and_envelope(void) {
    SigilAttributes attrs;
    JsonDocument doc;
    sigilBuildEventDoc(doc, "galton01", "puzzle_room", "balls_dropped", 25, attrs);

    TEST_ASSERT_EQUAL_STRING("event",        doc["msg_type"]);
    TEST_ASSERT_EQUAL_STRING("galton01",     doc["device_id"]);
    TEST_ASSERT_EQUAL_STRING("puzzle_room",  doc["system_name"]);
    TEST_ASSERT_EQUAL_STRING("balls_dropped", doc["event"]);
}

void test_event_value_serializes_int(void) {
    SigilAttributes attrs;
    JsonDocument doc;
    sigilBuildEventDoc(doc, "galton01", "puzzle_room", "balls_dropped", 25, attrs);

    TEST_ASSERT_TRUE(doc["value"].is<int>());
    TEST_ASSERT_EQUAL_INT(25, doc["value"].as<int>());
}

void test_event_value_serializes_float(void) {
    SigilAttributes attrs;
    JsonDocument doc;
    sigilBuildEventDoc(doc, "galton01", "puzzle_room", "hopper_speed", 3.5f, attrs);

    TEST_ASSERT_TRUE(doc["value"].is<float>());
    TEST_ASSERT_EQUAL_FLOAT(3.5f, doc["value"].as<float>());
}

void test_event_value_serializes_bool(void) {
    SigilAttributes attrs;
    JsonDocument doc;
    sigilBuildEventDoc(doc, "galton01", "puzzle_room", "jam_detected", true, attrs);

    TEST_ASSERT_TRUE(doc["value"].is<bool>());
    TEST_ASSERT_TRUE(doc["value"].as<bool>());
}

void test_event_value_serializes_string(void) {
    SigilAttributes attrs;
    JsonDocument doc;
    sigilBuildEventDoc(doc, "galton01", "puzzle_room", "cycle_complete", "0142310", attrs);

    TEST_ASSERT_TRUE(doc["value"].is<const char*>());
    TEST_ASSERT_EQUAL_STRING("0142310", doc["value"].as<const char*>());
}

// ── bare event (no value) ────────────────────────────────────────────────

void test_bare_event_has_no_value_key(void) {
    SigilAttributes attrs;
    JsonDocument doc;
    sigilBuildEventDoc(doc, "galton01", "puzzle_room", "cycle_started", attrs);

    TEST_ASSERT_EQUAL_STRING("event",         doc["msg_type"]);
    TEST_ASSERT_EQUAL_STRING("cycle_started", doc["event"]);
    TEST_ASSERT_FALSE(doc["value"].is<int>());
    TEST_ASSERT_FALSE(doc["value"].is<const char*>());
}

// ── attribute merging ─────────────────────────────────────────────────────

void test_event_merges_attributes(void) {
    SigilAttributes attrs;
    attrs.add("hw_rev", "1.0");

    JsonDocument doc;
    sigilBuildEventDoc(doc, "galton01", "puzzle_room", "balls_dropped", 25, attrs);

    TEST_ASSERT_TRUE(doc["attributes"].is<JsonObject>());
    TEST_ASSERT_EQUAL_STRING("1.0", doc["attributes"]["hw_rev"]);
}

void test_event_without_attributes_omits_attributes_key(void) {
    SigilAttributes attrs;
    JsonDocument doc;
    sigilBuildEventDoc(doc, "galton01", "puzzle_room", "balls_dropped", 25, attrs);

    TEST_ASSERT_FALSE(doc["attributes"].is<JsonObject>());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_event_sets_msg_type_and_envelope);
    RUN_TEST(test_event_value_serializes_int);
    RUN_TEST(test_event_value_serializes_float);
    RUN_TEST(test_event_value_serializes_bool);
    RUN_TEST(test_event_value_serializes_string);
    RUN_TEST(test_bare_event_has_no_value_key);
    RUN_TEST(test_event_merges_attributes);
    RUN_TEST(test_event_without_attributes_omits_attributes_key);
    return UNITY_END();
}
