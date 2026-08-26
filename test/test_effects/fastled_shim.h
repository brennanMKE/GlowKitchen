#pragma once

// Host stand-ins for the handful of FastLED names src/effects.h and
// src/themes.h need (issue #0014's frame-capture diff harness). This file
// exists to satisfy the include-order contract documented at the top of
// src/effects.h: that header uses CRGB, CHSV, sin8, min and max without
// including <FastLED.h> itself, on the assumption that whoever includes it
// has already brought those names into scope. On device, <FastLED.h> does
// that (main.cpp, line 2); here, this shim does, and MUST be included
// before "effects.h"/"themes.h" in any test file that uses them.
//
// CRGB stores the raw triple and operator=(const CHSV&) stores {h, s, v}
// WITHOUT converting to RGB. That's deliberate: the store is injective, so
// byte-equality of two shim buffers is exactly equivalence of the CHSV
// arguments the two paths computed -- nobody has to reimplement FastLED's
// real hsv2rgb_rainbow to make the frame diff meaningful. Any deterministic
// sin8 makes the diff valid too (both paths call the same one); this one is
// not table-matched to FastLED's real sin8_C and does not need to be -- see
// issue #0014's plan, section 4.

#include <stdint.h>
#include <cmath>

struct CHSV {
    uint8_t hue, sat, val;
    CHSV() : hue(0), sat(0), val(0) {}
    CHSV(uint8_t h, uint8_t s, uint8_t v) : hue(h), sat(s), val(v) {}
};

struct CRGB {
    uint8_t r, g, b;
    CRGB() : r(0), g(0), b(0) {}
    CRGB(uint8_t r_, uint8_t g_, uint8_t b_) : r(r_), g(g_), b(b_) {}

    // Injective, non-color-accurate on purpose -- see file header comment.
    CRGB& operator=(const CHSV& hsv) {
        r = hsv.hue;
        g = hsv.sat;
        b = hsv.val;
        return *this;
    }

    bool operator==(const CRGB& o) const {
        return r == o.r && g == o.g && b == o.b;
    }
};

// Not exact HSV math, and doesn't need to be: this phase's renderers never
// actually read customHues (the THEME_CUSTOM boot guard keeps that branch
// unreachable -- see src/effects.h), so this only needs to exist for
// refreshCustomHues() to type-check on a host build, not to be exercised.
inline CHSV rgb2hsv_approximate(const CRGB& rgb) {
    return CHSV(rgb.r, rgb.g, rgb.b);
}

// Deterministic stand-in for FastLED's table-based sin8. theta wraps at 256
// exactly like the real one (uint8_t parameter), so `sin8(now/15 + x*40)`
// behaves identically to the device build bit-for-bit in its wraparound,
// even though the waveform itself isn't FastLED's real lookup table.
inline uint8_t sin8(uint8_t theta) {
    double rad = (theta / 256.0) * 2.0 * M_PI;
    double v = (std::sin(rad) + 1.0) * 127.5;
    if (v < 0.0) v = 0.0;
    if (v > 255.0) v = 255.0;
    return (uint8_t)(v + 0.5);
}

template <typename T> inline T min(T a, T b) { return a < b ? a : b; }
template <typename T> inline T max(T a, T b) { return a > b ? a : b; }
