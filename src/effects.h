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

// Issue #0016: the wire/name form of EffectMode, for SET_EFFECT's "mode"
// field and NVS-mismatch log lines. `static const char* const`, not a bare
// `const char* NAMES[]` -- src/themes.h's THEME_NAMES[] had exactly this bug
// flagged in #0014's round-2 review (external linkage -> duplicate-symbol
// link error the moment a second TU in one test binary includes this
// header). `static` gives internal linkage.
static const char* const EFFECT_MODE_NAMES[] = {
    "BLEND", "FLICKER", "CHASE", "WIPE", "SCAN",
    "SPARKLE", "PULSE", "STROBE", "COLORLOOP"
};
static_assert(sizeof(EFFECT_MODE_NAMES) / sizeof(EFFECT_MODE_NAMES[0])
              == EFFECT_COLORLOOP + 1,
              "one name per EffectMode");

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

// Issue #0016: NVS layout version for the CustomEffectConfig blob. Nothing
// has ever written a version byte before this ticket -- reading a missing
// "fx_ver" key returns 0 (Preferences' default), which is why 0 is reserved
// to mean "never written" rather than "version zero of this layout". 2
// follows the spec's suggested value; 1 is left meaning "the pre-#0016
// layout" even though that layout never actually persisted anything.
static const uint8_t SETTINGS_VERSION = 2;

// Explicit field assignment, not memset -- so the "zero-initialize on
// mismatch" behavior the ticket cares about is a named, native-testable
// function rather than an incidental property of a memset call site in
// main.cpp. speed/intensity reset to 128 (the default everywhere else in
// this tree), not 0 -- a zero-initialized config would render as a frozen
// animation, which is a confusing failure mode for a device that fell back
// after a version mismatch.
inline void resetCustomEffect(CustomEffectConfig& c) {
    c.mode = EFFECT_BLEND;
    for (uint8_t i = 0; i < MAX_CUSTOM_COLORS; i++) c.colors[i] = CRGB(0, 0, 0);
    c.colorCount = 0;
    c.speed = 128;
    c.intensity = 128;
    c.timeoutMs = 0;
    c.activatedAt = 0;
    c.revertTheme = THEME_GREEN;
}

// The validity gate applyLoadedCustomEffect() (src/effect_parse.h) runs
// against a stored blob before trusting it. revertTheme < CYCLEABLE_THEME_COUNT
// is the load-bearing check here: a stored revertTheme == THEME_CUSTOM would
// be an infinite self-revert the moment CLEAR_EFFECT or the #0017 timeout
// runs, so it is rejected outright rather than merely bounds-checked against
// THEME_COUNT.
inline bool customEffectIsValid(const CustomEffectConfig& c) {
    if (c.colorCount < 1 || c.colorCount > MAX_CUSTOM_COLORS) return false;
    if (c.mode > EFFECT_COLORLOOP) return false;
    if (c.revertTheme >= CYCLEABLE_THEME_COUNT) return false;
    if (c.timeoutMs > 28800000UL) return false;
    return true;
}

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

