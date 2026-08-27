// Native (host-compiled) tests for src/effect_parse.h (issue #0016): the
// hand-rolled SET_EFFECT payload parser and the NVS load decision. Run with
// `pio test -e native`.
//
// Sibling directory to test/test_effects/, not a second .cpp inside it: all
// .cpp files within one PlatformIO test subdirectory link into one binary,
// and src/themes.h's THEME_NAMES[] (before #0014's round-2 fix) had exactly
// this failure mode -- external linkage causing a duplicate-symbol link
// error the moment a second TU in the same binary includes it. A sibling
// directory sidesteps the question entirely and keeps a parser regression
// from being confused with a renderer regression.
//
// Include-order contract (transitively inherited via effect_parse.h ->
// effects.h -> themes.h): fastled_shim.h MUST come first, exactly as
// test/test_effects/test_effects.cpp does. Do not move or edit
// fastled_shim.h -- #0014's harness depends on it and must keep passing
// untouched.
#include <unity.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "../test_effects/fastled_shim.h"
#include "effects.h"
#include "effect_parse.h"

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------

static bool crgbEq(const CRGB& a, uint8_t r, uint8_t g, uint8_t b) {
    return a.r == r && a.g == g && a.b == b;
}

static bool configsEqual(const CustomEffectConfig& a, const CustomEffectConfig& b) {
    if (a.mode != b.mode) return false;
    if (a.colorCount != b.colorCount) return false;
    for (uint8_t i = 0; i < MAX_CUSTOM_COLORS; i++) {
        if (!(a.colors[i] == b.colors[i])) return false;
    }
    if (a.speed != b.speed) return false;
    if (a.intensity != b.intensity) return false;
    if (a.timeoutMs != b.timeoutMs) return false;
    if (a.activatedAt != b.activatedAt) return false;
    if (a.revertTheme != b.revertTheme) return false;
    return true;
}

static CustomEffectConfig fullyPopulatedSeed() {
    CustomEffectConfig c;
    c.mode = EFFECT_STROBE;
    for (uint8_t i = 0; i < MAX_CUSTOM_COLORS; i++) c.colors[i] = CRGB(10, 20, 30 + i);
    c.colorCount = 4;
    c.speed = 77;
    c.intensity = 200;
    c.timeoutMs = 5000;
    c.activatedAt = 12345;
    c.revertTheme = THEME_SUNSET;
    return c;
}

// ---------------------------------------------------------------------
// A. Parser -- parseEffectPayload()
// ---------------------------------------------------------------------

// 1
void test_case01_minimal_one_color(void) {
    CustomEffectConfig out;
    EffectParseResult r = parseEffectPayload("{\"mode\":\"CHASE\",\"colors\":[\"#FF6600\"]}", out);
    TEST_ASSERT_EQUAL(EFFECT_PARSE_OK, r);
    TEST_ASSERT_EQUAL(EFFECT_CHASE, out.mode);
    TEST_ASSERT_EQUAL_UINT8(1, out.colorCount);
    TEST_ASSERT_TRUE(crgbEq(out.colors[0], 0xFF, 0x66, 0x00));
    TEST_ASSERT_EQUAL_UINT8(128, out.speed);
    TEST_ASSERT_EQUAL_UINT8(128, out.intensity);
    TEST_ASSERT_EQUAL_UINT32(0, out.timeoutMs);
}

// 2 -- every field individually, 8 colors, COLORLOOP
void test_case02_full_eight_colors(void) {
    const char* payload =
        "{\"mode\":\"COLORLOOP\",\"colors\":[\"#000000\",\"#111111\",\"#222222\",\"#333333\","
        "\"#444444\",\"#555555\",\"#666666\",\"#777777\"],\"speed\":10,\"intensity\":20,\"timeout\":30}";
    CustomEffectConfig out;
    EffectParseResult r = parseEffectPayload(payload, out);
    TEST_ASSERT_EQUAL(EFFECT_PARSE_OK, r);
    TEST_ASSERT_EQUAL(EFFECT_COLORLOOP, out.mode);
    TEST_ASSERT_EQUAL_UINT8(8, out.colorCount);
    TEST_ASSERT_TRUE(crgbEq(out.colors[0], 0x00, 0x00, 0x00));
    TEST_ASSERT_TRUE(crgbEq(out.colors[7], 0x77, 0x77, 0x77));
    TEST_ASSERT_EQUAL_UINT8(10, out.speed);
    TEST_ASSERT_EQUAL_UINT8(20, out.intensity);
    TEST_ASSERT_EQUAL_UINT32(30000, out.timeoutMs);
}

