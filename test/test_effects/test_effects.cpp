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
    const PaletteColor* colors;
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

    // LegacyEffectState.colors is deliberately still `const uint8_t*` -- a
    // bare hue array, per legacy_effects.h's frozen pre-#0015 shape. Extract
    // just the hue byte from each PaletteColor for the reference side; the
    // real (EffectState) side keeps the full widened triple. Since
    // legacyBlend()/legacyFlicker() only ever read the hue, and every
    // built-in PaletteColor is {hue, 255, 200}, this is exactly the same
    // sequence of hue bytes runThemeDiff read before the widening.
    static uint8_t legacyHues[32];
    for (int i = 0; i < theme.colorCount; i++) legacyHues[i] = theme.colors[i].h;

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
    b.colors = legacyHues;
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
    runThemeDiff(THEMES[THEME_GREEN], EFFECT_FLICKER, 1000u);
}

void test_theme_rainbow_matches_legacy(void) {
    // blendOffset seeded near the uint8_t wrap -- see runThemeDiff's comment
    // and issues/0014.md's Gotchas.
    runThemeDiff(THEMES[THEME_RAINBOW], EFFECT_BLEND, 1000u, 250);
}

void test_theme_pink_pony_matches_legacy(void) {
    runThemeDiff(THEMES[THEME_PINK_PONY], EFFECT_BLEND, 1000u, 250);
}

void test_theme_ocean_waves_matches_legacy(void) {
    runThemeDiff(THEMES[THEME_OCEAN_WAVES], EFFECT_BLEND, 1000u, 250);
}

void test_theme_sunset_matches_legacy(void) {
    runThemeDiff(THEMES[THEME_SUNSET], EFFECT_BLEND, 1000u, 250);
}