// Derived palette cache for THEME_CUSTOM, so getCurrentColorArray()/
// getCurrentColorCount() (in main.cpp) are genuinely wired for the custom
// slot rather than stubbed out. In this phase colorCount is always 0 (see
// CustomEffectConfig above), so this cache is never actually read through a
// reachable path -- the boot guard keeps THEME_CUSTOM from being entered at
// all until issue #0016 calls refreshCustomPalette() after parsing a real
// config.
//
// Issue #0015 widening: this used to keep only the hue byte
// (rgb2hsv_approximate(...).hue), which is exactly the "white/pastel custom
// colors are inexpressible" gap #0013 flags. Since customEffect.colors is
// already CRGB, converting the full {h,s,v} triple here costs nothing extra
// and closes that gap for whenever #0016 starts populating customEffect --
// nothing calls this yet, so it remains unexercised until then.
//
// `static`, not `extern`/`inline` -- src/effects.h is included by exactly
// one firmware translation unit (main.cpp); adding a second .cpp under src/
// that also includes this header would need customPalette moved into
// main.cpp behind an extern declaration (or declared C++17 `inline`) so both
// TUs share one definition instead of each getting its own copy.
static PaletteColor customPalette[MAX_CUSTOM_COLORS];
inline void refreshCustomPalette() {
    for (uint8_t i = 0; i < customEffect.colorCount && i < MAX_CUSTOM_COLORS; i++) {
        CHSV hsv = rgb2hsv_approximate(customEffect.colors[i]);
        customPalette[i] = { hsv.hue, hsv.sat, hsv.val };
    }
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
    //
    // colors: widened from `const uint8_t*` (a bare hue array) to
    // `const PaletteColor*` (issue #0015, per #0013's "Palette representation
    // -- CHSV triples" decision). renderBlend()/renderFlicker() below only
    // ever read `.h` off each entry -- exactly the hue byte they read before
    // -- so their painted output is unchanged; the seven new renderers below
    // use the full {h,s,v} triple.
    CRGB*               leds;
    int                 numLeds, ledsPerColor;
    const PaletteColor* colors;
    int                 colorCount;
    int            hueIndex;        // in/out -- caller copies it back afterward
    bool           colorChangeEnabled;
    int            brightness, maxBrightness;

    // Function pointer so the native test can inject a fixed-seed LCG; on
    // device this is a two-line wrapper over Arduino random(lo, hi), so the
    // shipped behavior is byte-for-byte what flickerLEDs() did before.
    uint32_t (*rng)(uint32_t lo, uint32_t hi);

    // ---- issue #0015 only. renderBlend()/renderFlicker() MUST NOT read any
    // field below this line, and test/test_effects/legacy_effects.h's
    // LegacyEffectState deliberately does not mirror them. See the plan in
    // issues/0015.md, section 3a.
    uint8_t    speed;          // per-frame input, 0-255 (128 = default)
    uint8_t    intensity;      // per-frame input, 0-255 (128 = default)
    uint32_t   effectTimeout;  // the ONE deadline the active renderer ticks off
    uint16_t   effectPhase;    // head position / cycle counter, per renderer
    uint8_t    effectSub;      // secondary counter, per renderer
    int8_t     effectDir;      // +1 / -1, SCAN only
    EffectMode lastMode;       // for the mode-change reset in loopLED()
    uint8_t*   sparkleVal;     // -> sparkleValues[MAX_LEDS], SPARKLE only
};

// Resets ONLY the #0015 fields. Must never touch blendTimeout, rotateTimeout,
// hueTimeout, blendOffset or timeouts[] -- doing so would change what BLEND
// and FLICKER do on a theme switch, i.e. regress #0014's compatibility
// promise.
inline void resetEffectState(EffectState& s, uint32_t now) {
    s.effectTimeout = now;
    s.effectPhase = 0;
    s.effectSub = 0;
    s.effectDir = 1;
    if (s.sparkleVal) for (int i = 0; i < s.numLeds; i++) s.sparkleVal[i] = 0;
}

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

        uint8_t currentHue = s.colors[groupIndex].h;
        uint8_t nextHue = s.colors[nextGroupIndex].h;

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
            uint8_t hue = s.colors[s.hueIndex].h;
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

// =======================================================================
// Issue #0015: seven new renderers (CHASE, WIPE, SCAN, SPARKLE, PULSE,
// STROBE, COLORLOOP). See issues/0015.md section 5 for the speed/intensity
// mapping tables this code implements, and the justification for each
// choice not pinned down by the spec (WIPE's contrast knob, SPARKLE's
// density divisor, COLORLOOP's saturation knob).
//
// All seven share the same skeleton: degenerate-input guard, one
// timeReached() gate on s.effectTimeout, paint, return true. None of them
// is read by renderBlend()/renderFlicker() above, and none of them touches
// blendTimeout/rotateTimeout/hueTimeout/blendOffset/timeouts[] -- see the
// #0015 field block in EffectState and resetEffectState()'s comment.
// =======================================================================

// speed 0 -> slowMs, 255 -> fastMs, linear. One helper, endpoints documented
// per renderer below -- no undocumented curve anywhere (issues/0015.md
// section 4).
inline uint16_t speedInterval(uint8_t speed, uint16_t slowMs, uint16_t fastMs) {
    return (uint16_t)(slowMs - (((uint32_t)(slowMs - fastMs) * speed) / 255));
}

// The palette entry the active theme/effect is "on" right now -- s.hueIndex
// modulo s.colorCount, per the "index the palette as s.colors[idx %
// s.colorCount], never raw" ground rule (issues/0015.md section 1.2). Reused
// by every #0015 renderer except SPARKLE (colour derived from LED index) and
// COLORLOOP (walks between two indices rather than showing one).
inline PaletteColor currentPaletteColor(EffectState& s) {
    return s.colors[s.hueIndex % s.colorCount];
}

