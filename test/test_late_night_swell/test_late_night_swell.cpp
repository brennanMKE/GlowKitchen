// Native tests for the late-night swell in src/late_night_swell.h.
// Run with `pio test -e native`.
#include <unity.h>
#include <stdint.h>
#include <stdlib.h>
#include "late_night_swell.h"

void setUp(void) {}
void tearDown(void) {}

void test_swells_only_on_late_night_hours(void) {
    for (int hour = 0; hour < 24; hour++) {
        bool expected = hour >= 19;
        TEST_ASSERT_EQUAL_MESSAGE(expected, lateNightSwellHour(hour), "hour");
    }
}

void test_both_ends_return_to_the_configured_brightness(void) {
    TEST_ASSERT_EQUAL_UINT8(180, swellBrightness(180, 225, 0));
    TEST_ASSERT_EQUAL_UINT8(180, swellBrightness(180, 225, SWELL_MS));
    TEST_ASSERT_EQUAL_UINT8(180, swellBrightness(180, 225, SWELL_MS + 5000));
}

void test_reaches_the_peak_and_dips_low(void) {
    uint8_t hi = 0;
    uint8_t lo = 255;
    for (uint32_t t = SWELL_IN_MS; t < SWELL_MS - SWELL_OUT_MS; t += 5) {
        uint8_t b = swellBrightness(180, 225, t);
        if (b > hi) hi = b;
        if (b < lo) lo = b;
    }
    TEST_ASSERT_UINT8_WITHIN(2, 225, hi);
    TEST_ASSERT_UINT8_WITHIN(2, 180 / SWELL_DEPTH_DIVISOR, lo);
}

void test_dim_strip_never_goes_dark(void) {
    for (uint32_t t = 0; t < SWELL_MS; t += 5) {
        TEST_ASSERT_TRUE(swellBrightness(8, 225, t) >= SWELL_FLOOR);
        TEST_ASSERT_TRUE(swellBrightness(1, 225, t) >= 1);
    }
}

void test_dips_deep_enough_to_show_under_the_power_limit(void) {
    // The rise is flattened by the power limit, so the dip must carry the
    // swell on its own: well under half the configured level.
    uint8_t lo = 255;
    for (uint32_t t = SWELL_IN_MS; t < SWELL_MS - SWELL_OUT_MS; t += 5) {
        uint8_t b = swellBrightness(180, 225, t);
        if (b < lo) lo = b;
    }
    TEST_ASSERT_TRUE(lo < 180 / 4);
}

void test_never_exceeds_the_peak(void) {
    for (uint32_t t = 0; t < SWELL_MS; t += 5) {
        TEST_ASSERT_TRUE(swellBrightness(200, 225, t) <= 225);
    }
}

void test_never_steps(void) {
    // At a ~10ms frame interval no frame should jump visibly.
    uint8_t prev = swellBrightness(180, 225, 0);
    for (uint32_t t = 10; t <= SWELL_MS; t += 10) {
        uint8_t b = swellBrightness(180, 225, t);
        TEST_ASSERT_TRUE_MESSAGE(abs((int)b - (int)prev) <= 12, "brightness stepped");
        prev = b;
    }
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_swells_only_on_late_night_hours);
    RUN_TEST(test_both_ends_return_to_the_configured_brightness);
    RUN_TEST(test_reaches_the_peak_and_dips_low);
    RUN_TEST(test_dim_strip_never_goes_dark);
    RUN_TEST(test_dips_deep_enough_to_show_under_the_power_limit);
    RUN_TEST(test_never_exceeds_the_peak);
    RUN_TEST(test_never_steps);
    return UNITY_END();
}