// 3 -- same as case 2, keys in reverse order -> identical struct
void test_case03_key_order_irrelevant(void) {
    const char* payload =
        "{\"mode\":\"COLORLOOP\",\"colors\":[\"#000000\",\"#111111\",\"#222222\",\"#333333\","
        "\"#444444\",\"#555555\",\"#666666\",\"#777777\"],\"speed\":10,\"intensity\":20,\"timeout\":30}";
    const char* reversed =
        "{\"timeout\":30,\"intensity\":20,\"speed\":10,\"colors\":[\"#000000\",\"#111111\",\"#222222\","
        "\"#333333\",\"#444444\",\"#555555\",\"#666666\",\"#777777\"],\"mode\":\"COLORLOOP\"}";
    CustomEffectConfig a, b;
    TEST_ASSERT_EQUAL(EFFECT_PARSE_OK, parseEffectPayload(payload, a));
    TEST_ASSERT_EQUAL(EFFECT_PARSE_OK, parseEffectPayload(reversed, b));
    TEST_ASSERT_TRUE(configsEqual(a, b));
}

// 4 -- whitespace everywhere
void test_case04_whitespace_tolerant(void) {
    CustomEffectConfig out;
    EffectParseResult r = parseEffectPayload(
        "{ \"mode\" : \"CHASE\" , \"colors\" : [ \"#FF6600\" , \"#00AAFF\" ] }", out);
    TEST_ASSERT_EQUAL(EFFECT_PARSE_OK, r);
    TEST_ASSERT_EQUAL(EFFECT_CHASE, out.mode);
    TEST_ASSERT_EQUAL_UINT8(2, out.colorCount);
    TEST_ASSERT_TRUE(crgbEq(out.colors[0], 0xFF, 0x66, 0x00));
    TEST_ASSERT_TRUE(crgbEq(out.colors[1], 0x00, 0xAA, 0xFF));
}

// 5 -- case-insensitive key and value
void test_case05_case_insensitive_key_and_value(void) {
    CustomEffectConfig out;
    EffectParseResult r = parseEffectPayload("{\"MoDe\":\"chase\",\"colors\":[\"#FF6600\"]}", out);
    TEST_ASSERT_EQUAL(EFFECT_PARSE_OK, r);
    TEST_ASSERT_EQUAL(EFFECT_CHASE, out.mode);
}

// 6 -- hex color without leading '#'
void test_case06_hex_without_hash(void) {
    CustomEffectConfig out;
    EffectParseResult r = parseEffectPayload("{\"mode\":\"CHASE\",\"colors\":[\"FF6600\"]}", out);
    TEST_ASSERT_EQUAL(EFFECT_PARSE_OK, r);
    TEST_ASSERT_TRUE(crgbEq(out.colors[0], 0xFF, 0x66, 0x00));
}

// 7 -- empty colors array
void test_case07_empty_colors_array(void) {
    CustomEffectConfig out;
    EffectParseResult r = parseEffectPayload("{\"mode\":\"CHASE\",\"colors\":[]}", out);
    TEST_ASSERT_EQUAL(EFFECT_PARSE_BAD_COLOR_COUNT, r);
}

// 8 -- lower boundary: 1 color
void test_case08_one_color_lower_boundary(void) {
    CustomEffectConfig out;
    EffectParseResult r = parseEffectPayload("{\"mode\":\"CHASE\",\"colors\":[\"#FF0000\"]}", out);
    TEST_ASSERT_EQUAL(EFFECT_PARSE_OK, r);
    TEST_ASSERT_EQUAL_UINT8(1, out.colorCount);
}

// 9 -- upper boundary: 8 colors
void test_case09_eight_colors_upper_boundary(void) {
    CustomEffectConfig out;
    EffectParseResult r = parseEffectPayload(
        "{\"mode\":\"CHASE\",\"colors\":[\"#000000\",\"#000000\",\"#000000\",\"#000000\","
        "\"#000000\",\"#000000\",\"#000000\",\"#000000\"]}", out);
    TEST_ASSERT_EQUAL(EFFECT_PARSE_OK, r);
    TEST_ASSERT_EQUAL_UINT8(8, out.colorCount);
}