// Paints `width` LEDs of colour `c` at value `val`, starting at `start`.
// wrap=true wraps the run around the strip (CHASE); wrap=false clamps to
// [0, numLeds) and silently drops any part of the run that would fall
// outside it (SCAN, whose bounce logic can otherwise push a wide band past
// either end). Shared by CHASE and SCAN -- the two renderers differ only in
// wrap-vs-bounce and their width ceiling, so this is the ~400 bytes the plan
// flags as easiest to waste by not sharing it.
inline void paintRun(EffectState& s, int start, int width, PaletteColor c, uint8_t val, bool wrap) {
    for (int k = 0; k < width; k++) {
        int idx = start + k;
        if (wrap) {
            idx = idx % s.numLeds;
            if (idx < 0) idx += s.numLeds;
        } else if (idx < 0 || idx >= s.numLeds) {
            continue;
        }
        s.leds[idx] = CHSV(c.h, c.s, val);
    }
}

// CHASE -- a run of `width` lit LEDs travelling around the strip.
// Speed: slowMs=40, fastMs=2 (@128 -> 21ms/tick; ~5.0s per lap at 240 LEDs).
// Intensity: run width, 1 LED @0 to numLeds/4 LEDs @255 (30 @128, 60 @255 at
// 240 LEDs). effectPhase is the head index.
inline bool renderChase(EffectState& s, uint32_t now) {
    if (s.numLeds <= 0 || s.colorCount <= 0 || !s.colors) return false;
    if (!timeReached(now, s.effectTimeout)) return false;
    s.effectTimeout = now + speedInterval(s.speed, 40, 2);

    uint8_t base = (uint8_t)min(s.maxBrightness, s.brightness);
    PaletteColor c = currentPaletteColor(s);
    int widthCeil = max(1, s.numLeds / 4);
    int width = 1 + ((uint32_t)s.intensity * (widthCeil - 1)) / 255;

    for (int i = 0; i < s.numLeds; i++) s.leds[i] = CHSV(c.h, c.s, 0);
    paintRun(s, s.effectPhase, width, c, base, /*wrap=*/true);

    s.effectPhase = (s.effectPhase + 1) % s.numLeds;
    if (s.effectPhase == 0 && s.colorChangeEnabled) {
        s.hueIndex = (s.hueIndex + 1) % s.colorCount;
    }
    return true;
}

// SCAN -- Larson/Cylon: a band of `width` LEDs sweeping back and forth.
// Speed: slowMs=25, fastMs=1 (@128 -> 13ms/tick; ~3.1s per sweep at 240
// LEDs). Intensity: band width, 1 LED @0 to numLeds/8 LEDs @255 (15 @128, 30
// @255 at 240 LEDs) -- half CHASE's ceiling, since a Cylon eye wider than an
// eighth of the strip loses the "eye". effectPhase is the eye position,
// effectDir is +-1. Deliberately no fading tail: a uniform band keeps this
// distinct from CHASE by motion rather than shading, and saves a divide per
// LED.
inline bool renderScan(EffectState& s, uint32_t now) {
    if (s.numLeds <= 0 || s.colorCount <= 0 || !s.colors) return false;
    if (!timeReached(now, s.effectTimeout)) return false;
    s.effectTimeout = now + speedInterval(s.speed, 25, 1);

    uint8_t base = (uint8_t)min(s.maxBrightness, s.brightness);
    PaletteColor c = currentPaletteColor(s);
    int widthCeil = max(1, s.numLeds / 8);
    int width = 1 + ((uint32_t)s.intensity * (widthCeil - 1)) / 255;

    for (int i = 0; i < s.numLeds; i++) s.leds[i] = CHSV(c.h, c.s, 0);
    paintRun(s, s.effectPhase, width, c, base, /*wrap=*/false);

    s.effectPhase += s.effectDir;
    if (s.effectPhase <= 0) {
        s.effectPhase = 0;
        s.effectDir = 1;
        if (s.colorChangeEnabled) s.hueIndex = (s.hueIndex + 1) % s.colorCount;
    } else if (s.effectPhase >= s.numLeds - 1) {
        s.effectPhase = s.numLeds - 1;
        s.effectDir = -1;
    }
    return true;
}

