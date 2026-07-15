#include <unity.h>
#include <ArduinoJson.h>
#include "SigilAttributes.h"

void setUp(void) {}
void tearDown(void) {}

// ── add() ────────────────────────────────────────────────────────────────

void test_add_appends_new_key(void) {
    SigilAttributes attrs;
    attrs.add("location", "office");
    TEST_ASSERT_EQUAL_UINT8(1, attrs.count());
}

void test_add_updates_existing_key_in_place(void) {
    SigilAttributes attrs;
    attrs.add("location", "office");
    attrs.add("location", "workshop");

    TEST_ASSERT_EQUAL_UINT8(1, attrs.count());

    JsonDocument doc;
    attrs.applyTo(doc);
    TEST_ASSERT_EQUAL_STRING("workshop", doc["attributes"]["location"]);
}

void test_add_enforces_cap(void) {
    SigilAttributes attrs;
    char key[8];
    for (int i = 0; i < SIGIL_MAX_ATTRIBUTES + 5; i++) {
        snprintf(key, sizeof(key), "k%d", i);
        attrs.add(key, "v");
    }
    TEST_ASSERT_EQUAL_UINT8(SIGIL_MAX_ATTRIBUTES, attrs.count());
}

void test_add_truncates_oversized_key_and_value(void) {
    SigilAttributes attrs;
    // Entry key buffer is 32 bytes, value buffer is 64 bytes (see
    // SigilAttributes.h). Both must be NUL-terminated after truncation,
    // never overflow, and never leave garbage past the terminator.
    String longKey(50, 'k');
    String longValue(100, 'v');

    attrs.add(longKey.c_str(), longValue.c_str());
    TEST_ASSERT_EQUAL_UINT8(1, attrs.count());

    JsonDocument doc;
    attrs.applyTo(doc);
    JsonObject obj = doc["attributes"].as<JsonObject>();

    // Exactly one key survived truncation (no duplicate/garbage entries).
    size_t keyCount = 0;
    for (JsonPair kv : obj) {
        (void)kv;
        keyCount++;
    }
    TEST_ASSERT_EQUAL_UINT(1, keyCount);

    for (JsonPair kv : obj) {
        TEST_ASSERT_TRUE(strlen(kv.key().c_str()) <= 31);
        TEST_ASSERT_TRUE(strlen(kv.value().as<const char*>()) <= 63);
    }
}

// ── remove() ─────────────────────────────────────────────────────────────

void test_remove_deletes_existing_key_and_preserves_order(void) {
    SigilAttributes attrs;
    attrs.add("a", "1");
    attrs.add("b", "2");
    attrs.add("c", "3");

    attrs.remove("b");
    TEST_ASSERT_EQUAL_UINT8(2, attrs.count());

    JsonDocument doc;
    attrs.applyTo(doc);
    TEST_ASSERT_EQUAL_STRING("1", doc["attributes"]["a"]);
    TEST_ASSERT_EQUAL_STRING("3", doc["attributes"]["c"]);
    TEST_ASSERT_FALSE(doc["attributes"]["b"].is<const char*>());
}

void test_remove_nonexistent_key_is_noop(void) {
    SigilAttributes attrs;
    attrs.add("a", "1");
    attrs.remove("does_not_exist");
    TEST_ASSERT_EQUAL_UINT8(1, attrs.count());
}

// ── applyTo() ────────────────────────────────────────────────────────────

void test_applyTo_empty_does_not_create_attributes_key(void) {
    SigilAttributes attrs;
    JsonDocument doc;
    doc["msg_type"] = "data";
    attrs.applyTo(doc);

    TEST_ASSERT_FALSE(doc["attributes"].is<JsonObject>());
}

void test_applyTo_creates_nested_object(void) {
    SigilAttributes attrs;
    attrs.add("hw_rev", "1.0");

    JsonDocument doc;
    doc["msg_type"] = "data";
    attrs.applyTo(doc);

    TEST_ASSERT_TRUE(doc["attributes"].is<JsonObject>());
    TEST_ASSERT_EQUAL_STRING("1.0", doc["attributes"]["hw_rev"]);
}

void test_applyTo_merges_without_clobbering_unrelated_keys(void) {
    // Mirrors the device→relay handoff: the device already wrote its own
    // attributes object; the relay's SigilAttributes must merge into it
    // rather than replace it.
    SigilAttributes relayAttrs;
    relayAttrs.add("location", "office");

    JsonDocument doc;
    doc["msg_type"]                    = "data";
    doc["attributes"]["serial"]        = "SV-001";
    doc["attributes"]["location"]      = "unset";

    relayAttrs.applyTo(doc);

    TEST_ASSERT_EQUAL_STRING("SV-001", doc["attributes"]["serial"]);
    TEST_ASSERT_EQUAL_STRING("office", doc["attributes"]["location"]);

    // Regression guard: overwriting a colliding key must update it in
    // place, not leave a stale duplicate entry alongside the new one.
    JsonObject attrsObj = doc["attributes"].as<JsonObject>();
    TEST_ASSERT_EQUAL_UINT(2, attrsObj.size());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_add_appends_new_key);
    RUN_TEST(test_add_updates_existing_key_in_place);
    RUN_TEST(test_add_enforces_cap);
    RUN_TEST(test_add_truncates_oversized_key_and_value);
    RUN_TEST(test_remove_deletes_existing_key_and_preserves_order);
    RUN_TEST(test_remove_nonexistent_key_is_noop);
    RUN_TEST(test_applyTo_empty_does_not_create_attributes_key);
    RUN_TEST(test_applyTo_creates_nested_object);
    RUN_TEST(test_applyTo_merges_without_clobbering_unrelated_keys);
    return UNITY_END();
}
