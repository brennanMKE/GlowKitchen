// Native tests for the living looks (src/looks.h): the six built-in themes'
// renderers and the cross-fade between looks.
//
// The shim's CRGB stores the CHSV it was assigned as {h, s, v} rather than
// converting to RGB (see test_effects/fastled_shim.h), so r/g/b below read as
// hue/saturation/value for anything a renderer painted.

#include <unity.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "../test_effects/fastled_shim.h"
#include "effects.h"
#include "looks.h"

void setUp(void) {}
void tearDown(void) {}

static uint32_t rngState;
static uint32_t rng(uint32_t lo, uint32_t hi) {
    uint32_t x = rngState;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rngState = x;
    return lo + (x % (hi - lo));
}

typedef bool (*RenderFn)(EffectState&, uint32_t);
struct LookSpec { const char* name; RenderFn fn; EffectMode mode; };
static const LookSpec LOOKS[] = {
    {"CANDLE",  renderCandle,  EFFECT_CANDLE},
    {"FLOW",    renderFlow,    EFFECT_FLOW},
    {"GLITTER", renderGlitter, EFFECT_GLITTER},
    {"WAVES",   renderWaves,   EFFECT_WAVES},
    {"EMBER",   renderEmber,   EFFECT_EMBER},
    {"CANOPY",  renderCanopy,  EFFECT_CANOPY},
};
static const int NUM_LOOKS = sizeof(LOOKS) / sizeof(LOOKS[0]);

static const int MAXN = 500;
static const int TAIL = 8;
static CRGB leds[MAXN + TAIL];
static uint32_t timeouts[MAXN + TAIL];
static uint8_t lvl[MAXN + TAIL], tgt[MAXN + TAIL], aux[MAXN + TAIL];
static const int BRIGHT = 180;

static EffectState makeState(int numLeds, int ledsPerColor, const PaletteColor* pal, int count,
                             uint8_t speed, uint8_t intensity, uint32_t now) {
    for (int i = 0; i < MAXN + TAIL; i++) {
        leds[i] = CRGB(77, 88, 99);
        timeouts[i] = now;
        lvl[i] = tgt[i] = aux[i] = 0;
    }
    for (int i = numLeds; i < numLeds + TAIL; i++) { lvl[i] = tgt[i] = aux[i] = 0xA5; timeouts[i] = 0xDEADBEEF; }
    EffectState s = {};
    s.leds = leds;
    s.numLeds = numLeds;
    s.ledsPerColor = ledsPerColor;
    s.colors = pal;
    s.colorCount = count;
    s.colorChangeEnabled = true;
    s.brightness = BRIGHT;
    s.maxBrightness = 255;
    s.rng = rng;
    s.speed = speed;
    s.intensity = intensity;
    s.timeouts = timeouts;
    s.ledLevel = lvl;
    s.ledTarget = tgt;
    s.ledAux = aux;
    resetEffectState(s, now);
    return s;
}

static const PaletteColor* themePalette(int theme, int& count) {
    switch (theme) {
        case THEME_GREEN:       count = NUM_GREEN_HUES;     return GREEN_HUES;
        case THEME_RAINBOW:     count = NUM_RAINBOW_HUES;   return RAINBOW_HUES;
        case THEME_PINK_PONY:   count = NUM_PINK_PONY_HUES; return PINK_PONY_HUES;
        case THEME_OCEAN_WAVES: count = NUM_OCEAN_HUES;     return OCEAN_HUES;
        case THEME_SUNSET:      count = NUM_SUNSET_HUES;    return SUNSET_HUES;
        default:                count = NUM_FOREST_HUES;    return FOREST_HUES;
    }
}

// ---- Mapping and validity ------------------------------------------------

void test_builtin_themes_use_living_looks(void) {
    TEST_ASSERT_EQUAL(EFFECT_CANDLE,  BUILTIN_EFFECT_MODE[THEME_GREEN]);
    TEST_ASSERT_EQUAL(EFFECT_FLOW,    BUILTIN_EFFECT_MODE[THEME_RAINBOW]);
    TEST_ASSERT_EQUAL(EFFECT_GLITTER, BUILTIN_EFFECT_MODE[THEME_PINK_PONY]);
    TEST_ASSERT_EQUAL(EFFECT_WAVES,   BUILTIN_EFFECT_MODE[THEME_OCEAN_WAVES]);
    TEST_ASSERT_EQUAL(EFFECT_EMBER,   BUILTIN_EFFECT_MODE[THEME_SUNSET]);
    TEST_ASSERT_EQUAL(EFFECT_CANOPY,  BUILTIN_EFFECT_MODE[THEME_FOREST]);
}