// WIPE -- colour fills progressively from one end. Speed: slowMs=40,
// fastMs=2 (@128 -> 21ms/tick; ~5.0s per full wipe at 240 LEDs). Intensity:
// contrast of the un-wiped region -- the previous colour at full brightness
// @0 (a soft recolour of the whole strip) down to black @255 (the classic
// wipe-onto-black). effectPhase is the front index; no extra state is
// needed to remember the previous colour -- it's the palette entry one slot
// behind s.hueIndex.
inline bool renderWipe(EffectState& s, uint32_t now) {
    if (s.numLeds <= 0 || s.colorCount <= 0 || !s.colors) return false;
    if (!timeReached(now, s.effectTimeout)) return false;
    s.effectTimeout = now + speedInterval(s.speed, 40, 2);

    uint8_t base = (uint8_t)min(s.maxBrightness, s.brightness);
    PaletteColor c = currentPaletteColor(s);
    PaletteColor prev = s.colors[(s.hueIndex + s.colorCount - 1) % s.colorCount];
    uint8_t vAhead = ((uint32_t)base * (255 - s.intensity)) / 255;

    for (int i = 0; i < s.numLeds; i++) {
        if (i <= s.effectPhase) {
            s.leds[i] = CHSV(c.h, c.s, base);
        } else {
            s.leds[i] = CHSV(prev.h, prev.s, vAhead);
        }
    }

    s.effectPhase++;
    if (s.effectPhase >= s.numLeds) {
        s.effectPhase = 0;
        if (s.colorChangeEnabled) s.hueIndex = (s.hueIndex + 1) % s.colorCount;
    }
    return true;
}

// PULSE -- breathing: the whole strip fades up and down in unison. Speed:
// slowMs=40, fastMs=4 (@128 -> 22ms/tick; ~5.6s per breath at effectSub's
// natural 256-step wrap). Intensity: trough depth, no visible pulse @0 down
// to a full dip to black @255 -- peak stays exactly `base` at every
// intensity. effectSub is the phase counter (wraps naturally at 256 -- one
// full breath).
inline bool renderPulse(EffectState& s, uint32_t now) {
    if (s.numLeds <= 0 || s.colorCount <= 0 || !s.colors) return false;
    if (!timeReached(now, s.effectTimeout)) return false;
    s.effectTimeout = now + speedInterval(s.speed, 40, 4);

    uint8_t base = (uint8_t)min(s.maxBrightness, s.brightness);
    PaletteColor c = currentPaletteColor(s);
    uint8_t floorV = ((uint32_t)base * (255 - s.intensity)) / 255;
    uint8_t wave = sin8(s.effectSub);
    uint8_t v = floorV + (((uint32_t)(base - floorV) * wave) / 255);

    for (int i = 0; i < s.numLeds; i++) s.leds[i] = CHSV(c.h, c.s, v);

    s.effectSub++;  // uint8_t: wraps to 0 at 256, i.e. one full breath
    if (s.effectSub == 0 && s.colorChangeEnabled) {
        s.hueIndex = (s.hueIndex + 1) % s.colorCount;
    }
    return true;
}

// STROBE -- hard on/off flashing. Period (one full on+off cycle): slowMs=600,
// fastMs=40 (@128 -> 319ms, ~3.1Hz), with a hard floor of 40ms (25Hz) --
// faster is a photosensitivity risk and is near the WS2812 refresh time for
// 240 LEDs anyway; speedInterval()'s own endpoints enforce the floor.
// Intensity: duty cycle, ~6% on @0 to ~94% on @255 (50% @128, verified
// within +-5% by issues/0015.md's Gate 5). effectSub is the on/off flag; this
// renderer sets its own deadline rather than using the shared skeleton's
// single line, since on/off phases have different durations.
inline bool renderStrobe(EffectState& s, uint32_t now) {
    if (s.numLeds <= 0 || s.colorCount <= 0 || !s.colors) return false;
    if (!timeReached(now, s.effectTimeout)) return false;

    uint8_t base = (uint8_t)min(s.maxBrightness, s.brightness);
    PaletteColor c = currentPaletteColor(s);
    uint16_t period = speedInterval(s.speed, 600, 40);
    uint32_t raw = (uint32_t)period * s.intensity / 255;
    uint32_t lo = period / 16;
    uint32_t hi = (uint32_t)period * 15 / 16;
    uint16_t onMs = (uint16_t)max(lo, min(hi, raw));

    s.effectSub = s.effectSub ? 0 : 1;
    bool on = s.effectSub != 0;
    for (int i = 0; i < s.numLeds; i++) s.leds[i] = CHSV(c.h, c.s, on ? base : 0);

    s.effectTimeout = now + (on ? onMs : (uint16_t)(period - onMs));
    if (on && s.colorChangeEnabled) {
        s.hueIndex = (s.hueIndex + 1) % s.colorCount;
    }
    return true;
}