// 10 -- 9 colors: upper boundary + 1
void test_case10_nine_colors_rejected(void) {
    CustomEffectConfig out;
    EffectParseResult r = parseEffectPayload(
        "{\"mode\":\"CHASE\",\"colors\":[\"#000000\",\"#000000\",\"#000000\",\"#000000\","
        "\"#000000\",\"#000000\",\"#000000\",\"#000000\",\"#000000\"]}", out);
    TEST_ASSERT_EQUAL(EFFECT_PARSE_BAD_COLOR_COUNT, r);
}

// 11 -- five separate malformed-hex asserts
void test_case11_bad_color_forms(void) {
    CustomEffectConfig out;
    TEST_ASSERT_EQUAL(EFFECT_PARSE_BAD_COLOR,
        parseEffectPayload("{\"mode\":\"CHASE\",\"colors\":[\"#GG0000\"]}", out));
    TEST_ASSERT_EQUAL(EFFECT_PARSE_BAD_COLOR,
        parseEffectPayload("{\"mode\":\"CHASE\",\"colors\":[\"#FF660\"]}", out));
    TEST_ASSERT_EQUAL(EFFECT_PARSE_BAD_COLOR,
        parseEffectPayload("{\"mode\":\"CHASE\",\"colors\":[\"#FF66000\"]}", out));
    TEST_ASSERT_EQUAL(EFFECT_PARSE_BAD_COLOR,
        parseEffectPayload("{\"mode\":\"CHASE\",\"colors\":[\"\"]}", out));
    TEST_ASSERT_EQUAL(EFFECT_PARSE_BAD_COLOR,
        parseEffectPayload("{\"mode\":\"CHASE\",\"colors\":[\"0xFF6600\"]}", out));
}

// 12 -- bad mode forms, including numeric (not accepted)
void test_case12_bad_mode_forms(void) {
    CustomEffectConfig out;
    TEST_ASSERT_EQUAL(EFFECT_PARSE_BAD_MODE,
        parseEffectPayload("{\"mode\":\"SPIRAL\",\"colors\":[\"#FF0000\"]}", out));
    TEST_ASSERT_EQUAL(EFFECT_PARSE_BAD_MODE,
        parseEffectPayload("{\"mode\":\"\",\"colors\":[\"#FF0000\"]}", out));
    TEST_ASSERT_EQUAL(EFFECT_PARSE_BAD_MODE,
        parseEffectPayload("{\"mode\":2,\"colors\":[\"#FF0000\"]}", out));
}

// 13 -- missing required keys
void test_case13_missing_required_keys(void) {
    CustomEffectConfig out;
    TEST_ASSERT_EQUAL(EFFECT_PARSE_NO_MODE,
        parseEffectPayload("{\"colors\":[\"#FF0000\"]}", out));
    TEST_ASSERT_EQUAL(EFFECT_PARSE_NO_COLORS,
        parseEffectPayload("{\"mode\":\"CHASE\"}", out));
}

// 14 -- defaults when speed/intensity/timeout absent
void test_case14_defaults_when_absent(void) {
    CustomEffectConfig out;
    EffectParseResult r = parseEffectPayload("{\"mode\":\"CHASE\",\"colors\":[\"#FF0000\"]}", out);
    TEST_ASSERT_EQUAL(EFFECT_PARSE_OK, r);
    TEST_ASSERT_EQUAL_UINT8(128, out.speed);
    TEST_ASSERT_EQUAL_UINT8(128, out.intensity);
    TEST_ASSERT_EQUAL_UINT32(0, out.timeoutMs);
}

// 15 -- four separate BAD_NUMBER asserts
void test_case15_bad_number_forms(void) {
    CustomEffectConfig out;
    TEST_ASSERT_EQUAL(EFFECT_PARSE_BAD_NUMBER,
        parseEffectPayload("{\"mode\":\"CHASE\",\"colors\":[\"#FF0000\"],\"speed\":300}", out));
    TEST_ASSERT_EQUAL(EFFECT_PARSE_BAD_NUMBER,
        parseEffectPayload("{\"mode\":\"CHASE\",\"colors\":[\"#FF0000\"],\"speed\":\"128\"}", out));
    TEST_ASSERT_EQUAL(EFFECT_PARSE_BAD_NUMBER,
        parseEffectPayload("{\"mode\":\"CHASE\",\"colors\":[\"#FF0000\"],\"speed\":-5}", out));
    TEST_ASSERT_EQUAL(EFFECT_PARSE_BAD_NUMBER,
        parseEffectPayload("{\"mode\":\"CHASE\",\"colors\":[\"#FF0000\"],\"intensity\":256}", out));
}

