#pragma once

// Effect engine (issue #0014, phase 1 of #0013's umbrella): EffectMode
// dispatch, the CustomEffectConfig slot for ad hoc user effects, and the two
// existing renderers (renderBlend()/renderFlicker()) extracted from
// main.cpp's slowBlend()/flickerLEDs() so a native test can drive them
// against a stub leds[] buffer.
//
// INCLUDE-ORDER CONTRACT -- the one genuinely fragile thing in this design:
// this header uses CRGB, CHSV, sin8, min and max WITHOUT including
// <FastLED.h> itself. Whoever includes this header must already have those
// names in scope:
//   - main.cpp: `#include <FastLED.h>` at the top of the file (line 2)
//     brings them in before `#include "effects.h"`.
//   - test/test_effects/: `fastled_shim.h`, a host-only stand-in providing
//     the same names, is included before this header.
// This keeps effects.h itself free of any Arduino/FastLED dependency, so it
// compiles cleanly on a native/host target.
//
// Header-only (every function `inline`): [env:native] does not build
// main.cpp (test_build_src is off, per platformio.ini), so a src/effects.cpp
// would not be linkable from a native test without turning on project-
// source building -- which would drag main.cpp, WiFi.h and FastLED.h into
// the host build and fail. A header needs no platformio.ini change at all:
// [env:native] already carries -I src.

#include <stdint.h>
#include "time_utils.h"   // timeReached()
#include "themes.h"       // HueTheme, THEME_CUSTOM, CYCLEABLE_THEME_COUNT

// All nine values ship now; only EFFECT_BLEND and EFFECT_FLICKER have
// renderers in this phase. The other seven land in issue #0015.
enum EffectMode {
    EFFECT_BLEND = 0,
    EFFECT_FLICKER = 1,
    EFFECT_CHASE = 2,
    EFFECT_WIPE = 3,
    EFFECT_SCAN = 4,
    EFFECT_SPARKLE = 5,
    EFFECT_PULSE = 6,
    EFFECT_STROBE = 7,
    EFFECT_COLORLOOP = 8
};

#define MAX_CUSTOM_COLORS 8

// Ad hoc effect configuration. Nothing populates this in this phase --
// that's issue #0016 (MQTT SET_EFFECT + NVS persistence). It exists now so
// the engine has a slot to dispatch through: THEME_CUSTOM is unreachable via
// NEXT_THEME/PREV_THEME cycling, and the boot guard in loadAllPreferences()
// falls back to THEME_GREEN whenever colorCount == 0 (which it always is
// right now, since customEffect is zero-initialized and never written to).
//
// uint32_t, not unsigned long, for the two time fields -- same reasoning as
// src/time_utils.h (issue #0008 type-width note): unsigned long is 64 bits
// on a native host and 32 on the C3, and these fields will be fed to
// timeReached() in issue #0017.
struct CustomEffectConfig {
    EffectMode mode;
    CRGB       colors[MAX_CUSTOM_COLORS];
    uint8_t    colorCount;
    uint8_t    speed;          // default 128
    uint8_t    intensity;      // default 128
    uint32_t   timeoutMs;      // 0 = no timeout
    uint32_t   activatedAt;
    HueTheme   revertTheme;
};

// Maps each of the 6 cycleable themes to the EffectMode that renders it.
// Green -> FLICKER (candle effect); everything else -> BLEND (gradient
// scroll). THEME_CUSTOM is deliberately absent from this table -- its mode
// comes from customEffect.mode instead, via getCurrentEffectMode() below.
static const EffectMode BUILTIN_EFFECT_MODE[CYCLEABLE_THEME_COUNT] = {
    EFFECT_FLICKER,  // THEME_GREEN
    EFFECT_BLEND,    // THEME_RAINBOW
    EFFECT_BLEND,    // THEME_PINK_PONY
    EFFECT_BLEND,    // THEME_OCEAN_WAVES
    EFFECT_BLEND,    // THEME_SUNSET
    EFFECT_BLEND     // THEME_FOREST
};
static_assert(sizeof(BUILTIN_EFFECT_MODE) / sizeof(BUILTIN_EFFECT_MODE[0])
              == CYCLEABLE_THEME_COUNT,
              "one mode per cycleable theme");