void test_mode_names_cover_new_modes(void) {
    TEST_ASSERT_EQUAL_STRING("CANDLE", EFFECT_MODE_NAMES[EFFECT_CANDLE]);
    TEST_ASSERT_EQUAL_STRING("CANOPY", EFFECT_MODE_NAMES[EFFECT_CANOPY]);
}

// Was `mode > EFFECT_COLORLOOP`: NEON/RAIN/TRAIL/STACK custom effects were
// thrown away on the boot-time NVS load.
void test_custom_effect_valid_for_every_mode(void) {
    CustomEffectConfig c;
    resetCustomEffect(c);
    c.colorCount = 1;
    for (int m = 0; m <= EFFECT_LAST_MODE; m++) {
        c.mode = (EffectMode)m;
        TEST_ASSERT_TRUE_MESSAGE(customEffectIsValid(c), EFFECT_MODE_NAMES[m]);
    }
    c.mode = (EffectMode)(EFFECT_LAST_MODE + 1);
    TEST_ASSERT_FALSE(customEffectIsValid(c));
}

// ---- Palettes --------------------------------------------------------------

// The old Sunset table ran into yellow-green (hue 64-85).
void test_sunset_palette_has_no_green(void) {
    for (int i = 0; i < NUM_SUNSET_HUES; i++) {
        uint8_t h = SUNSET_HUES[i].h;
        TEST_ASSERT_TRUE_MESSAGE(h <= 48 || h >= 185, "sunset hue in the green/blue range");
    }
}

// The old Forest table ran into aqua (up to 145).
void test_forest_palette_stays_green(void) {
    for (int i = 0; i < NUM_FOREST_HUES; i++) {
        TEST_ASSERT_TRUE(FOREST_HUES[i].h >= 64 && FOREST_HUES[i].h <= 112);
    }
}

// ---- Helpers ---------------------------------------------------------------

void test_ease_endpoints_and_monotonic(void) {
    TEST_ASSERT_EQUAL_UINT8(0, fxEaseInOut(0));
    TEST_ASSERT_EQUAL_UINT8(255, fxEaseInOut(255));
    for (int x = 1; x < 256; x++) TEST_ASSERT_TRUE(fxEaseInOut((uint8_t)x) >= fxEaseInOut((uint8_t)(x - 1)));
}

void test_hue_lerp_takes_short_way(void) {
    // 250 -> 10 is 16 steps forward through red, not 240 back through green.
    uint8_t mid = fxHueLerp(250, 10, 128);
    TEST_ASSERT_TRUE(mid >= 250 || mid <= 10);
    TEST_ASSERT_EQUAL_UINT8(250, fxHueLerp(250, 10, 0));
    TEST_ASSERT_EQUAL_UINT8(10, fxHueLerp(250, 10, 255));
}

void test_time_units_no_overflow_jump(void) {
    // A plain t * 256 / 2000 wraps at 16.7M ms; this must keep climbing.
    uint32_t a = fxTimeUnits(16777000u, 2000);
    uint32_t b = fxTimeUnits(16778000u, 2000);
    TEST_ASSERT_TRUE(b > a);
    TEST_ASSERT_UINT32_WITHIN(1, 128, b - a);
}

void test_palette_ramp_ends(void) {
    PaletteColor first = fxPaletteRamp(SUNSET_HUES, NUM_SUNSET_HUES, 0);
    PaletteColor last = fxPaletteRamp(SUNSET_HUES, NUM_SUNSET_HUES, 255);
    TEST_ASSERT_EQUAL_UINT8(SUNSET_HUES[0].h, first.h);
    // 255 lands within the last segment, near its end, never wrapping to entry 0.
    int d = (int8_t)(uint8_t)(last.h - SUNSET_HUES[NUM_SUNSET_HUES - 1].h);
    TEST_ASSERT_TRUE(d >= -6 && d <= 0);
}

// ---- Renderers -------------------------------------------------------------

