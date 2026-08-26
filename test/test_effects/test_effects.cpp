// Native (host-compiled) frame-capture diff for issue #0014: proves the
// EffectMode extraction (renderBlend()/renderFlicker() in src/effects.h)
// renders byte-for-byte identically to the pre-#0014 flickerLEDs()/
// slowBlend() it replaced, for all six built-in themes. Run with
// `pio test -e native`.
//
// Method (issue #0014 plan, section 4): drive two independent EffectStates
// -- one through the real, currently-shipping renderBlend()/renderFlicker(),
// one through legacy_effects.h's frozen transcription of the pre-#0014
// functions -- from identical starting state and identical RNG seeds, for
// ~600 frames (~6s of simulated animation, stepping `now` by 10ms), and
// assert after every single frame that leds[], hueIndex, blendOffset and
// timeouts[] all agree. A visual inspection on a flashed board cannot catch
// a subtle truncation bug like blendOffset's uint8_t width; this can, and
// it can be rerun by every later phase of issue #0013.
#include <unity.h>
#include <stdint.h>
#include <string.h>
#include <cstdio>

// Include-order contract (see src/effects.h): the shim must come first so
// CRGB/CHSV/sin8/min/max are in scope before effects.h/themes.h are parsed.
#include "fastled_shim.h"
#include "effects.h"
#include "legacy_effects.h"

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------
// Deterministic, independently-seeded RNG streams. EffectState::rng is a
// bare function pointer (uint32_t(*)(uint32_t,uint32_t)) with no state
// parameter -- by design, so the device build stays a two-line wrapper over
// Arduino random(). That means the two EffectStates under test need two
// SEPARATE global RNG states (rngStateA / rngStateB), reset to the same
// seed before every run: if renderFlicker() and legacyFlicker() ever drew a
// different number of randoms, or in a different order, per-LED, the two
// streams would desync and every following frame would (correctly) fail
// the diff.
// ---------------------------------------------------------------------

static uint32_t rngStateA;
static uint32_t rngStateB;

static uint32_t xorshift32(uint32_t& state) {
    uint32_t x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return x;
}

static uint32_t rngA(uint32_t lo, uint32_t hi) {
    return lo + (xorshift32(rngStateA) % (hi - lo));
}

static uint32_t rngB(uint32_t lo, uint32_t hi) {
    return lo + (xorshift32(rngStateB) % (hi - lo));
}

static const uint32_t RNG_SEED = 0x12345678u;

// ---------------------------------------------------------------------
// Fixtures: one per cycleable built-in theme, using the REAL tables from
// src/themes.h (not hand-copied stand-ins) so a future change to those
// tables is caught here automatically.
// ---------------------------------------------------------------------

struct ThemeFixture {
    const char* name;
    const uint8_t* colors;
    int colorCount;
};

static const ThemeFixture THEMES[] = {
    {"Green",       GREEN_HUES,     NUM_GREEN_HUES},
    {"Rainbow",     RAINBOW_HUES,   NUM_RAINBOW_HUES},
    {"Pink Pony",   PINK_PONY_HUES, NUM_PINK_PONY_HUES},
    {"Ocean Waves", OCEAN_HUES,     NUM_OCEAN_HUES},
    {"Sunset",      SUNSET_HUES,    NUM_SUNSET_HUES},
    {"Forest",      FOREST_HUES,    NUM_FOREST_HUES},
};
static const int NUM_THEME_FIXTURES = sizeof(THEMES) / sizeof(THEMES[0]);
static_assert(NUM_THEME_FIXTURES == CYCLEABLE_THEME_COUNT,
              "one fixture per cycleable theme");

// Mirrors main.cpp's device defaults (numLeds=240, ledsPerColor=25,
// BRIGHTNESS=180, MAX_BRIGHTNESS=225) so the exercised math -- especially
// blendOffset's `% (numLeds * colorCount)` truncation -- matches what
// actually ships.
static const int TEST_NUM_LEDS = 240;
static const int TEST_LEDS_PER_COLOR = 25;
static const int TEST_BRIGHTNESS = 180;
static const int TEST_MAX_BRIGHTNESS = 225;