// 16 -- range boundaries both ends, speed and intensity
void test_case16_number_range_boundaries(void) {
    CustomEffectConfig out;
    TEST_ASSERT_EQUAL(EFFECT_PARSE_OK,
        parseEffectPayload("{\"mode\":\"CHASE\",\"colors\":[\"#FF0000\"],\"speed\":0}", out));
    TEST_ASSERT_EQUAL_UINT8(0, out.speed);
    TEST_ASSERT_EQUAL(EFFECT_PARSE_OK,
        parseEffectPayload("{\"mode\":\"CHASE\",\"colors\":[\"#FF0000\"],\"speed\":255}", out));
    TEST_ASSERT_EQUAL_UINT8(255, out.speed);
    TEST_ASSERT_EQUAL(EFFECT_PARSE_OK,
        parseEffectPayload("{\"mode\":\"CHASE\",\"colors\":[\"#FF0000\"],\"intensity\":0}", out));
    TEST_ASSERT_EQUAL_UINT8(0, out.intensity);
    TEST_ASSERT_EQUAL(EFFECT_PARSE_OK,
        parseEffectPayload("{\"mode\":\"CHASE\",\"colors\":[\"#FF0000\"],\"intensity\":255}", out));
    TEST_ASSERT_EQUAL_UINT8(255, out.intensity);
}

// 17 -- the timeout clamp, both sides of the boundary, and the no-overflow case
void test_case17_timeout_clamp(void) {
    CustomEffectConfig out;
    const char* base = "{\"mode\":\"CHASE\",\"colors\":[\"#FF0000\"],\"timeout\":%s}";
    char buf[160];

    snprintf(buf, sizeof(buf), base, "0");
    TEST_ASSERT_EQUAL(EFFECT_PARSE_OK, parseEffectPayload(buf, out));
    TEST_ASSERT_EQUAL_UINT32(0, out.timeoutMs);

    snprintf(buf, sizeof(buf), base, "28799");
    TEST_ASSERT_EQUAL(EFFECT_PARSE_OK, parseEffectPayload(buf, out));
    TEST_ASSERT_EQUAL_UINT32(28799000UL, out.timeoutMs);

    snprintf(buf, sizeof(buf), base, "28800");
    TEST_ASSERT_EQUAL(EFFECT_PARSE_OK, parseEffectPayload(buf, out));
    TEST_ASSERT_EQUAL_UINT32(28800000UL, out.timeoutMs);

    snprintf(buf, sizeof(buf), base, "28801");
    TEST_ASSERT_EQUAL(EFFECT_PARSE_OK, parseEffectPayload(buf, out));
    TEST_ASSERT_EQUAL_UINT32(28800000UL, out.timeoutMs);

    snprintf(buf, sizeof(buf), base, "999999999999999");
    TEST_ASSERT_EQUAL(EFFECT_PARSE_OK, parseEffectPayload(buf, out));
    TEST_ASSERT_EQUAL_UINT32(28800000UL, out.timeoutMs);
}

// 18 -- structural rejections
void test_case18_structural_rejections(void) {
    CustomEffectConfig out;
    TEST_ASSERT_EQUAL(EFFECT_PARSE_NOT_OBJECT, parseEffectPayload("", out));
    TEST_ASSERT_EQUAL(EFFECT_PARSE_NOT_OBJECT, parseEffectPayload("not json", out));
    TEST_ASSERT_EQUAL(EFFECT_PARSE_NOT_OBJECT,
        parseEffectPayload("{\"mode\":\"CHASE\",\"colors\":[\"#FF6600\"", out));
    TEST_ASSERT_EQUAL(EFFECT_PARSE_NO_MODE, parseEffectPayload("{}", out));
}

