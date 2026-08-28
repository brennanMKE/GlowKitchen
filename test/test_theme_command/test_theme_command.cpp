// Issue #0021: broadcast theme commands must not cancel a running custom
// effect, while device-targeted ones still must.
//
// Every case below is derived from the arithmetic of the rule -- the two
// inputs crossed against each other -- rather than from a transcript of the
// bug. #0008 (rounds 2-3), #0014 (round 1) and #0017 (round 1) each shipped a
// green suite that could not fail because its fixtures were written to look
// like the symptom instead.

#include <unity.h>
#include "theme_command.h"

void setUp(void) {}
void tearDown(void) {}

// The exact topic that carried the retained FOREST which cancelled effects.
static const char* BROADCAST = "lights/all/cmd";
static const char* DEVICE    = "lights/84fce68774a4/cmd";
static const char* NAMED     = "lights/kitchen/cmd";

// ---- The rule itself: both inputs must be true to ignore ----------------

void test_broadcast_ignored_while_effect_active(void) {
    TEST_ASSERT_TRUE(shouldIgnoreBroadcastTheme(BROADCAST, true));
}

void test_broadcast_obeyed_when_no_effect_active(void) {
    // Normal fleet control must keep working -- this is the case that would
    // break every existing HA automation if the rule were unconditional.
    TEST_ASSERT_FALSE(shouldIgnoreBroadcastTheme(BROADCAST, false));
}

void test_device_topic_obeyed_while_effect_active(void) {
    // Deliberately taking control of one strip still wins. If this ever
    // returns true, a person has no way to override a running effect except
    // CLEAR_EFFECT.
    TEST_ASSERT_FALSE(shouldIgnoreBroadcastTheme(DEVICE, true));
}

void test_named_device_topic_obeyed_while_effect_active(void) {
    TEST_ASSERT_FALSE(shouldIgnoreBroadcastTheme(NAMED, true));
}

void test_device_topic_obeyed_when_no_effect_active(void) {
    TEST_ASSERT_FALSE(shouldIgnoreBroadcastTheme(DEVICE, false));
}

// ---- Substring hazards --------------------------------------------------
// The check is a substring search for "/all/", so anything that merely
// contains those letters, or contains "all" without the slashes, must not be
// mistaken for the broadcast topic.

void test_device_named_all_something_is_not_a_broadcast(void) {
    // A device literally named "allotment" -- "all" appears, "/all/" does not.
    TEST_ASSERT_FALSE(shouldIgnoreBroadcastTheme("lights/allotment/cmd", true));
}

void test_device_name_ending_in_all_is_not_a_broadcast(void) {
    TEST_ASSERT_FALSE(shouldIgnoreBroadcastTheme("lights/hall/cmd", true));
}

void test_wall_is_not_a_broadcast(void) {
    TEST_ASSERT_FALSE(shouldIgnoreBroadcastTheme("lights/wall/cmd", true));
}

// ---- Degenerate input ---------------------------------------------------

void test_null_topic_is_not_ignored(void) {
    // A null topic must not crash and must not silently swallow a command.
    TEST_ASSERT_FALSE(shouldIgnoreBroadcastTheme(nullptr, true));
    TEST_ASSERT_FALSE(shouldIgnoreBroadcastTheme(nullptr, false));
}

void test_empty_topic_is_not_ignored(void) {
    TEST_ASSERT_FALSE(shouldIgnoreBroadcastTheme("", true));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_broadcast_ignored_while_effect_active);
    RUN_TEST(test_broadcast_obeyed_when_no_effect_active);
    RUN_TEST(test_device_topic_obeyed_while_effect_active);
    RUN_TEST(test_named_device_topic_obeyed_while_effect_active);
    RUN_TEST(test_device_topic_obeyed_when_no_effect_active);
    RUN_TEST(test_device_named_all_something_is_not_a_broadcast);
    RUN_TEST(test_device_name_ending_in_all_is_not_a_broadcast);
    RUN_TEST(test_wall_is_not_a_broadcast);
    RUN_TEST(test_null_topic_is_not_ignored);
    RUN_TEST(test_empty_topic_is_not_ignored);
    return UNITY_END();
}