void test_theme_forest_matches_legacy(void) {
    runThemeDiff(THEMES[THEME_FOREST], EFFECT_BLEND, 1000u, 250);
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

// =======================================================================
// Issue #0015, Gates 1-6: the seven new renderers (CHASE, WIPE, SCAN,
// SPARKLE, PULSE, STROBE, COLORLOOP). See issues/0015.md section 6.
//
// Unlike the frame-diff cases above, there is no legacy counterpart for
// these seven -- the method here is bounds sweeps, rollover survival,
// determinism, monotonicity in speed/intensity, and palette coverage.
// =======================================================================

// ---- Gate 1: BUILTIN_EFFECT_MODE[] self-selection (#0014 gotcha) --------
// The six runThemeDiff() call sites above now pass literal EFFECT_FLICKER/
// EFFECT_BLEND rather than indexing BUILTIN_EFFECT_MODE[THEME_X], so the
// diff no longer picks its own renderer from the table it exists to check.
// This is the other half: assert the table's actual contents against
// independent literals, so a wrong entry (e.g. THEME_FOREST mapped to
// EFFECT_FLICKER) is caught here instead of silently running the same
// renderer on both sides of a diff that would then still pass.
void test_builtin_effect_mode_table(void) {
    TEST_ASSERT_EQUAL(EFFECT_FLICKER, BUILTIN_EFFECT_MODE[THEME_GREEN]);
    TEST_ASSERT_EQUAL(EFFECT_BLEND, BUILTIN_EFFECT_MODE[THEME_RAINBOW]);
    TEST_ASSERT_EQUAL(EFFECT_BLEND, BUILTIN_EFFECT_MODE[THEME_PINK_PONY]);
    TEST_ASSERT_EQUAL(EFFECT_BLEND, BUILTIN_EFFECT_MODE[THEME_OCEAN_WAVES]);
    TEST_ASSERT_EQUAL(EFFECT_BLEND, BUILTIN_EFFECT_MODE[THEME_SUNSET]);
    TEST_ASSERT_EQUAL(EFFECT_BLEND, BUILTIN_EFFECT_MODE[THEME_FOREST]);
}

// ---------------------------------------------------------------------
// Shared fixtures for Gates 2-6: a small, independent 3-entry palette
// (deliberately not one of the six built-in themes -- these gates are
// about the renderers, not the theme tables) and a table of all seven
// renderers.
// ---------------------------------------------------------------------

static const PaletteColor GATE_PALETTE[] = {
    {0, 255, 200}, {85, 255, 200}, {170, 255, 200}
};
static const int GATE_COLOR_COUNT = sizeof(GATE_PALETTE) / sizeof(GATE_PALETTE[0]);

typedef bool (*RenderFn)(EffectState&, uint32_t);

struct RendererSpec {
    const char* name;
    RenderFn fn;
    uint16_t slowMs, fastMs;   // speedInterval() endpoints (issues/0015.md section 5)
};

static const RendererSpec RENDERERS[] = {
    {"CHASE",     renderChase,     40, 2},
    {"WIPE",      renderWipe,      40, 2},
    {"SCAN",      renderScan,      25, 1},
    {"SPARKLE",   renderSparkle,   60, 5},
    {"PULSE",     renderPulse,     40, 4},
    {"STROBE",    renderStrobe,   600, 40},
    {"COLORLOOP", renderColorloop, 60, 2},
};
static const int NUM_RENDERERS = sizeof(RENDERERS) / sizeof(RENDERERS[0]);

// Builds a fresh, zero-initialized EffectState wired to caller-owned
// leds[]/sparkleVal[] buffers, GATE_PALETTE, colorChangeEnabled on, and the
// given per-frame inputs. Every #0015 gate below constructs its states this
// way so the only thing that varies between gates is what's being swept.
static EffectState makeGateState(CRGB* leds, uint8_t* sparkleVal, int numLeds,
                                  uint8_t speed, uint8_t intensity,
                                  uint32_t startNow, uint32_t (*rng)(uint32_t, uint32_t)) {
    EffectState s = {};
    s.leds = leds;
    s.numLeds = numLeds;
    s.ledsPerColor = 1;
    s.colors = GATE_PALETTE;
    s.colorCount = GATE_COLOR_COUNT;
    s.hueIndex = 0;
    s.colorChangeEnabled = true;
    s.brightness = TEST_BRIGHTNESS;
    s.maxBrightness = TEST_MAX_BRIGHTNESS;
    s.rng = rng;
    s.speed = speed;
    s.intensity = intensity;
    s.sparkleVal = sparkleVal;
    s.effectTimeout = startNow;
    s.effectPhase = 0;
    s.effectSub = 0;
    s.effectDir = 1;
    s.lastMode = EFFECT_BLEND;
    return s;
}

// ---- Gate 2: bounds / no overrun (highest value) -------------------------
// For each renderer, allocate CRGB buf[numLeds + 8], fill the 8-LED tail
// with a sentinel, run 600 frames at a 5ms step, and assert the tail is
// never touched. Sweeps numLeds x intensity x speed per issues/0015.md
// section 6 -- this is what catches an off-by-one in paintRun()'s wrap, a
// width that exceeds the strip at intensity=255, or a SCAN bounce that
// steps one past either end. Message formatting is deferred to the failure
// path (TEST_FAIL_MESSAGE) rather than built on every one of the ~453,600
// frame checks, which would otherwise dominate the runtime.
void test_bounds_no_overrun(void) {
    static const int NUM_LEDS_CASES[] = {1, 2, 7, 64, 240, 500};
    static const uint8_t INTENSITY_CASES[] = {0, 1, 127, 128, 254, 255};
    static const uint8_t SPEED_CASES[] = {0, 128, 255};
    const int TAIL = 8;
    const int FRAMES = 600;
    const uint32_t STEP = 5;
    static CRGB buf[500 + 8];
    static uint8_t sparkleBuf[500];
    const CRGB sentinel(77, 88, 99);

    for (int r = 0; r < NUM_RENDERERS; r++) {
        for (unsigned ni = 0; ni < sizeof(NUM_LEDS_CASES) / sizeof(NUM_LEDS_CASES[0]); ni++) {
            int numLeds = NUM_LEDS_CASES[ni];
            for (unsigned ii = 0; ii < sizeof(INTENSITY_CASES) / sizeof(INTENSITY_CASES[0]); ii++) {
                uint8_t intensity = INTENSITY_CASES[ii];
                for (unsigned si = 0; si < sizeof(SPEED_CASES) / sizeof(SPEED_CASES[0]); si++) {
                    uint8_t speed = SPEED_CASES[si];

                    for (int i = 0; i < numLeds + TAIL; i++) buf[i] = sentinel;
                    for (int i = 0; i < numLeds; i++) sparkleBuf[i] = 0;

                    rngStateA = RNG_SEED;
                    EffectState s = makeGateState(buf, sparkleBuf, numLeds, speed, intensity, 1000u, rngA);

                    uint32_t now = 1000u;
                    for (int f = 0; f < FRAMES; f++) {
                        RENDERERS[r].fn(s, now);

                        bool tailOk = true;
                        for (int t = 0; t < TAIL; t++) {
                            if (!(buf[numLeds + t] == sentinel)) { tailOk = false; break; }
                        }
                        if (!tailOk) {
                            char msg[160];
                            snprintf(msg, sizeof(msg),
                                     "%s numLeds=%d intensity=%d speed=%d frame=%d: "
                                     "tail sentinel overwritten (overrun)",
                                     RENDERERS[r].name, numLeds, intensity, speed, f);
                            TEST_FAIL_MESSAGE(msg);
                        }
                        now += STEP;
                    }
                }
            }
        }
    }
}

// Degenerate-input guard: colorCount==0 and numLeds==0 must return false and
// write nothing (issues/0015.md section 1.7 -- THEME_CUSTOM with
// colorCount==0 is unreachable today but issue #0016 can produce it, and
// `% 0` is a hard fault, not a glitch).
void test_degenerate_guards_return_false(void) {
    CRGB buf[8];
    const CRGB sentinel(77, 88, 99);
    for (int i = 0; i < 8; i++) buf[i] = sentinel;
    uint8_t sparkleBuf[8] = {0};

    for (int r = 0; r < NUM_RENDERERS; r++) {
        EffectState s1 = makeGateState(buf, sparkleBuf, 4, 128, 128, 1000u, rngA);
        s1.colorCount = 0;
        bool drew1 = RENDERERS[r].fn(s1, 1000u);
        char msg[128];
        snprintf(msg, sizeof(msg), "%s: colorCount==0 must return false and write nothing", RENDERERS[r].name);
        TEST_ASSERT_FALSE_MESSAGE(drew1, msg);
        for (int i = 0; i < 8; i++) TEST_ASSERT_TRUE_MESSAGE(buf[i] == sentinel, msg);

        EffectState s2 = makeGateState(buf, sparkleBuf, 0, 128, 128, 1000u, rngA);
        bool drew2 = RENDERERS[r].fn(s2, 1000u);
        snprintf(msg, sizeof(msg), "%s: numLeds==0 must return false and write nothing", RENDERERS[r].name);
        TEST_ASSERT_FALSE_MESSAGE(drew2, msg);
        for (int i = 0; i < 8; i++) TEST_ASSERT_TRUE_MESSAGE(buf[i] == sentinel, msg);
    }
}

// ---- Gate 3: rollover (issue #0008) --------------------------------------
// For each renderer, start 3000ms before the 2^32 millis() wrap and run
// 1200 frames at a 10ms step (the wrap lands at frame 300). Assert the
// renderer returns true at least once in every window of
// 3 x expectedIntervalAt128 / step frames across the whole run. A renderer
// written with `s.effectTimeout < now` goes permanently silent after the
// wrap -- this is the single most important gate in the suite, and it is
// designed to be able to fail (see the deliberate-perturbation proof in
// issues/0015.md's Verification).
void test_rollover_all_seven(void) {
    static CRGB leds[TEST_NUM_LEDS];
    static uint8_t sparkleBuf[TEST_NUM_LEDS];
    const int FRAMES = 1200;
    const uint32_t STEP = 10;

    for (int r = 0; r < NUM_RENDERERS; r++) {
        uint32_t startNow = (uint32_t)(0u - 3000u); // wrap lands at frame 300
        rngStateA = RNG_SEED;
        EffectState s = makeGateState(leds, sparkleBuf, TEST_NUM_LEDS, 128, 128, startNow, rngA);

        uint16_t expectedIntervalAt128 = speedInterval(128, RENDERERS[r].slowMs, RENDERERS[r].fastMs);
        int window = (3 * (int)expectedIntervalAt128) / (int)STEP;
        if (window < 3) window = 3;

        uint32_t now = startNow;
        int sinceTrue = 0;
        int maxGap = 0;
        for (int f = 0; f < FRAMES; f++) {
            bool drew = RENDERERS[r].fn(s, now);
            if (drew) {
                if (sinceTrue > maxGap) maxGap = sinceTrue;
                sinceTrue = 0;
            } else {
                sinceTrue++;
            }
            now += STEP;
        }
        if (sinceTrue > maxGap) maxGap = sinceTrue;

        if (maxGap > window) {
            char msg[192];
            snprintf(msg, sizeof(msg),
                     "%s: max gap between true returns = %d frames (window budget %d) across the "
                     "2^32 wrap -- looks like a stranded deadline (issue #0008)",
                     RENDERERS[r].name, maxGap, window);
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

// ---- Gate 4: determinism -------------------------------------------------
// Two independent runs of each renderer with the same seed and inputs must
// produce identical frame sequences. Catches an un-injected RNG,
// uninitialized state, or anything reading a real clock.
void test_determinism_all_seven(void) {
    static const int N = 64;
    static CRGB ledsA[N], ledsB[N];
    static uint8_t sparkleA[N], sparkleB[N];
    const int FRAMES = 300;
    const uint32_t STEP = 15;

    for (int r = 0; r < NUM_RENDERERS; r++) {
        rngStateA = RNG_SEED;
        rngStateB = RNG_SEED;
        EffectState a = makeGateState(ledsA, sparkleA, N, 128, 128, 1000u, rngA);
        EffectState b = makeGateState(ledsB, sparkleB, N, 128, 128, 1000u, rngB);

        uint32_t now = 1000u;
        for (int f = 0; f < FRAMES; f++) {
            bool drewA = RENDERERS[r].fn(a, now);
            bool drewB = RENDERERS[r].fn(b, now);

            char msg[128];
            snprintf(msg, sizeof(msg), "%s frame %d: drew mismatch (a=%d b=%d)", RENDERERS[r].name, f, drewA, drewB);
            TEST_ASSERT_EQUAL_MESSAGE(drewA, drewB, msg);

            snprintf(msg, sizeof(msg), "%s frame %d: leds[] diverged between two identically-seeded runs",
                     RENDERERS[r].name, f);
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(ledsA, ledsB, sizeof(CRGB) * N, msg);

            now += STEP;
        }
    }
}

// ---- Gate 5: monotonicity -------------------------------------------------
// At numLeds=240, speed=128, sweep intensity 0->255 in steps of 15 and
// assert the per-mode metric from issues/0015.md section 6 is monotone,
// plus one shared speed test (below): the number of true returns in a fixed
// window is non-decreasing in speed, for all seven.

static const uint8_t GATE5_INTENSITIES[] = {
    0, 15, 30, 45, 60, 75, 90, 105, 120, 135, 150, 165, 180, 195, 210, 225, 240, 255
};
static const int GATE5_N = sizeof(GATE5_INTENSITIES) / sizeof(GATE5_INTENSITIES[0]);
static const uint8_t GATE5_BASE = TEST_BRIGHTNESS < TEST_MAX_BRIGHTNESS ? TEST_BRIGHTNESS : TEST_MAX_BRIGHTNESS;

static int countLit(const CRGB* leds, int n) {
    int c = 0;
    for (int i = 0; i < n; i++) if (leds[i].b > 0) c++;
    return c;
}

// CHASE/SCAN: settled lit-LED count after 600 frames, increases in intensity.
void test_monotonic_chase_scan(void) {
    static CRGB leds[TEST_NUM_LEDS];
    static uint8_t sparkleBuf[TEST_NUM_LEDS];
    int prevChase = -1, prevScan = -1;

    for (int k = 0; k < GATE5_N; k++) {
        uint8_t intensity = GATE5_INTENSITIES[k];

        rngStateA = RNG_SEED;
        EffectState sc = makeGateState(leds, sparkleBuf, TEST_NUM_LEDS, 128, intensity, 1000u, rngA);
        uint32_t now = 1000u;
        for (int f = 0; f < 600; f++) { renderChase(sc, now); now += 10; }
        int chaseLit = countLit(leds, TEST_NUM_LEDS);

        rngStateA = RNG_SEED;
        EffectState ss = makeGateState(leds, sparkleBuf, TEST_NUM_LEDS, 128, intensity, 1000u, rngA);
        now = 1000u;
        for (int f = 0; f < 600; f++) { renderScan(ss, now); now += 10; }
        int scanLit = countLit(leds, TEST_NUM_LEDS);

        char msg[128];
        snprintf(msg, sizeof(msg), "CHASE lit count not non-decreasing at intensity=%d (prev=%d now=%d)",
                 intensity, prevChase, chaseLit);
        TEST_ASSERT_TRUE_MESSAGE(chaseLit >= prevChase, msg);
        snprintf(msg, sizeof(msg), "SCAN lit count not non-decreasing at intensity=%d (prev=%d now=%d)",
                 intensity, prevScan, scanLit);
        TEST_ASSERT_TRUE_MESSAGE(scanLit >= prevScan, msg);

        prevChase = chaseLit;
        prevScan = scanLit;
    }
}

// WIPE: value of the still-"ahead" region (last LED, sampled a few ticks in,
// well before the wipe front can reach it), decreases in intensity.
void test_monotonic_wipe(void) {
    static CRGB leds[TEST_NUM_LEDS];
    static uint8_t sparkleBuf[TEST_NUM_LEDS];
    int prevAhead = 256;

    for (int k = 0; k < GATE5_N; k++) {
        uint8_t intensity = GATE5_INTENSITIES[k];
        rngStateA = RNG_SEED;
        EffectState s = makeGateState(leds, sparkleBuf, TEST_NUM_LEDS, 128, intensity, 1000u, rngA);
        uint32_t now = 1000u;
        for (int f = 0; f < 10; f++) { renderWipe(s, now); now += 10; }
        int ahead = leds[TEST_NUM_LEDS - 1].b;

        char msg[128];
        snprintf(msg, sizeof(msg), "WIPE ahead-region value not non-increasing at intensity=%d (prev=%d now=%d)",
                 intensity, prevAhead, ahead);
        TEST_ASSERT_TRUE_MESSAGE(ahead <= prevAhead, msg);
        prevAhead = ahead;
    }
}

// SPARKLE: mean lit-LED count over 600 frames, increases in intensity.
void test_monotonic_sparkle(void) {
    static CRGB leds[TEST_NUM_LEDS];
    static uint8_t sparkleBuf[TEST_NUM_LEDS];
    double prevMean = -1;

    for (int k = 0; k < GATE5_N; k++) {
        uint8_t intensity = GATE5_INTENSITIES[k];
        rngStateA = RNG_SEED;
        EffectState s = makeGateState(leds, sparkleBuf, TEST_NUM_LEDS, 128, intensity, 1000u, rngA);
        uint32_t now = 1000u;
        double sum = 0;
        for (int f = 0; f < 600; f++) {
            renderSparkle(s, now);
            sum += countLit(leds, TEST_NUM_LEDS);
            now += 10;
        }
        double mean = sum / 600.0;

        char msg[128];
        snprintf(msg, sizeof(msg), "SPARKLE mean lit count not non-decreasing at intensity=%d (prev=%.2f now=%.2f)",
                 intensity, prevMean, mean);
        TEST_ASSERT_TRUE_MESSAGE(mean >= prevMean, msg);
        prevMean = mean;
    }
}

// PULSE: minimum value over several breaths decreases in intensity; peak
// stays exactly `base` at every intensity.
void test_monotonic_pulse(void) {
    static CRGB leds[TEST_NUM_LEDS];
    static uint8_t sparkleBuf[TEST_NUM_LEDS];
    int prevMin = 256;

    for (int k = 0; k < GATE5_N; k++) {
        uint8_t intensity = GATE5_INTENSITIES[k];
        rngStateA = RNG_SEED;
        EffectState s = makeGateState(leds, sparkleBuf, TEST_NUM_LEDS, 128, intensity, 1000u, rngA);
        uint32_t now = 1000u;
        uint8_t minV = 255, maxV = 0;
        for (int f = 0; f < 2000; f++) {  // several full 256-tick breaths
            renderPulse(s, now);
            if (leds[0].b < minV) minV = leds[0].b;
            if (leds[0].b > maxV) maxV = leds[0].b;
            now += 5;
        }

        char msg[128];
        snprintf(msg, sizeof(msg), "PULSE min value not non-increasing at intensity=%d (prev=%d now=%d)",
                 intensity, prevMin, minV);
        TEST_ASSERT_TRUE_MESSAGE((int)minV <= prevMin, msg);
        snprintf(msg, sizeof(msg), "PULSE peak not exactly base at intensity=%d (base=%d peak=%d)",
                 intensity, GATE5_BASE, maxV);
        TEST_ASSERT_EQUAL_MESSAGE(GATE5_BASE, maxV, msg);

        prevMin = minV;
    }
}

// STROBE: fraction of simulated milliseconds spent lit, increases in
// intensity, and lands within +-5% of 0.50 at intensity=128.
static double strobeOnFraction(uint8_t intensity) {
    static CRGB leds[TEST_NUM_LEDS];
    static uint8_t sparkleBuf[TEST_NUM_LEDS];
    rngStateA = RNG_SEED;
    EffectState s = makeGateState(leds, sparkleBuf, TEST_NUM_LEDS, 128, intensity, 1000u, rngA);
    const uint32_t STEP = 2;
    const int FRAMES = 1000; // 2000ms simulated -- several periods at speed=128 (~319ms)
    uint32_t now = 1000u;
    long litMs = 0;
    for (int f = 0; f < FRAMES; f++) {
        renderStrobe(s, now);
        if (leds[0].b > 0) litMs += STEP;
        now += STEP;
    }
    return (double)litMs / (double)(FRAMES * STEP);
}

void test_monotonic_strobe(void) {
    double prevFrac = -1;
    for (int k = 0; k < GATE5_N; k++) {
        double frac = strobeOnFraction(GATE5_INTENSITIES[k]);
        char msg[128];
        snprintf(msg, sizeof(msg), "STROBE on-fraction not non-decreasing at intensity=%d (prev=%.3f now=%.3f)",
                 GATE5_INTENSITIES[k], prevFrac, frac);
        TEST_ASSERT_TRUE_MESSAGE(frac >= prevFrac, msg);
        prevFrac = frac;
    }
}

void test_strobe_duty_cycle_at_default_intensity(void) {
    double frac = strobeOnFraction(128);
    char msg[128];
    snprintf(msg, sizeof(msg), "STROBE on-fraction at intensity=128 is %.3f, want 0.50 +-5%%", frac);
    TEST_ASSERT_TRUE_MESSAGE(frac >= 0.45 && frac <= 0.55, msg);
}

// COLORLOOP: the saturation byte increases in intensity (sat = 128 +
// intensity/2, painted unconditionally every frame -- one frame is enough).
void test_monotonic_colorloop_saturation(void) {
    static CRGB leds[TEST_NUM_LEDS];
    static uint8_t sparkleBuf[TEST_NUM_LEDS];
    int prevSat = -1;

    for (int k = 0; k < GATE5_N; k++) {
        uint8_t intensity = GATE5_INTENSITIES[k];
        rngStateA = RNG_SEED;
        EffectState s = makeGateState(leds, sparkleBuf, TEST_NUM_LEDS, 128, intensity, 1000u, rngA);
        renderColorloop(s, 1000u);
        int sat = leds[0].g;

        char msg[128];
        snprintf(msg, sizeof(msg), "COLORLOOP saturation not non-decreasing at intensity=%d (prev=%d now=%d)",
                 intensity, prevSat, sat);
        TEST_ASSERT_TRUE_MESSAGE(sat >= prevSat, msg);
        prevSat = sat;
    }
}

// Shared speed test: the number of true returns in a fixed 10s window is
// non-decreasing in speed, for all seven.
void test_speed_monotonic_all_seven(void) {
    static const uint8_t SPEEDS[] = {0, 64, 128, 192, 255};
    const int NS = sizeof(SPEEDS) / sizeof(SPEEDS[0]);
    const uint32_t STEP = 5;
    const int FRAMES = 10000 / STEP; // 10s simulated window
    static CRGB leds[TEST_NUM_LEDS];
    static uint8_t sparkleBuf[TEST_NUM_LEDS];

    for (int r = 0; r < NUM_RENDERERS; r++) {
        int prevCount = -1;
        for (int si = 0; si < NS; si++) {
            rngStateA = RNG_SEED;
            EffectState s = makeGateState(leds, sparkleBuf, TEST_NUM_LEDS, SPEEDS[si], 128, 1000u, rngA);
            uint32_t now = 1000u;
            int count = 0;
            for (int f = 0; f < FRAMES; f++) {
                if (RENDERERS[r].fn(s, now)) count++;
                now += STEP;
            }
            char msg[128];
            snprintf(msg, sizeof(msg), "%s: true-count not non-decreasing in speed at speed=%d (prev=%d now=%d)",
                     RENDERERS[r].name, SPEEDS[si], prevCount, count);
            TEST_ASSERT_TRUE_MESSAGE(count >= prevCount, msg);
            prevCount = count;
        }
    }
}

// ---- Gate 6: palette coverage --------------------------------------------
// Over 6000 frames (60s simulated) with colorChangeEnabled, assert (a)
// every hue written is a member of the active palette -- except COLORLOOP,
// which walks between entries, where the assertion is that it *reaches*
// every palette entry -- and (b) every palette entry is used at least once.
// Catches a hueIndex advance that never fires, wraps wrong, or indexes past
// the array. Uses a smaller numLeds (12, still >= GATE_COLOR_COUNT) than
// the other gates purely to keep the per-LED-per-frame scan affordable;
// every renderer's hueIndex-advance logic is numLeds-independent.
static bool hueInPalette(uint8_t hue) {
    for (int i = 0; i < GATE_COLOR_COUNT; i++) if (GATE_PALETTE[i].h == hue) return true;
    return false;
}

void test_palette_coverage_all_seven(void) {
    const int GATE6_NUM_LEDS = 12;
    const int FRAMES = 6000;
    const uint32_t STEP = 10; // 60s simulated
    static CRGB leds[TEST_NUM_LEDS];
    static uint8_t sparkleBuf[TEST_NUM_LEDS];

    for (int r = 0; r < NUM_RENDERERS; r++) {
        bool isColorloop = (strcmp(RENDERERS[r].name, "COLORLOOP") == 0);
        rngStateA = RNG_SEED;
        EffectState s = makeGateState(leds, sparkleBuf, GATE6_NUM_LEDS, 128, 128, 1000u, rngA);

        bool entryUsed[GATE_COLOR_COUNT] = {false, false, false};
        uint32_t now = 1000u;

        for (int f = 0; f < FRAMES; f++) {
            RENDERERS[r].fn(s, now);

            for (int i = 0; i < GATE6_NUM_LEDS; i++) {
                uint8_t hue = leds[i].r;
                if (!isColorloop && !hueInPalette(hue)) {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "%s frame %d LED %d: hue %d not a member of the active palette",
                             RENDERERS[r].name, f, i, hue);
                    TEST_FAIL_MESSAGE(msg);
                }
                for (int p = 0; p < GATE_COLOR_COUNT; p++) {
                    if (GATE_PALETTE[p].h == hue) entryUsed[p] = true;
                }
            }
            now += STEP;
        }

        for (int p = 0; p < GATE_COLOR_COUNT; p++) {
            if (!entryUsed[p]) {
                char msg[128];
                snprintf(msg, sizeof(msg), "%s: palette entry %d (hue=%d) never used/reached over %d frames",
                         RENDERERS[r].name, p, GATE_PALETTE[p].h, FRAMES);
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
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

    // Issue #0015, Gates 1-6 (see issues/0015.md section 6).
    RUN_TEST(test_builtin_effect_mode_table);
    RUN_TEST(test_degenerate_guards_return_false);
    RUN_TEST(test_bounds_no_overrun);
    RUN_TEST(test_rollover_all_seven);
    RUN_TEST(test_determinism_all_seven);
    RUN_TEST(test_monotonic_chase_scan);
    RUN_TEST(test_monotonic_wipe);
    RUN_TEST(test_monotonic_sparkle);
    RUN_TEST(test_monotonic_pulse);
    RUN_TEST(test_monotonic_strobe);
    RUN_TEST(test_strobe_duty_cycle_at_default_intensity);
    RUN_TEST(test_monotonic_colorloop_saturation);
    RUN_TEST(test_speed_monotonic_all_seven);
    RUN_TEST(test_palette_coverage_all_seven);

    return UNITY_END();
}