// 19 -- unknown extra key ignored
void test_case19_unknown_key_ignored(void) {
    CustomEffectConfig out;
    EffectParseResult r = parseEffectPayload(
        "{\"mode\":\"CHASE\",\"colors\":[\"#FF6600\"],\"wobble\":7}", out);
    TEST_ASSERT_EQUAL(EFFECT_PARSE_OK, r);
    TEST_ASSERT_EQUAL(EFFECT_CHASE, out.mode);
}

// 20 -- prior state preserved on every rejection in 7/10/11/12/13/15/18
void test_case20_prior_state_preserved_on_rejection(void) {
    const char* rejecting[] = {
        "{\"mode\":\"CHASE\",\"colors\":[]}",                                    // 7
        "{\"mode\":\"CHASE\",\"colors\":[\"#000000\",\"#000000\",\"#000000\","
            "\"#000000\",\"#000000\",\"#000000\",\"#000000\",\"#000000\",\"#000000\"]}", // 10
        "{\"mode\":\"CHASE\",\"colors\":[\"#GG0000\"]}",                         // 11
        "{\"mode\":\"SPIRAL\",\"colors\":[\"#FF0000\"]}",                        // 12
        "{\"colors\":[\"#FF0000\"]}",                                           // 13
        "{\"mode\":\"CHASE\",\"colors\":[\"#FF0000\"],\"speed\":300}",          // 15
        "not json",                                                             // 18
    };

    for (size_t i = 0; i < sizeof(rejecting) / sizeof(rejecting[0]); i++) {
        CustomEffectConfig live = fullyPopulatedSeed();
        CustomEffectConfig seedCopy = live;
        CustomEffectConfig scratch;
        EffectParseResult r = parseEffectPayload(rejecting[i], scratch);
        TEST_ASSERT_NOT_EQUAL(EFFECT_PARSE_OK, r);
        if (r == EFFECT_PARSE_OK) live = scratch;
        TEST_ASSERT_TRUE_MESSAGE(configsEqual(live, seedCopy),
            "a rejected parse must never mutate the live config");
    }
}

// 21 -- prefix arithmetic, the #0008 off-by-one, pinned structurally
void test_case21_prefix_arithmetic(void) {
    TEST_ASSERT_EQUAL_UINT32(strlen("SET_EFFECT:"), sizeof(SET_EFFECT_PREFIX) - 1);

    const char* full = "SET_EFFECT:{\"mode\":\"CHASE\",\"colors\":[\"#FF6600\"]}";
    CustomEffectConfig out;
    EffectParseResult r = parseEffectPayload(full + (sizeof(SET_EFFECT_PREFIX) - 1), out);
    TEST_ASSERT_EQUAL(EFFECT_PARSE_OK, r);
    TEST_ASSERT_EQUAL(EFFECT_CHASE, out.mode);
    TEST_ASSERT_EQUAL_UINT8(1, out.colorCount);
    TEST_ASSERT_TRUE(crgbEq(out.colors[0], 0xFF, 0x66, 0x00));
}

// ---------------------------------------------------------------------
// B. Persistence decision -- applyLoadedCustomEffect() / resetCustomEffect()
//    / customEffectIsValid()
// ---------------------------------------------------------------------

// 22 -- old firmware, never written: version 0, length 0, null bytes
void test_case22_never_written_rejected(void) {
    CustomEffectConfig out;
    CustomEffectConfig defaults;
    resetCustomEffect(defaults);
    bool accepted = applyLoadedCustomEffect(0, 0, nullptr, out);
    TEST_ASSERT_FALSE(accepted);
    TEST_ASSERT_TRUE(configsEqual(out, defaults));
}

// 23 -- THE test that proves the version byte does its job: a fully
// plausible valid-looking config at the wrong version must still be rejected.
void test_case23_wrong_version_with_plausible_bytes_rejected(void) {
    CustomEffectConfig plausible;
    resetCustomEffect(plausible);
    plausible.mode = EFFECT_CHASE;
    plausible.colors[0] = CRGB(0xFF, 0x66, 0x00);
    plausible.colors[1] = CRGB(0x00, 0xAA, 0xFF);
    plausible.colorCount = 2;
    plausible.speed = 100;
    plausible.intensity = 200;
    plausible.timeoutMs = 60000;
    plausible.activatedAt = 999;
    plausible.revertTheme = THEME_RAINBOW;
    TEST_ASSERT_TRUE_MESSAGE(customEffectIsValid(plausible),
        "fixture must itself be plausible, or this test proves nothing about the version byte");

    CustomEffectConfig out;
    bool accepted = applyLoadedCustomEffect(1, sizeof(CustomEffectConfig), &plausible, out);
    TEST_ASSERT_FALSE(accepted);

    CustomEffectConfig defaults;
    resetCustomEffect(defaults);
    TEST_ASSERT_TRUE(configsEqual(out, defaults));
}

