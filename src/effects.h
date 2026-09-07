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
    EFFECT_COLORLOOP = 8,
    // Issue #0021: four renderers added to serve looks the first nine could
    // only approximate. Appended, never inserted -- customEffect.mode is
    // persisted to NVS as an integer, so renumbering an existing value would
    // silently change what a device comes back as after a reboot.
    EFFECT_NEON = 9,
    EFFECT_RAIN = 10,
    EFFECT_TRAIL = 11,
    EFFECT_STACK = 12
};

// Issue #0016: the wire/name form of EffectMode, for SET_EFFECT's "mode"
// field and NVS-mismatch log lines. `static const char* const`, not a bare
// `const char* NAMES[]` -- src/themes.h's THEME_NAMES[] had exactly this bug
// flagged in #0014's round-2 review (external linkage -> duplicate-symbol
// link error the moment a second TU in one test binary includes this
// header). `static` gives internal linkage.
static const char* const EFFECT_MODE_NAMES[] = {
    "BLEND", "FLICKER", "CHASE", "WIPE", "SCAN",
    "SPARKLE", "PULSE", "STROBE", "COLORLOOP",
    "NEON", "RAIN", "TRAIL", "STACK"
};
static_assert(sizeof(EFFECT_MODE_NAMES) / sizeof(EFFECT_MODE_NAMES[0])
              == EFFECT_STACK + 1,
              "one name per EffectMode");

#define MAX_CUSTOM_COLORS 8

// How many drops RAIN tracks at once. Eight is enough to read as rain on a
// 240-LED run and costs 32 bytes of EffectState; intensity selects how many of
// them are actually in play.
#define RAIN_STREAMS 8

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
// Exact integer RGB->HSV (issue #0019). FastLED's rgb2hsv_approximate() is
// fast and explicitly inexact: it lost ~40% of the saturation on every input
// we measured, which turned a three-shade pink palette into white sparkles on
// a real strip (#FFB6C1 came out s=40 against a true 73, rendering a pale
// grey). This runs once per SET_EFFECT, never per frame, so the approximation
// bought nothing that mattered and cost the whole point of #0013's palette
// widening.
//
// Standard max/min formulation in FastLED's 0-255 hue space. No floating
// point: hue is computed as a 0-1530 sixth-sector value and scaled down, which
// keeps the division exact enough that the three pinks above land on their
// true hues. Pure integer math with no FastLED dependency, so the native test
// suite links the real implementation rather than a copy.
//
// This does NOT make the round trip lossless -- assigning a CHSV goes through
// hsv2rgb_rainbow(), a deliberately non-linear "rainbow" mapping rather than a
// colorimetric one. It removes the avoidable half of the error.
inline PaletteColor rgbToPaletteColor(uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t maxc = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
    const uint8_t minc = (r < g) ? ((r < b) ? r : b) : ((g < b) ? g : b);
    const uint8_t delta = (uint8_t)(maxc - minc);

    const uint8_t v = maxc;
    if (delta == 0 || maxc == 0) {
        return { 0, 0, v };   // achromatic: grey/white/black, hue is meaningless
    }
    const uint8_t sat = (uint8_t)(((uint32_t)delta * 255) / maxc);

    // Sixth-sector hue in 0..1529, then scaled into FastLED's 0..255 circle.
    uint16_t h6;
    if (maxc == r)      h6 = (uint16_t)((  0 + ((int32_t)(g - b) * 255) / delta) + 1530) % 1530;
    else if (maxc == g) h6 = (uint16_t)(( 510 + ((int32_t)(b - r) * 255) / delta));
    else                h6 = (uint16_t)((1020 + ((int32_t)(r - g) * 255) / delta));

    const uint8_t hue = (uint8_t)(((uint32_t)h6 * 256) / 1530);
    return { hue, sat, v };
}