// SPARKLE -- random LEDs pop and fade. Speed: slowMs=60, fastMs=5 (@128 ->
// 33ms/tick: one fade step + spawn). Intensity: spawns per tick, 1 @0 to 6
// @255 (3 @128). Paired with the fixed fade-by-24-per-tick below (a sparkle
// lives ~10.6 ticks): steady-state lit count is spawns x 10.6, which is the
// range that reads as "sparkle" rather than "static" or "noise" -- the
// divisor (48) and the fade step are a pair; if either changes, restate
// both. Colour is derived from the LED index (i % colorCount) rather than
// at spawn time, avoiding a second MAX_LEDS array. Uses s.rng, the injected
// function pointer, never Arduino random() directly, so the native test
// stays deterministic. Needs no deadline rebasing under SET_CLOCK_OFFSET --
// sparkleVal[] holds brightness, not deadlines.
inline bool renderSparkle(EffectState& s, uint32_t now) {
    if (s.numLeds <= 0 || s.colorCount <= 0 || !s.colors) return false;
    if (!s.sparkleVal) return false;
    if (!timeReached(now, s.effectTimeout)) return false;
    s.effectTimeout = now + speedInterval(s.speed, 60, 5);

    uint8_t base = (uint8_t)min(s.maxBrightness, s.brightness);

    // Fade all sparkles by 24, saturating at 0.
    for (int i = 0; i < s.numLeds; i++) {
        int faded = (int)s.sparkleVal[i] - 24;
        s.sparkleVal[i] = (uint8_t)max(0, faded);
    }

    // Spawn n new sparkles.
    int n = 1 + ((uint32_t)s.intensity * s.numLeds) / (255 * 48);
    for (int k = 0; k < n; k++) {
        int idx = (int)s.rng(0, (uint32_t)s.numLeds);
        if (idx >= 0 && idx < s.numLeds) s.sparkleVal[idx] = 255;
    }

    for (int i = 0; i < s.numLeds; i++) {
        PaletteColor c = s.colors[i % s.colorCount];
        uint8_t v = (uint8_t)(((uint32_t)s.sparkleVal[i] * base) / 255);
        s.leds[i] = CHSV(c.h, c.s, v);
    }
    return true;
}

// COLORLOOP -- the whole strip cycles through the palette, one hue unit at a
// time along the shortest path. Speed: slowMs=60, fastMs=2 (@128 -> 31ms/
// tick; ~1s per 32 hue units). Intensity: saturation, sat=128 (pastel) @0 to
// sat=255 (pure) @255 (sat=192 @128) -- the floor of 128 exists because a
// fully desaturated strip is white, which defeats the point of a colour
// loop. This is the weakest of the seven per issues/0015.md; drop the
// saturation knob (fix sat at 255) if flash gets tight or the reviewer
// objects. effectSub is the current hue, effectPhase the target palette
// index (uint8_t arithmetic makes "shortest path" two comparisons). Equal
// adjacent palette entries (Green's array repeats) simply arrive
// immediately and move on -- no renderBlend()-style interpolation needed.
inline bool renderColorloop(EffectState& s, uint32_t now) {
    if (s.numLeds <= 0 || s.colorCount <= 0 || !s.colors) return false;
    if (!timeReached(now, s.effectTimeout)) return false;
    s.effectTimeout = now + speedInterval(s.speed, 60, 2);

    uint8_t base = (uint8_t)min(s.maxBrightness, s.brightness);
    uint8_t sat = 128 + s.intensity / 2;
    uint8_t hue = s.effectSub;
    uint8_t target = s.colors[s.effectPhase % s.colorCount].h;

    if (hue != target) {
        uint8_t fwd = (uint8_t)(target - hue);
        uint8_t bwd = (uint8_t)(hue - target);
        hue = (fwd <= bwd) ? (uint8_t)(hue + 1) : (uint8_t)(hue - 1);
        s.effectSub = hue;
    }
    if (hue == target && s.colorChangeEnabled) {
        s.effectPhase = (s.effectPhase + 1) % s.colorCount;
        s.hueIndex = s.effectPhase;
    }

    for (int i = 0; i < s.numLeds; i++) s.leds[i] = CHSV(hue, sat, base);
    return true;
}