// 24 -- correct version, wrong length (both directions)
void test_case24_wrong_length_rejected(void) {
    CustomEffectConfig plausible;
    resetCustomEffect(plausible);
    plausible.colorCount = 1;

    CustomEffectConfig out;
    TEST_ASSERT_FALSE(applyLoadedCustomEffect(SETTINGS_VERSION, sizeof(CustomEffectConfig) - 1, &plausible, out));
    TEST_ASSERT_FALSE(applyLoadedCustomEffect(SETTINGS_VERSION, sizeof(CustomEffectConfig) + 1, &plausible, out));
}

// 25 -- correct version + length, five separate invalid-content rejections
void test_case25_invalid_content_rejected(void) {
    CustomEffectConfig out;
    CustomEffectConfig c;

    resetCustomEffect(c); c.colorCount = 0;
    TEST_ASSERT_FALSE(applyLoadedCustomEffect(SETTINGS_VERSION, sizeof(c), &c, out));

    resetCustomEffect(c); c.colorCount = 9;
    TEST_ASSERT_FALSE(applyLoadedCustomEffect(SETTINGS_VERSION, sizeof(c), &c, out));

    resetCustomEffect(c); c.colorCount = 1; c.mode = (EffectMode)42;
    TEST_ASSERT_FALSE(applyLoadedCustomEffect(SETTINGS_VERSION, sizeof(c), &c, out));

    resetCustomEffect(c); c.colorCount = 1; c.revertTheme = THEME_CUSTOM;
    TEST_ASSERT_FALSE_MESSAGE(applyLoadedCustomEffect(SETTINGS_VERSION, sizeof(c), &c, out),
        "revertTheme == THEME_CUSTOM is the infinite-self-revert trap and must be rejected");

    resetCustomEffect(c); c.colorCount = 1; c.timeoutMs = 28800001UL;
    TEST_ASSERT_FALSE(applyLoadedCustomEffect(SETTINGS_VERSION, sizeof(c), &c, out));
}

// 26 -- correct version + length + valid config: accepted, exact
// field-by-field round-trip, activatedAt forced to 0 despite nonzero input
void test_case26_valid_config_accepted_and_activated_at_zeroed(void) {
    CustomEffectConfig c;
    resetCustomEffect(c);
    c.mode = EFFECT_PULSE;
    c.colors[0] = CRGB(1, 2, 3);
    c.colors[1] = CRGB(4, 5, 6);
    c.colorCount = 2;
    c.speed = 33;
    c.intensity = 44;
    c.timeoutMs = 15000;
    c.activatedAt = 777777; // must be forced to 0 on load
    c.revertTheme = THEME_OCEAN_WAVES;

    CustomEffectConfig out;
    bool accepted = applyLoadedCustomEffect(SETTINGS_VERSION, sizeof(c), &c, out);
    TEST_ASSERT_TRUE(accepted);
    TEST_ASSERT_EQUAL(c.mode, out.mode);
    TEST_ASSERT_TRUE(out.colors[0] == c.colors[0]);
    TEST_ASSERT_TRUE(out.colors[1] == c.colors[1]);
    TEST_ASSERT_EQUAL_UINT8(c.colorCount, out.colorCount);
    TEST_ASSERT_EQUAL_UINT8(c.speed, out.speed);
    TEST_ASSERT_EQUAL_UINT8(c.intensity, out.intensity);
    TEST_ASSERT_EQUAL_UINT32(c.timeoutMs, out.timeoutMs);
    TEST_ASSERT_EQUAL_UINT32(0, out.activatedAt);
    TEST_ASSERT_EQUAL(c.revertTheme, out.revertTheme);
}