// Defined once, in main.cpp (the file-scope theme/effect state).
extern HueTheme currentTheme;
extern CustomEffectConfig customEffect;

// THEME_CUSTOM must be tested FIRST: THEME_CUSTOM == 6 == CYCLEABLE_THEME_COUNT,
// so the bounds check alone would swallow it.
inline EffectMode getCurrentEffectMode() {
    if (currentTheme == THEME_CUSTOM) return customEffect.mode;
    if ((int)currentTheme >= CYCLEABLE_THEME_COUNT) return EFFECT_BLEND; // corrupt value
    return BUILTIN_EFFECT_MODE[currentTheme];
}

// Derived hue cache for THEME_CUSTOM, so getCurrentColorArray()/
// getCurrentColorCount() (in main.cpp) are genuinely wired for the custom
// slot rather than stubbed out. In this phase colorCount is always 0 (see
// CustomEffectConfig above), so this cache is never actually read through a
// reachable path -- the boot guard keeps THEME_CUSTOM from being entered at
// all until issue #0016 calls refreshCustomHues() after parsing a real
// config.
static uint8_t customHues[MAX_CUSTOM_COLORS];
inline void refreshCustomHues() {
    for (uint8_t i = 0; i < customEffect.colorCount && i < MAX_CUSTOM_COLORS; i++)
        customHues[i] = rgb2hsv_approximate(customEffect.colors[i]).hue;
}

// Per-frame renderer state. Deliberately not "everything the render loop
// might ever want" -- just enough for renderBlend()/renderFlicker() to run
// without reaching for main.cpp globals directly, so a native test can
// construct two independent instances and diff their output.
struct EffectState {
    // Owned across frames (main.cpp keeps one file-scope `EffectState fx;`
    // and re-seeds the per-frame inputs below every call).
    uint32_t  blendTimeout, rotateTimeout, hueTimeout;
    uint8_t   blendOffset;          // MUST stay uint8_t -- see renderBlend()
    uint32_t* timeouts;             // -> timeouts[MAX_LEDS], per-LED flicker deadlines

    // Per-frame inputs, re-seeded by the caller before every render call.
    CRGB*          leds;
    int            numLeds, ledsPerColor;
    const uint8_t* colors;
    int            colorCount;
    int            hueIndex;        // in/out -- caller copies it back afterward
    bool           colorChangeEnabled;
    int            brightness, maxBrightness;

    // Function pointer so the native test can inject a fixed-seed LCG; on
    // device this is a two-line wrapper over Arduino random(lo, hi), so the
    // shipped behavior is byte-for-byte what flickerLEDs() did before.
    uint32_t (*rng)(uint32_t lo, uint32_t hi);
};