// 600 frames * 10ms = 6s of simulated animation: covers the 100ms blend
// tick, the 500ms rotate tick, the 500-750ms flicker tick and the 2000ms
// hue rotation several times over.
static const int TEST_FRAMES = 600;
static const uint32_t TEST_STEP_MS = 10;

// Drives EFFECT_FLICKER through renderFlicker() (state A) and
// legacyFlicker() (state B) side by side, or EFFECT_BLEND through
// renderBlend()/legacyBlend(), asserting agreement after every frame.
//
// `a` (EffectState, the real/shipping struct) and `b` (LegacyEffectState,
// pinned uint8_t blendOffset -- see legacy_effects.h) are DELIBERATELY
// separate types, populated field-by-field below rather than via `b = a`.
// That decoupling is what lets this harness catch a widened
// EffectState::blendOffset at all: see legacy_effects.h's header comment
// and issues/0014.md's Gotchas for why a shared struct type made the
// perturbation invisible in round 1.
//
// initialBlendOffset seeds both sides at 250 for the BLEND-mode fixtures
// (see call sites below), so the uint8_t truncation at 256 is crossed
// inside this function's 600-frame window instead of requiring ~12,750
// frames from a 0 start -- see issues/0014.md's Gotchas for the arithmetic.
static void runThemeDiff(const ThemeFixture& theme, EffectMode mode, uint32_t startNow,
                          uint8_t initialBlendOffset = 0) {
    CRGB ledsA[TEST_NUM_LEDS];
    CRGB ledsB[TEST_NUM_LEDS];
    uint32_t timeoutsA[TEST_NUM_LEDS];
    uint32_t timeoutsB[TEST_NUM_LEDS];

    for (int i = 0; i < TEST_NUM_LEDS; i++) {
        timeoutsA[i] = startNow;
        timeoutsB[i] = startNow;
    }

    EffectState a = {};
    a.leds = ledsA;
    a.numLeds = TEST_NUM_LEDS;
    a.ledsPerColor = TEST_LEDS_PER_COLOR;
    a.colors = theme.colors;
    a.colorCount = theme.colorCount;
    a.hueIndex = 0;
    a.colorChangeEnabled = true;
    a.brightness = TEST_BRIGHTNESS;
    a.maxBrightness = TEST_MAX_BRIGHTNESS;
    a.timeouts = timeoutsA;
    a.blendTimeout = startNow;
    a.rotateTimeout = startNow;
    a.hueTimeout = startNow + 2000;
    a.blendOffset = initialBlendOffset;
    a.rng = rngA;

    // Field-by-field, not `LegacyEffectState b = a;` -- the two are
    // different types on purpose (see comment above).
    LegacyEffectState b = {};
    b.leds = ledsB;
    b.numLeds = a.numLeds;
    b.ledsPerColor = a.ledsPerColor;
    b.colors = a.colors;
    b.colorCount = a.colorCount;
    b.hueIndex = a.hueIndex;
    b.colorChangeEnabled = a.colorChangeEnabled;
    b.brightness = a.brightness;
    b.maxBrightness = a.maxBrightness;
    b.timeouts = timeoutsB;
    b.blendTimeout = a.blendTimeout;
    b.rotateTimeout = a.rotateTimeout;
    b.hueTimeout = a.hueTimeout;
    b.blendOffset = initialBlendOffset;
    b.rng = rngB;

    rngStateA = RNG_SEED;
    rngStateB = RNG_SEED;

    uint32_t now = startNow;
    for (int frame = 0; frame < TEST_FRAMES; frame++) {
        bool drewA, drewB;
        if (mode == EFFECT_FLICKER) {
            drewA = renderFlicker(a, now);
            drewB = legacyFlicker(b, now);
        } else {
            drewA = renderBlend(a, now);
            drewB = legacyBlend(b, now);
        }

        char msg[128];
        snprintf(msg, sizeof(msg), "%s frame %d: drew mismatch (new=%d legacy=%d)",
                 theme.name, frame, drewA, drewB);
        TEST_ASSERT_EQUAL_MESSAGE(drewA, drewB, msg);

        snprintf(msg, sizeof(msg), "%s frame %d: leds[] diverged", theme.name, frame);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(ledsA, ledsB, sizeof(CRGB) * TEST_NUM_LEDS, msg);

        snprintf(msg, sizeof(msg), "%s frame %d: timeouts[] diverged", theme.name, frame);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(timeoutsA, timeoutsB, sizeof(uint32_t) * TEST_NUM_LEDS, msg);

        snprintf(msg, sizeof(msg), "%s frame %d: hueIndex diverged (new=%d legacy=%d)",
                 theme.name, frame, a.hueIndex, b.hueIndex);
        TEST_ASSERT_EQUAL_MESSAGE(a.hueIndex, b.hueIndex, msg);

        snprintf(msg, sizeof(msg), "%s frame %d: blendOffset diverged (new=%u legacy=%u)",
                 theme.name, frame, a.blendOffset, b.blendOffset);
        TEST_ASSERT_EQUAL_MESSAGE(a.blendOffset, b.blendOffset, msg);

        now += TEST_STEP_MS;
    }
}