// 27 -- resetCustomEffect() alone
void test_case27_reset_custom_effect_defaults(void) {
    CustomEffectConfig c = fullyPopulatedSeed();
    resetCustomEffect(c);
    TEST_ASSERT_EQUAL(EFFECT_BLEND, c.mode);
    TEST_ASSERT_EQUAL_UINT8(0, c.colorCount);
    TEST_ASSERT_EQUAL_UINT8(128, c.speed);
    TEST_ASSERT_EQUAL_UINT8(128, c.intensity);
    TEST_ASSERT_EQUAL_UINT32(0, c.timeoutMs);
    TEST_ASSERT_EQUAL_UINT32(0, c.activatedAt);
    TEST_ASSERT_EQUAL(THEME_GREEN, c.revertTheme);
}

// ---------------------------------------------------------------------
// C. Wire-size guard
// ---------------------------------------------------------------------

// 28 -- longest legal command still fits PubSubClient's 256-byte default
// buffer with margin, on the longest realistic topic (issues/0016.md section
// 9.1's arithmetic, pinned here rather than trusted).
void test_case28_longest_legal_command_fits_mqtt_buffer(void) {
    const char* cmd =
        "SET_EFFECT:{\"mode\":\"COLORLOOP\",\"colors\":["
        "\"#FFFFFF\",\"#FFFFFF\",\"#FFFFFF\",\"#FFFFFF\","
        "\"#FFFFFF\",\"#FFFFFF\",\"#FFFFFF\",\"#FFFFFF\"],"
        "\"speed\":128,\"intensity\":128,\"timeout\":28800}";

    size_t cmdLen = strlen(cmd);
    size_t topicLen = strlen("lights/kitchen/cmd");
    // fixed header (1) + remaining-length (2) + topic-length field (2) + topic
    size_t total = cmdLen + 2 + topicLen + 3;
    TEST_ASSERT_LESS_OR_EQUAL(256, total);

    // The parser itself must also accept this exact payload.
    CustomEffectConfig out;
    EffectParseResult r = parseEffectPayload(cmd + (sizeof(SET_EFFECT_PREFIX) - 1), out);
    TEST_ASSERT_EQUAL(EFFECT_PARSE_OK, r);
    TEST_ASSERT_EQUAL(EFFECT_COLORLOOP, out.mode);
    TEST_ASSERT_EQUAL_UINT8(8, out.colorCount);
    TEST_ASSERT_EQUAL_UINT8(128, out.speed);
    TEST_ASSERT_EQUAL_UINT8(128, out.intensity);
    TEST_ASSERT_EQUAL_UINT32(28800000UL, out.timeoutMs);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_case01_minimal_one_color);
    RUN_TEST(test_case02_full_eight_colors);
    RUN_TEST(test_case03_key_order_irrelevant);
    RUN_TEST(test_case04_whitespace_tolerant);
    RUN_TEST(test_case05_case_insensitive_key_and_value);
    RUN_TEST(test_case06_hex_without_hash);
    RUN_TEST(test_case07_empty_colors_array);
    RUN_TEST(test_case08_one_color_lower_boundary);
    RUN_TEST(test_case09_eight_colors_upper_boundary);
    RUN_TEST(test_case10_nine_colors_rejected);
    RUN_TEST(test_case11_bad_color_forms);
    RUN_TEST(test_case12_bad_mode_forms);
    RUN_TEST(test_case13_missing_required_keys);
    RUN_TEST(test_case14_defaults_when_absent);
    RUN_TEST(test_case15_bad_number_forms);
    RUN_TEST(test_case16_number_range_boundaries);
    RUN_TEST(test_case17_timeout_clamp);
    RUN_TEST(test_case18_structural_rejections);
    RUN_TEST(test_case19_unknown_key_ignored);
    RUN_TEST(test_case20_prior_state_preserved_on_rejection);
    RUN_TEST(test_case21_prefix_arithmetic);
    RUN_TEST(test_case22_never_written_rejected);
    RUN_TEST(test_case23_wrong_version_with_plausible_bytes_rejected);
    RUN_TEST(test_case24_wrong_length_rejected);
    RUN_TEST(test_case25_invalid_content_rejected);
    RUN_TEST(test_case26_valid_config_accepted_and_activated_at_zeroed);
    RUN_TEST(test_case27_reset_custom_effect_defaults);
    RUN_TEST(test_case28_longest_legal_command_fits_mqtt_buffer);
    return UNITY_END();
}