// renderBlend() -- extracted from main.cpp's slowBlend() (pre-#0014 form).
// Returns true if it drew a new frame into s.leds (the caller is then
// responsible for FastLED.show() or applyMirror(), neither of which this
// function calls itself -- see the include-order contract above; FastLED
// itself is not even visible to this header by name).
//
// blendOffset stays uint8_t: `(blendOffset + 1) % (numLeds * colorCount)`
// has a modulus of ~1920 at the default 240 LEDs / 8 hues, so the
// assignment truncates at 256 and THAT TRUNCATION IS THE SHIPPED ANIMATION.
// Widening it to int would silently change the scroll -- this is precisely
// what the frame-capture diff test (test/test_effects/) exists to catch.
inline bool renderBlend(EffectState& s, uint32_t now) {
    if (!timeReached(now, s.blendTimeout)) return false;

    for (int i = 0; i < s.numLeds; i++) {
        // Which color group this LED belongs to, with the rotating offset
        // applied per LED.
        int effectiveLedPosition = (i + s.blendOffset) % (s.numLeds * s.colorCount);
        int groupIndex = (effectiveLedPosition / s.ledsPerColor) % s.colorCount;
        int nextGroupIndex = (groupIndex + 1) % s.colorCount;

        uint8_t currentHue = s.colors[groupIndex];
        uint8_t nextHue = s.colors[nextGroupIndex];

        // Position within the color group (0 .. ledsPerColor-1).
        int ledInGroup = effectiveLedPosition % s.ledsPerColor;

        // Blend fraction: 0.0 at the start of the group, 1.0 at the end.
        float blendFraction = (float)ledInGroup / (float)s.ledsPerColor;

        // Interpolate between current and next hue using the SHORTEST path
        // around the hue circle.
        uint8_t blendedHue;
        int forwardDistance = (nextHue - currentHue + 256) % 256;
        int backwardDistance = (currentHue - nextHue + 256) % 256;
        int shortestDistance = min(forwardDistance, backwardDistance);

        // Only blend if colors are close together (within 60 hue units).
        // This allows Pink Pony Club (55 unit span) to blend smoothly while
        // preventing blending across larger gaps in themes with wide hue
        // spans.
        const int BLEND_THRESHOLD = 60;

        if (shortestDistance <= BLEND_THRESHOLD) {
            if (forwardDistance <= backwardDistance) {
                // Go forward (clockwise around hue circle).
                blendedHue = (currentHue + (uint8_t)(forwardDistance * blendFraction)) % 256;
            } else {
                // Go backward (counter-clockwise around hue circle).
                blendedHue = (currentHue - (uint8_t)(backwardDistance * blendFraction) + 256) % 256;
            }
        } else {
            // Colors are too far apart -- don't blend, just use current
            // color. Keeps themes with large hue differences separate.
            blendedHue = currentHue;
        }

        // Subtle brightness variation for visual interest. Kept as `int`
        // arithmetic and `min(int,int)` exactly as before a stray uint8_t
        // here changes the saturation behavior.
        uint8_t brightness = min(s.maxBrightness, s.brightness + sin8(now / 15 + ledInGroup * 40) / 12);

        s.leds[i] = CHSV(blendedHue, 255, brightness);
    }

    s.blendTimeout = now + 100;  // blendInterval, unchanged from main.cpp

    // Slowly rotate the color groups if color change is enabled.
    if (s.colorChangeEnabled && timeReached(now, s.rotateTimeout)) {
        s.blendOffset = (s.blendOffset + 1) % (s.numLeds * s.colorCount); // one LED at a time
        s.rotateTimeout = now + 500; // rotateInterval, unchanged from main.cpp
    }

    return true;
}

// renderFlicker() -- extracted from main.cpp's flickerLEDs() (pre-#0014
// form). s.rng replaces the two direct Arduino random(lo, hi) calls, in the
// same order, so a fixed-seed native test consumes exactly as many draws as
// the device path does.
inline bool renderFlicker(EffectState& s, uint32_t now) {
    bool didChange = false;

    for (int i = 0; i < s.numLeds; i++) {
        if (timeReached(now, s.timeouts[i])) {
            didChange = true;
            uint32_t delay = s.rng(500, 750);  // slower, more candle-like flicker
            s.timeouts[i] = now + delay;
            uint8_t flicker = (uint8_t)s.rng(120, 255);
            uint8_t hue = s.colors[s.hueIndex];
            s.leds[i] = CHSV(hue, 255, flicker);
        }
    }

    // Auto color change within current theme.
    if (s.colorChangeEnabled && timeReached(now, s.hueTimeout)) {
        s.hueIndex++;
        s.hueTimeout = now + 2000;
        if (s.hueIndex >= s.colorCount) {
            s.hueIndex = 0;
        }
    }

    return didChange;
}