// Every look, across strip lengths, groupings and both knobs' extremes: never
// writes past numLeds in leds[] or in any per-LED array.
void test_looks_never_overrun(void) {
    static const int NS[] = {1, 2, 7, 10, 64, 240, 500};
    static const int LPC[] = {1, 3, 25};
    static const uint8_t KNOB[] = {0, 128, 255};
    const CRGB sentinel(77, 88, 99);
    for (int l = 0; l < NUM_LOOKS; l++)
    for (unsigned ni = 0; ni < sizeof(NS) / sizeof(NS[0]); ni++)
    for (unsigned li = 0; li < sizeof(LPC) / sizeof(LPC[0]); li++)
    for (unsigned si = 0; si < 3; si++)
    for (unsigned ii = 0; ii < 3; ii++) {
        int n = NS[ni];
        rngState = 0x12345678u;
        EffectState s = makeState(n, LPC[li], RAINBOW_HUES, NUM_RAINBOW_HUES, KNOB[si], KNOB[ii], 1000);
        uint32_t now = 1000;
        for (int f = 0; f < 150; f++, now += 7) LOOKS[l].fn(s, now);
        for (int t = n; t < n + TAIL; t++) {
            bool ok = leds[t] == sentinel && lvl[t] == 0xA5 && tgt[t] == 0xA5 && aux[t] == 0xA5
                      && timeouts[t] == 0xDEADBEEF;
            if (!ok) {
                char msg[128];
                snprintf(msg, sizeof(msg), "%s n=%d lpc=%d speed=%d int=%d overran",
                         LOOKS[l].name, n, LPC[li], KNOB[si], KNOB[ii]);
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
}

void test_looks_degenerate_inputs_draw_nothing(void) {
    const CRGB sentinel(77, 88, 99);
    for (int l = 0; l < NUM_LOOKS; l++) {
        EffectState s = makeState(10, 3, RAINBOW_HUES, 0, 128, 128, 1000);  // colorCount 0
        TEST_ASSERT_FALSE_MESSAGE(LOOKS[l].fn(s, 1000), LOOKS[l].name);
        s = makeState(0, 3, RAINBOW_HUES, NUM_RAINBOW_HUES, 128, 128, 1000);
        TEST_ASSERT_FALSE_MESSAGE(LOOKS[l].fn(s, 1000), LOOKS[l].name);
        s = makeState(10, 0, nullptr, NUM_RAINBOW_HUES, 128, 128, 1000);
        TEST_ASSERT_FALSE_MESSAGE(LOOKS[l].fn(s, 1000), LOOKS[l].name);
        TEST_ASSERT_TRUE(leds[0] == sentinel);
    }
    // The three that keep per-LED state refuse to run without it.
    EffectState s = makeState(10, 3, GREEN_HUES, NUM_GREEN_HUES, 128, 128, 1000);
    s.ledLevel = nullptr;
    TEST_ASSERT_FALSE(renderCandle(s, 1000));
    TEST_ASSERT_FALSE(renderEmber(s, 1000));
    TEST_ASSERT_FALSE(renderGlitter(s, 1000));
}

// One frame per LOOK_FRAME_MS, and every frame repaints every LED (the
// cross-fade and mirror rely on that).
void test_looks_frame_gate_and_full_repaint(void) {
    const CRGB sentinel(77, 88, 99);
    for (int l = 0; l < NUM_LOOKS; l++) {
        rngState = 1;
        EffectState s = makeState(10, 3, RAINBOW_HUES, NUM_RAINBOW_HUES, 128, 128, 1000);
        TEST_ASSERT_TRUE_MESSAGE(LOOKS[l].fn(s, 1000), LOOKS[l].name);
        for (int i = 0; i < 10; i++) TEST_ASSERT_FALSE_MESSAGE(leds[i] == sentinel, LOOKS[l].name);
        TEST_ASSERT_FALSE_MESSAGE(LOOKS[l].fn(s, 1000 + LOOK_FRAME_MS - 1), LOOKS[l].name);
        TEST_ASSERT_TRUE_MESSAGE(LOOKS[l].fn(s, 1000 + LOOK_FRAME_MS), LOOKS[l].name);
    }
}

// The configured brightness caps every look: value never exceeds it.
void test_looks_respect_brightness(void) {
    for (int theme = 0; theme < CYCLEABLE_THEME_COUNT; theme++) {
        int count; const PaletteColor* pal = themePalette(theme, count);
        rngState = 99;
        EffectState s = makeState(60, 5, pal, count, 128, 255, 1000);
        RenderFn fn = LOOKS[theme].fn;
        for (uint32_t now = 1000; now < 6000; now += LOOK_FRAME_MS) {
            fn(s, now);
            for (int i = 0; i < 60; i++) {
                if (leds[i].b > BRIGHT) TEST_FAIL_MESSAGE(LOOKS[theme].name);
            }
        }
    }
}

// "More lively" made checkable: on a 10-LED strip at the built-in settings,
// every look keeps changing. Over 5 s, sampled every 250 ms, most samples
// differ from the one before in most LEDs.
void test_looks_keep_moving(void) {
    for (int theme = 0; theme < CYCLEABLE_THEME_COUNT; theme++) {
        int count; const PaletteColor* pal = themePalette(theme, count);
        rngState = 7;
        EffectState s = makeState(10, 3, pal, count, 128, 128, 1000);
        CRGB prev[10];
        int changedSamples = 0, samples = 0;
        uint32_t now = 1000;
        for (int k = 0; k < 20; k++) {
            for (int f = 0; f < 250 / (int)LOOK_FRAME_MS; f++, now += LOOK_FRAME_MS) LOOKS[theme].fn(s, now);
            if (k > 0) {
                int changed = 0;
                for (int i = 0; i < 10; i++) if (!(leds[i] == prev[i])) changed++;
                if (changed >= 5) changedSamples++;
                samples++;
            }
            for (int i = 0; i < 10; i++) prev[i] = leds[i];
        }
        char msg[64];
        snprintf(msg, sizeof(msg), "%s changed in %d/%d samples", LOOKS[theme].name, changedSamples, samples);
        TEST_ASSERT_TRUE_MESSAGE(changedSamples >= samples * 3 / 4, msg);
    }
}

// Same seed, same frames.
void test_looks_deterministic(void) {
    for (int l = 0; l < NUM_LOOKS; l++) {
        CRGB first[40];
        for (int run = 0; run < 2; run++) {
            rngState = 4242;
            EffectState s = makeState(40, 4, PINK_PONY_HUES, NUM_PINK_PONY_HUES, 128, 128, 500);
            for (uint32_t now = 500; now < 3500; now += 13) LOOKS[l].fn(s, now);
            if (run == 0) memcpy(first, leds, sizeof(first));
            else for (int i = 0; i < 40; i++) TEST_ASSERT_TRUE_MESSAGE(first[i] == leds[i], LOOKS[l].name);
        }
    }
}

// Speed 128 is real time; colour change off holds colorT but not animT.
void test_look_clock_speed_and_color_hold(void) {
    EffectState s = makeState(10, 3, GREEN_HUES, NUM_GREEN_HUES, 128, 128, 1000);
    for (uint32_t now = 1000; now <= 11000; now += LOOK_FRAME_MS) fxLookTick(s, now);
    TEST_ASSERT_UINT32_WITHIN(LOOK_FRAME_MS * 2, 10000, s.animT);
    TEST_ASSERT_EQUAL_UINT32(s.animT, s.colorT);

    s = makeState(10, 3, GREEN_HUES, NUM_GREEN_HUES, 255, 128, 1000);
    for (uint32_t now = 1000; now <= 11000; now += LOOK_FRAME_MS) fxLookTick(s, now);
    TEST_ASSERT_TRUE(s.animT > 17000);

    s = makeState(10, 3, GREEN_HUES, NUM_GREEN_HUES, 128, 128, 1000);
    s.colorChangeEnabled = false;
    for (uint32_t now = 1000; now <= 11000; now += LOOK_FRAME_MS) fxLookTick(s, now);
    TEST_ASSERT_EQUAL_UINT32(0, s.colorT);
    TEST_ASSERT_TRUE(s.animT > 9000);
}

// FLOW's gradient holds still with colour change off: each LED keeps its hue,
// and only the ripple moves.
void test_flow_holds_colours_when_color_change_off(void) {
    EffectState s = makeState(10, 3, RAINBOW_HUES, NUM_RAINBOW_HUES, 128, 128, 1000);
    s.colorChangeEnabled = false;
    renderFlow(s, 1000);
    CRGB first[10];
    memcpy(first, leds, sizeof(first));
    for (uint32_t now = 1000; now < 9000; now += LOOK_FRAME_MS) renderFlow(s, now);
    bool valueMoved = false;
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_UINT8(first[i].r, leds[i].r);
        if (first[i].b != leds[i].b) valueMoved = true;
    }
    TEST_ASSERT_TRUE(valueMoved);
}

// CANDLE stays within a few hue steps of its palette and never goes dark for
// long: it's a candle, not a strobe.
void test_candle_stays_green_and_lit(void) {
    rngState = 31337;
    EffectState s = makeState(10, 3, GREEN_HUES, NUM_GREEN_HUES, 128, 128, 1000);
    long sum = 0; int n = 0;
    for (uint32_t now = 1000; now < 21000; now += LOOK_FRAME_MS) {
        renderCandle(s, now);
        if (now < 1500) continue;  // easing up from black
        for (int i = 0; i < 10; i++) {
            TEST_ASSERT_TRUE(leds[i].r >= 85 - 6 && leds[i].r <= 105 + 6);
            sum += leds[i].b; n++;
        }
    }
    int mean = (int)(sum / n);
    TEST_ASSERT_TRUE_MESSAGE(mean > BRIGHT / 2, "candle mean brightness too low");
}

// ---- Cross-fade --------------------------------------------------------------

// Palette entries as the strip would show them, through the same exact
// conversion the cross-fade uses.
static CRGB spectrum(const CHSV& c) { return fxSpectrumRgb(c.hue, c.sat, c.val); }

// Every random RGB survives rgbToPaletteColor() -> fxSpectrumRgb() within a
// few steps per channel. This is what keeps the fade's correction terms too
// small to grey out the middle. The floor is the hue byte itself: 256 hues
// over 1530 sector steps is about 6 RGB steps per hue step at full
// saturation and value, so 8 is the honest bound, not 2.
void test_spectrum_round_trip_is_tight(void) {
    rngState = 77;
    int worst = 0;
    for (int n = 0; n < 20000; n++) {
        CRGB a((uint8_t)rng(0, 256), (uint8_t)rng(0, 256), (uint8_t)rng(0, 256));
        PaletteColor p = rgbToPaletteColor(a.r, a.g, a.b);
        CRGB o = fxSpectrumRgb(p.h, p.s, p.v);
        int d = abs((int)o.r - a.r);
        d = d > abs((int)o.g - a.g) ? d : abs((int)o.g - a.g);
        d = d > abs((int)o.b - a.b) ? d : abs((int)o.b - a.b);
        if (d > worst) worst = d;
    }
    char msg[48];
    snprintf(msg, sizeof(msg), "worst round-trip error %d", worst);
    TEST_ASSERT_TRUE_MESSAGE(worst <= 8, msg);
}

void test_crossfade_endpoints_exact(void) {
    CRGB a(200, 10, 30), b(5, 180, 90), k(0, 0, 0), w(255, 255, 255);
    TEST_ASSERT_TRUE(fxCrossfade(a, b, 0) == a);
    TEST_ASSERT_TRUE(fxCrossfade(a, b, 255) == b);
    TEST_ASSERT_TRUE(fxCrossfade(k, w, 0) == k);
    TEST_ASSERT_TRUE(fxCrossfade(k, w, 255) == w);
}

// The bug this replaced: green to pink mixed per channel is grey at the
// middle, and the old brightness rescale turned that grey into a white flash.
// Every step must stay a saturated colour at full brightness.
void test_crossfade_green_to_pink_stays_colourful(void) {
    CRGB a(0, 200, 0), b(200, 0, 120);
    for (int f = 0; f <= 255; f++) {
        CRGB o = fxCrossfade(a, b, (uint8_t)f);
        PaletteColor p = rgbToPaletteColor(o.r, o.g, o.b);
        char msg[64];
        snprintf(msg, sizeof(msg), "f=%d rgb=(%d,%d,%d) s=%d", f, o.r, o.g, o.b, p.s);
        TEST_ASSERT_TRUE_MESSAGE(p.s >= 200, msg);
        TEST_ASSERT_TRUE_MESSAGE(fxMax3(o) >= 190, msg);
    }
}

// Every built-in palette pair that a theme switch can produce, both directions.
void test_crossfade_between_themes_never_whitens(void) {
    for (int t1 = 0; t1 < CYCLEABLE_THEME_COUNT; t1++)
    for (int t2 = 0; t2 < CYCLEABLE_THEME_COUNT; t2++) {
        int n1, n2;
        const PaletteColor* p1 = themePalette(t1, n1);
        const PaletteColor* p2 = themePalette(t2, n2);
        for (int i = 0; i < n1; i++)
        for (int j = 0; j < n2; j++) {
            CRGB a = spectrum(CHSV(p1[i].h, p1[i].s, 200));
            CRGB b = spectrum(CHSV(p2[j].h, p2[j].s, 200));
            for (int f = 0; f <= 255; f += 5) {
                CRGB o = fxCrossfade(a, b, (uint8_t)f);
                PaletteColor p = rgbToPaletteColor(o.r, o.g, o.b);
                if (p.s < 180) TEST_FAIL_MESSAGE("theme cross-fade lost its colour");
            }
        }
    }
}

// Fading up from black (switching on, boot) goes straight to the colour, not
// through red or a pastel.
void test_crossfade_from_black_keeps_target_colour(void) {
    CRGB k(0, 0, 0), b(200, 0, 120);
    PaletteColor want = rgbToPaletteColor(b.r, b.g, b.b);
    for (int f = 40; f < 255; f++) {
        CRGB o = fxCrossfade(k, b, (uint8_t)f);
        PaletteColor p = rgbToPaletteColor(o.r, o.g, o.b);
        TEST_ASSERT_UINT8_WITHIN(12, want.s, p.s);
        TEST_ASSERT_TRUE((uint8_t)(p.h - want.h + 6) <= 12);
    }
}

// No jumps: random pairs,
// each step from f to f+1 moves every channel by only a little.
void test_crossfade_is_continuous(void) {
    rngState = 2024;
    for (int n = 0; n < 2000; n++) {
        CRGB a((uint8_t)rng(0, 256), (uint8_t)rng(0, 256), (uint8_t)rng(0, 256));
        CRGB b((uint8_t)rng(0, 256), (uint8_t)rng(0, 256), (uint8_t)rng(0, 256));
        CRGB prev = a;
        for (int f = 1; f <= 255; f++) {
            CRGB o = fxCrossfade(a, b, (uint8_t)f);
            int d = abs((int)o.r - prev.r);
            d = d > abs((int)o.g - prev.g) ? d : abs((int)o.g - prev.g);
            d = d > abs((int)o.b - prev.b) ? d : abs((int)o.b - prev.b);
            if (d > 16) {
                char msg[128];
                snprintf(msg, sizeof(msg), "jump %d at f=%d: (%d,%d,%d)->(%d,%d,%d)",
                         d, f, a.r, a.g, a.b, b.r, b.g, b.b);
                TEST_FAIL_MESSAGE(msg);
            }
            prev = o;
        }
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_builtin_themes_use_living_looks);
    RUN_TEST(test_mode_names_cover_new_modes);
    RUN_TEST(test_custom_effect_valid_for_every_mode);
    RUN_TEST(test_sunset_palette_has_no_green);
    RUN_TEST(test_forest_palette_stays_green);
    RUN_TEST(test_ease_endpoints_and_monotonic);
    RUN_TEST(test_hue_lerp_takes_short_way);
    RUN_TEST(test_time_units_no_overflow_jump);
    RUN_TEST(test_palette_ramp_ends);
    RUN_TEST(test_looks_never_overrun);
    RUN_TEST(test_looks_degenerate_inputs_draw_nothing);
    RUN_TEST(test_looks_frame_gate_and_full_repaint);
    RUN_TEST(test_looks_respect_brightness);
    RUN_TEST(test_looks_keep_moving);
    RUN_TEST(test_looks_deterministic);
    RUN_TEST(test_look_clock_speed_and_color_hold);
    RUN_TEST(test_flow_holds_colours_when_color_change_off);
    RUN_TEST(test_candle_stays_green_and_lit);
    RUN_TEST(test_spectrum_round_trip_is_tight);
    RUN_TEST(test_crossfade_endpoints_exact);
    RUN_TEST(test_crossfade_green_to_pink_stays_colourful);
    RUN_TEST(test_crossfade_between_themes_never_whitens);
    RUN_TEST(test_crossfade_from_black_keeps_target_colour);
    RUN_TEST(test_crossfade_is_continuous);
    return UNITY_END();
}