void test_theme_green_matches_legacy(void) {
    runThemeDiff(THEMES[THEME_GREEN], BUILTIN_EFFECT_MODE[THEME_GREEN], 1000u);
}

void test_theme_rainbow_matches_legacy(void) {
    // blendOffset seeded near the uint8_t wrap -- see runThemeDiff's comment
    // and issues/0014.md's Gotchas.
    runThemeDiff(THEMES[THEME_RAINBOW], BUILTIN_EFFECT_MODE[THEME_RAINBOW], 1000u, 250);
}

void test_theme_pink_pony_matches_legacy(void) {
    runThemeDiff(THEMES[THEME_PINK_PONY], BUILTIN_EFFECT_MODE[THEME_PINK_PONY], 1000u, 250);
}

void test_theme_ocean_waves_matches_legacy(void) {
    runThemeDiff(THEMES[THEME_OCEAN_WAVES], BUILTIN_EFFECT_MODE[THEME_OCEAN_WAVES], 1000u, 250);
}

void test_theme_sunset_matches_legacy(void) {
    runThemeDiff(THEMES[THEME_SUNSET], BUILTIN_EFFECT_MODE[THEME_SUNSET], 1000u, 250);
}

void test_theme_forest_matches_legacy(void) {
    runThemeDiff(THEMES[THEME_FOREST], BUILTIN_EFFECT_MODE[THEME_FOREST], 1000u, 250);
}

// One run starting near the 2^32 millis() rollover (issue #0008), so the
// wrap path inside the renderers is exercised too, for both dispatch
// branches (FLICKER's per-LED timeouts[] array and BLEND's
// blendTimeout/rotateTimeout). startNow leaves 3000ms of runway before the
// wrap, which happens at frame 300 of the 600-frame run -- so this run
// covers frames both before and after the wrap.
void test_rollover_flicker_matches_legacy(void) {
    uint32_t startNow = (uint32_t)(0u - 3000u); // 3000ms before 2^32
    runThemeDiff(THEMES[THEME_GREEN], EFFECT_FLICKER, startNow);
}

void test_rollover_blend_matches_legacy(void) {
    uint32_t startNow = (uint32_t)(0u - 3000u); // 3000ms before 2^32
    runThemeDiff(THEMES[THEME_RAINBOW], EFFECT_BLEND, startNow, 250);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_theme_green_matches_legacy);
    RUN_TEST(test_theme_rainbow_matches_legacy);
    RUN_TEST(test_theme_pink_pony_matches_legacy);
    RUN_TEST(test_theme_ocean_waves_matches_legacy);
    RUN_TEST(test_theme_sunset_matches_legacy);
    RUN_TEST(test_theme_forest_matches_legacy);
    RUN_TEST(test_rollover_flicker_matches_legacy);
    RUN_TEST(test_rollover_blend_matches_legacy);
    return UNITY_END();
}