inline void refreshCustomPalette() {
    for (uint8_t i = 0; i < customEffect.colorCount && i < MAX_CUSTOM_COLORS; i++) {
        const CRGB& c = customEffect.colors[i];
        customPalette[i] = rgbToPaletteColor(c.r, c.g, c.b);
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

    // ---- issue #0021.
    // effectAux is a second position counter, uint16_t where effectSub is a
    // byte: STACK's settled height has to count LEDs, and MAX_LEDS is 500.
    uint16_t   effectAux;

    // RAIN's streams. Fixed-size and tiny (32 bytes) rather than another
    // MAX_LEDS array: rain is defined by a handful of independent drops, not
    // by per-LED state, and deriving the tail from distance-to-head means the
    // renderer never has to store what it painted. rainRate all-zero is the
    // "not seeded yet" signal, which is also what resetEffectState() restores.
    uint16_t   rainPos[RAIN_STREAMS];   // head position in 1/16 LED
    uint8_t    rainRate[RAIN_STREAMS];  // advance per tick, 1/16 LED, 0 = unseeded
    uint8_t    rainLen[RAIN_STREAMS];   // tail length in LEDs
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
    s.effectAux = 0;
    // Zeroing rainRate is what makes renderRain() reseed: a mode change has to
    // give it a fresh set of drops rather than resuming ones whose positions
    // were computed for a different strip length or intensity.
    for (int i = 0; i < RAIN_STREAMS; i++) {
        s.rainPos[i] = 0;
        s.rainRate[i] = 0;
        s.rainLen[i] = 0;
    }
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
//
// It was the only renderer that read neither `speed` nor `intensity`: the
// 500-750ms redraw window, the 120-255 brightness range and the 2000ms colour
// rotation were all constants, so a Flicker effect was the same effect however
// it was configured, and two Flicker presets could differ only in colour.
//
// Both knobs are live now, and the mapping is built so that 128 -- the value
// main.cpp passes for every built-in theme, EFFECT_DEFAULT_PARAM -- reproduces
// those constants exactly. That is not a nicety: renderFlicker() draws the
// FLICKER themes, and test_effects.cpp diffs it frame-for-frame against the
// frozen legacyFlicker(). Identity at 128 is what keeps issue #0014's
// compatibility promise while giving the effect path something to turn.
//
// Speed: scales the redraw window, 875-1312ms @0 through 500-750ms @128 to
// 128-193ms @255 -- a guttering candle at one end, a failing neon tube at the
// other. The colour rotation scales with it, so a fast flicker also changes
// colour quickly.
// Intensity: depth. The brightness floor runs from 254 @0 (no visible flicker
// at all) through 120 @128 to 0 @255 (LEDs drop right out and come back).
inline bool renderFlicker(EffectState& s, uint32_t now) {
    bool didChange = false;

    // 256 is unity, and speed 128 lands on it exactly: (128*3)/2 == 192.
    uint32_t scale = 448 - ((uint32_t)s.speed * 3) / 2;
    uint32_t loMs = (500u * scale) / 256u;
    uint32_t hiMs = (750u * scale) / 256u;

    // 255 - (128*135)/128 == 120, the constant this replaces. Capped at 254
    // rather than 255 because s.rng is a half-open range: lo == hi divides by
    // zero in the native harness and returns nothing useful on device.
    int floorV = 255 - ((int)s.intensity * 135) / 128;
    if (floorV < 0) floorV = 0;
    if (floorV > 254) floorV = 254;

    for (int i = 0; i < s.numLeds; i++) {
        if (timeReached(now, s.timeouts[i])) {
            didChange = true;
            uint32_t delay = s.rng(loMs, hiMs);
            s.timeouts[i] = now + delay;
            uint8_t flicker = (uint8_t)s.rng((uint32_t)floorV, 255);
            uint8_t hue = s.colors[s.hueIndex].h;
            s.leds[i] = CHSV(hue, 255, flicker);
        }
    }

    // Auto color change within current theme.
    if (s.colorChangeEnabled && timeReached(now, s.hueTimeout)) {
        s.hueIndex++;
        s.hueTimeout = now + (2000u * scale) / 256u;
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

// Paints `width` LEDs of colour `c` at value `val`, starting at `start`, plus
// `tail` more behind it fading linearly to nothing.
//
// wrap=true wraps the run around the strip (CHASE); wrap=false clamps to
// [0, numLeds) and silently drops any part of the run that would fall
// outside it (SCAN, whose bounce logic can otherwise push a wide band past
// either end). Shared by CHASE and SCAN -- the two renderers differ only in
// wrap-vs-bounce and their width ceiling, so this is the ~400 bytes the plan
// flags as easiest to waste by not sharing it.
//
// The run travels toward higher indices, so `start` is its trailing edge and
// the tail is painted downward from start-1. SCAN passes tail=0: a uniform
// band is what keeps it distinct from CHASE by motion rather than shading,
// which is a deliberate choice rather than an omission.
inline void paintRun(EffectState& s, int start, int width, PaletteColor c, uint8_t val,
                     bool wrap, int tail) {
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
    for (int k = 1; k <= tail; k++) {
        int idx = start - k;
        if (wrap) {
            idx = idx % s.numLeds;
            if (idx < 0) idx += s.numLeds;
        } else if (idx < 0 || idx >= s.numLeds) {
            continue;
        }
        uint8_t v = (uint8_t)(((uint32_t)val * (uint32_t)(tail - k + 1)) / (uint32_t)(tail + 1));
        s.leds[idx] = CHSV(c.h, c.s, v);
    }
}

// CHASE -- one run of `width` lit LEDs per palette colour, evenly spaced,
// travelling around the strip together, each trailing a fading tail.
//
// It used to paint a single run and change its colour once per lap, so a
// four-colour palette meant watching one colour cross, then the next: the
// palette read as a sequence in time rather than as a thing on the strip.
// Painting one run per colour costs no new parameter -- the palette already
// says how many runs there are -- and turns four ghost colours into four
// ghosts nose to tail, two cycle colours into two cycles racing.
//
// A single-colour palette is exactly the old behaviour plus the tail.
//
// Speed: slowMs=40, fastMs=2 (@128 -> 21ms/tick; ~5.0s per lap at 240 LEDs).
// Intensity: run width, 1 LED @0 up to a ceiling that is numLeds/4 for one run
// and three quarters of the run spacing for several, so the runs cannot grow
// into each other. Tail is half the width, clipped to the gap so a tail never
// reaches the run behind it. effectPhase is the head index; hueIndex still
// advances per lap, which now rotates which colour leads rather than swapping
// the only colour there is.
inline bool renderChase(EffectState& s, uint32_t now) {
    if (s.numLeds <= 0 || s.colorCount <= 0 || !s.colors) return false;
    if (!timeReached(now, s.effectTimeout)) return false;
    s.effectTimeout = now + speedInterval(s.speed, 40, 2);

    uint8_t base = (uint8_t)min(s.maxBrightness, s.brightness);

    // Eight colours on a 10-LED strip cannot be eight runs. Falling back to
    // one run keeps the renderer honest on short strips rather than painting
    // a solid bar and calling it a chase.
    int runs = s.colorCount;
    int spacing = s.numLeds / runs;
    if (spacing < 2) {
        runs = 1;
        spacing = s.numLeds;
    }

    int widthCeil = max(1, s.numLeds / 4);
    if (runs > 1) {
        int perRun = max(1, (spacing * 3) / 4);
        if (widthCeil > perRun) widthCeil = perRun;
    }
    int width = 1 + ((uint32_t)s.intensity * (widthCeil - 1)) / 255;

    int tail = width / 2;
    int gap = spacing - width;
    if (tail > gap - 1) tail = gap - 1;
    if (tail < 0) tail = 0;

    PaletteColor lead = currentPaletteColor(s);
    for (int i = 0; i < s.numLeds; i++) s.leds[i] = CHSV(lead.h, lead.s, 0);

    for (int r = 0; r < runs; r++) {
        PaletteColor c = s.colors[(s.hueIndex + r) % s.colorCount];
        paintRun(s, (int)s.effectPhase + r * spacing, width, c, base, /*wrap=*/true, tail);
    }

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
    // Quartered from (25, 1) after bench testing on a 10-LED strip: the
    // original range assumed ~240 LEDs, where a sweep is 240 steps. On 10
    // LEDs even speed=0 crossed the strip in ~250 ms, far too fast to read
    // as a scanner. Long strips get a correspondingly slower sweep -- worth
    // re-checking at 240 LEDs.
    s.effectTimeout = now + speedInterval(s.speed, 100, 4);

    uint8_t base = (uint8_t)min(s.maxBrightness, s.brightness);
    PaletteColor c = currentPaletteColor(s);
    // widthCeil floors at 3 so `intensity` is a live control on short strips.
    // numLeds/8 alone gives ceil==1 for anything under 16 LEDs, which pins
    // width to 1 for every intensity value -- confirmed on a 10-LED strip via
    // EFFECT_TRACE, where the scanner showed exactly one lit LED at intensity
    // 0, 128 and 255 alike, so it had no band and no tail at any setting.
    // Capped at numLeds/2 so a 1- or 2-LED strip cannot ask for a run wider
    // than itself. Behaviour at 60 and 240 LEDs is unchanged.
    int widthCeil = max(3, s.numLeds / 8);
    int halfStrip = max(1, s.numLeds / 2);
    if (widthCeil > halfStrip) widthCeil = halfStrip;
    int width = 1 + ((uint32_t)s.intensity * (widthCeil - 1)) / 255;

    for (int i = 0; i < s.numLeds; i++) s.leds[i] = CHSV(c.h, c.s, 0);
    paintRun(s, s.effectPhase, width, c, base, /*wrap=*/false, /*tail=*/0);

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
        uint8_t dist = (fwd <= bwd) ? fwd : bwd;
        // Issue #0018: this used to advance a fixed +/-1 per tick, which made
        // the time to reach a colour depend on how far apart the palette
        // entries happened to be -- 80-128 hue units is typical for
        // well-separated colours, so a single transition took 80-128 ticks and
        // a 3-colour palette took ~30 s at default speed. It read as a static
        // colour, which is the one thing this mode must not do. `speed` only
        // moved the tick interval, so it could not fix the rate: the step size
        // was the wrong knob left at 1.
        //
        // Step now scales with speed (1 at speed 0, 12 at speed 255), so speed
        // controls the perceived rate rather than only the frame cadence. At
        // the default speed 128 that is a step of 6 every ~31 ms, crossing a
        // 128-unit gap in about 0.7 s. Snapping when dist <= step keeps the
        // walk from stepping past the target and oscillating around it.
        uint8_t step = (uint8_t)(1 + ((uint32_t)s.speed * 11) / 255);
        if (dist <= step) {
            hue = target;
        } else {
            hue = (fwd <= bwd) ? (uint8_t)(hue + step) : (uint8_t)(hue - step);
        }
        s.effectSub = hue;
    }
    // Issue #0018: COLORLOOP deliberately ignores colorChangeEnabled, unlike
    // every other renderer. Elsewhere that flag means "should the palette
    // advance"; here advancing the palette IS the effect, so honouring it left
    // the hue parked on the first entry forever -- a colour loop showing one
    // colour. Turning colour change off now dims/holds the other modes as
    // before and leaves this one looping.
    if (hue == target) {
        s.effectPhase = (s.effectPhase + 1) % s.colorCount;
        s.hueIndex = s.effectPhase;
    }

    for (int i = 0; i < s.numLeds; i++) s.leds[i] = CHSV(hue, sat, base);
    return true;
}

// =======================================================================
// Issue #0021: four renderers for looks the first nine could only
// approximate. Each states which existing renderer it is not, because that
// is the only reason any of them earns its flash.
// =======================================================================

// NEON -- FLICKER, but the palette is laid out by LED position instead of
// applying one hue to the whole strip at a time.
//
// That single difference is the whole mode. renderFlicker() reads
// colors[hueIndex] once and paints every LED that hue, so a three-colour
// palette is the whole strip blue, then the whole strip cyan, then the whole
// strip red. A neon sign is not like that: the blue tube and the red tube are
// lit at once, in different places, each failing on its own schedule. Indexing
// by position (the thing SPARKLE already does) is what makes them coexist.
//
// It is a separate mode rather than a flag on FLICKER because renderFlicker()
// draws the built-in FLICKER themes and is diffed frame-for-frame against a
// frozen copy of its pre-refactor self; changing how it indexes the palette
// would break that promise for every theme.
//
// Speed and intensity map exactly as FLICKER's do -- same window, same depth,
// same identity at 128 -- so a look tuned on one transfers to the other.
inline bool renderNeon(EffectState& s, uint32_t now) {
    if (s.numLeds <= 0 || s.colorCount <= 0 || !s.colors) return false;
    if (!s.timeouts) return false;
    bool didChange = false;

    uint32_t scale = 448 - ((uint32_t)s.speed * 3) / 2;
    uint32_t loMs = (500u * scale) / 256u;
    uint32_t hiMs = (750u * scale) / 256u;
    int floorV = 255 - ((int)s.intensity * 135) / 128;
    if (floorV < 0) floorV = 0;
    if (floorV > 254) floorV = 254;

    uint8_t base = (uint8_t)min(s.maxBrightness, s.brightness);

    for (int i = 0; i < s.numLeds; i++) {
        if (timeReached(now, s.timeouts[i])) {
            didChange = true;
            s.timeouts[i] = now + s.rng(loMs, hiMs);
            uint8_t flicker = (uint8_t)s.rng((uint32_t)floorV, 255);
            PaletteColor c = s.colors[i % s.colorCount];
            s.leds[i] = CHSV(c.h, c.s, (uint8_t)(((uint32_t)flicker * base) / 255));
        }
    }
    return didChange;
}

// RAIN -- independent drops falling at their own rates, each a bright head
// over a tail that fades behind it.
//
// CHASE with several runs looks like rain from a distance and stops looking
// like it the moment you watch one: its runs are the same length, the same
// brightness and exactly in step, because they are one animation drawn several
// times. Rain is the opposite -- what the eye reads is the *unevenness*. So
// every drop here carries its own rate and its own length, drawn once when it
// spawns and redrawn when it wraps, which is the whole difference.
//
// Positions are 1/16 of an LED so a drop can move slower than one LED per
// tick; without the fraction every drop would round to the same integer speed
// and the mode would collapse back into CHASE.
//
// Speed: slowMs=50, fastMs=3. Intensity: how many of the RAIN_STREAMS drops
// are in play, 1 @0 to all 8 @255 -- density, as it is in SPARKLE.
inline bool renderRain(EffectState& s, uint32_t now) {
    if (s.numLeds <= 0 || s.colorCount <= 0 || !s.colors) return false;
    if (!timeReached(now, s.effectTimeout)) return false;
    s.effectTimeout = now + speedInterval(s.speed, 50, 3);

    uint8_t base = (uint8_t)min(s.maxBrightness, s.brightness);
    int active = 1 + ((uint32_t)s.intensity * (RAIN_STREAMS - 1)) / 255;

    // Seeded on first use and after any mode change, never at construction:
    // the strip length is not known until the renderer runs.
    for (int k = 0; k < active; k++) {
        if (s.rainRate[k] == 0) {
            s.rainPos[k] = (uint16_t)(s.rng(0, (uint32_t)s.numLeds) * 16u);
            s.rainRate[k] = (uint8_t)s.rng(6, 26);   // 0.4 to 1.6 LEDs per tick
            s.rainLen[k] = (uint8_t)s.rng(3, 12);
        }
    }

    for (int i = 0; i < s.numLeds; i++) s.leds[i] = CHSV(0, 0, 0);

    for (int k = 0; k < active; k++) {
        int head = (int)(s.rainPos[k] / 16);
        int len = (int)s.rainLen[k];
        PaletteColor c = s.colors[k % s.colorCount];

        for (int t = 0; t <= len; t++) {
            int idx = head - t;
            if (idx < 0 || idx >= s.numLeds) continue;
            // The head is the brightest thing on the strip and the tail falls
            // away linearly behind it.
            uint8_t v = (uint8_t)(((uint32_t)base * (uint32_t)(len - t + 1)) / (uint32_t)(len + 1));
            // Painted unconditionally rather than "brightest wins": on device
            // CRGB holds converted RGB, so there is no value byte to compare
            // without a second per-LED array. Drops overlap rarely and briefly,
            // and the later one simply wins.
            s.leds[idx] = CHSV(c.h, c.s, v);
        }

        s.rainPos[k] = (uint16_t)(s.rainPos[k] + s.rainRate[k]);
        // Off the end, tail and all: respawn above the top with a new rate and
        // length, so no drop ever repeats the one before it.
        if ((int)(s.rainPos[k] / 16) - len > s.numLeds) {
            s.rainPos[k] = 0;
            s.rainRate[k] = (uint8_t)s.rng(6, 26);
            s.rainLen[k] = (uint8_t)s.rng(3, 12);
        }
    }
    return true;
}

// TRAIL -- runs that leave a wall behind them, filling the strip over a lap
// and clearing at the end of it.
//
// CHASE's tail fades out within a few LEDs, which is right for something
// passing through and wrong for a light cycle: the point of a cycle is that
// what it has driven through stays lit and becomes an obstacle. Here the wall
// is dim rather than absent, it extends all the way back to where the run
// started this lap, and it survives until the lap wraps.
//
// One run per palette colour, spaced like CHASE, so each colour fills its own
// segment and the strip arrives full just as the lap ends.
//
// Speed: slowMs=40, fastMs=2, one LED per tick as CHASE. Intensity: the width
// of the bright head, 1 LED @0 to a quarter of the run spacing @255.
inline bool renderTrail(EffectState& s, uint32_t now) {
    if (s.numLeds <= 0 || s.colorCount <= 0 || !s.colors) return false;
    if (!timeReached(now, s.effectTimeout)) return false;
    s.effectTimeout = now + speedInterval(s.speed, 40, 2);

    uint8_t base = (uint8_t)min(s.maxBrightness, s.brightness);

    int runs = s.colorCount;
    int spacing = s.numLeds / runs;
    if (spacing < 2) {
        runs = 1;
        spacing = s.numLeds;
    }

    int widthCeil = max(1, spacing / 4);
    int width = 1 + ((uint32_t)s.intensity * (widthCeil - 1)) / 255;
    // A quarter brightness reads as "already been here" next to a head at
    // full: bright enough to be a wall, dim enough not to be the cycle.
    uint8_t wallV = (uint8_t)(base / 4);

    for (int i = 0; i < s.numLeds; i++) s.leds[i] = CHSV(0, 0, 0);

    int covered = (int)s.effectPhase;
    if (covered > spacing) covered = spacing;

    for (int r = 0; r < runs; r++) {
        PaletteColor c = s.colors[(s.hueIndex + r) % s.colorCount];
        int start = r * spacing;
        for (int k = 0; k < covered; k++) {
            int idx = (start + k) % s.numLeds;
            s.leds[idx] = CHSV(c.h, c.s, wallV);
        }
        paintRun(s, start + covered - width, width, c, base, /*wrap=*/true, /*tail=*/0);
    }

    s.effectPhase++;
    if ((int)s.effectPhase > spacing) {
        s.effectPhase = 0;
        if (s.colorChangeEnabled) s.hueIndex = (s.hueIndex + 1) % s.colorCount;
    }
    return true;
}

// STACK -- pieces fall from one end, settle at the other, and the strip clears
// once it is full.
//
// SPARKLE with a seven-colour palette is colourful twinkling that happens to
// use recognisable colours; nothing falls and nothing accumulates, which is
// the entire idea being referenced. This is the smallest renderer that
// actually has the idea in it.
//
// Nothing is stored per LED. The settled region is the last effectAux LEDs,
// and the colour of a settled LED is derived from how far up the stack it sits
// -- piece number = distance / pieceLen -- so the strip itself is the record
// of what landed, and a mode change resets it by zeroing one counter.
//
// Speed: slowMs=60, fastMs=4, one LED of fall per tick. Intensity: piece
// length, 1 LED @0 to an eighth of the strip @255.
inline bool renderStack(EffectState& s, uint32_t now) {
    if (s.numLeds <= 0 || s.colorCount <= 0 || !s.colors) return false;
    if (!timeReached(now, s.effectTimeout)) return false;
    s.effectTimeout = now + speedInterval(s.speed, 60, 4);

    uint8_t base = (uint8_t)min(s.maxBrightness, s.brightness);

    int pieceCeil = max(1, s.numLeds / 8);
    int pieceLen = 1 + ((uint32_t)s.intensity * (pieceCeil - 1)) / 255;
    int settled = (int)s.effectAux;

    for (int i = 0; i < s.numLeds; i++) s.leds[i] = CHSV(0, 0, 0);

    // The settled stack, growing from the far end back toward the source.
    for (int d = 0; d < settled && d < s.numLeds; d++) {
        int idx = s.numLeds - 1 - d;
        PaletteColor c = s.colors[(d / pieceLen) % s.colorCount];
        s.leds[idx] = CHSV(c.h, c.s, base);
    }

    // The piece currently falling, coloured as the one it is about to become.
    int landing = s.numLeds - settled - pieceLen;
    int head = (int)s.effectPhase;
    if (head > landing) head = landing;
    if (landing >= 0) {
        PaletteColor c = s.colors[(settled / pieceLen) % s.colorCount];
        for (int k = 0; k < pieceLen; k++) {
            int idx = head + k;
            if (idx >= 0 && idx < s.numLeds) s.leds[idx] = CHSV(c.h, c.s, base);
        }
    }

    if (head >= landing) {
        s.effectPhase = 0;
        s.effectAux = (uint16_t)(settled + pieceLen);
        // Full: clear and start again, which is the only way this mode ends.
        if ((int)s.effectAux + pieceLen > s.numLeds) s.effectAux = 0;
    } else {
        s.effectPhase++;
    }
    return true;
}
