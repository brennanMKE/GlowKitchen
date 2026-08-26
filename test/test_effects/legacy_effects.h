#pragma once

// Frozen, independently-transcribed copies of pre-#0014 flickerLEDs() and
// slowBlend(), taken from `git show main:src/main.cpp` (commit a6650fe,
// before src/effects.h existed) and renamed legacyFlicker()/legacyBlend().
// Adapted ONLY at the seam -- state access via EffectState instead of
// main.cpp globals, s.rng instead of direct Arduino random() calls, no
// FastLED.show()/applyMirror() calls (the caller's job, and not under test
// here). The math inside the loops is untouched, on purpose: this file
// exists so test_effects.cpp can diff it, frame by frame, against
// renderBlend()/renderFlicker() in src/effects.h -- the actual extraction
// this issue (#0014) performed.
//
// Honest caveat (issue #0014 plan, section 4): this is a transcription, not
// a mechanically-derived copy, so the diff is only as good as the
// copy-paste. It is deliberately typed out again here rather than #included
// from src/effects.h, so a bug introduced during the real extraction has
// something independent to be caught against.
//
// Same include-order contract as src/effects.h: relies on CRGB, CHSV, sin8,
// min, max already being in scope (fastled_shim.h, included first by
// test_effects.cpp) and on EffectState/timeReached() already being declared
// (effects.h/time_utils.h, also included first).
//
// LegacyEffectState is a DELIBERATELY SEPARATE type from EffectState, not a
// type alias or a reuse of the real struct. Round 1 of this issue had
// legacyFlicker()/legacyBlend() take EffectState& directly -- the same type
// renderBlend()/renderFlicker() take -- on the theory that this was harmless
// since both paths only read/write the same field names. It was not
// harmless: a round-2 review proved that widening EffectState::blendOffset
// from uint8_t to int (the exact regression this harness exists to catch)
// still passed 18/18, because the perturbation changed the shared struct
// definition, so BOTH the new-path state and the "legacy" reference widened
// together and stayed in lockstep with each other. Giving the reference its
// own struct, with blendOffset pinned to uint8_t here regardless of what
// EffectState declares, closes that hole: a widening in src/effects.h now
// only affects the real code path, and the pinned-width reference can
// diverge from it and get caught. See issues/0014.md's Gotchas/Verification
// for the fuller story and the seeded-offset perturbation proof.
struct LegacyEffectState {
    uint32_t  blendTimeout, rotateTimeout, hueTimeout;
    uint8_t   blendOffset;          // PINNED uint8_t -- see comment above
    uint32_t* timeouts;

    CRGB*          leds;
    int            numLeds, ledsPerColor;
    const uint8_t* colors;
    int            colorCount;
    int            hueIndex;
    bool           colorChangeEnabled;
    int            brightness, maxBrightness;

    uint32_t (*rng)(uint32_t lo, uint32_t hi);
};

inline bool legacyFlicker(LegacyEffectState& s, uint32_t now) {
    bool didChange = false;

    // Update individual LED flickering
    for (int i = 0; i < s.numLeds; i++) {
      if (timeReached(now, s.timeouts[i])) {
          didChange = true;
          uint32_t delay = s.rng(500, 750);  // Slower, more candle-like flicker
          s.timeouts[i] = now + delay;
          uint8_t flicker = (uint8_t)s.rng(120, 255);
          uint8_t hue = s.colors[s.hueIndex];
          s.leds[i] = CHSV(hue, 255, flicker);
      }
    }

    // Auto color change within current theme
    if (s.colorChangeEnabled && timeReached(now, s.hueTimeout)) {
        s.hueIndex++;
        s.hueTimeout = now + 2000;
        if (s.hueIndex >= s.colorCount) {
            s.hueIndex = 0;
        }
        //saveHueToPreferences();
    }

    return didChange;
}

inline bool legacyBlend(LegacyEffectState& s, uint32_t now) {
    // Update blend animation
    if (!timeReached(now, s.blendTimeout)) return false;

    for (int i = 0; i < s.numLeds; i++) {
        // Calculate which color group this LED belongs to, with offset applied per LED
        int effectiveLedPosition = (i + s.blendOffset) % (s.numLeds * s.colorCount);
        int groupIndex = (effectiveLedPosition / s.ledsPerColor) % s.colorCount;
        int nextGroupIndex = (groupIndex + 1) % s.colorCount;

        // Get the current and next colors
        uint8_t currentHue = s.colors[groupIndex];
        uint8_t nextHue = s.colors[nextGroupIndex];

        // Calculate position within the color group (0 to ledsPerColor-1)
        int ledInGroup = effectiveLedPosition % s.ledsPerColor;

        // Calculate blend fraction (0.0 at start of group, 1.0 at end of group)
        // This creates a smooth transition across the ledsPerColor range
        float blendFraction = (float)ledInGroup / (float)s.ledsPerColor;

        // Interpolate between current and next hue using the SHORTEST path around the hue circle
        uint8_t blendedHue;

        // Calculate distances in both directions around the hue circle
        int forwardDistance = (nextHue - currentHue + 256) % 256;
        int backwardDistance = (currentHue - nextHue + 256) % 256;
        int shortestDistance = min(forwardDistance, backwardDistance);

        // Only blend if colors are close together (within 60 hue units)
        // This allows Pink Pony Club (55 unit span) to blend smoothly
        // while preventing blending across larger gaps in themes with wide hue spans
        const int BLEND_THRESHOLD = 60;

        if (shortestDistance <= BLEND_THRESHOLD) {
            // Colors are close - do smooth blending
            if (forwardDistance <= backwardDistance) {
                // Go forward (clockwise around hue circle)
                blendedHue = (currentHue + (uint8_t)(forwardDistance * blendFraction)) % 256;
            } else {
                // Go backward (counter-clockwise around hue circle)
                blendedHue = (currentHue - (uint8_t)(backwardDistance * blendFraction) + 256) % 256;
            }
        } else {
            // Colors are too far apart - don't blend, just use current color
            // This keeps themes with large hue differences separate from each other
            blendedHue = currentHue;
        }

        // Add subtle brightness variation for visual interest
        int baseBrightness = s.brightness;
        uint8_t brightness = min(s.maxBrightness, baseBrightness + sin8(now / 15 + ledInGroup * 40) / 12);

        s.leds[i] = CHSV(blendedHue, 255, brightness);
    }

    s.blendTimeout = now + 100; // blendInterval

    // Slowly rotate the color groups if color change is enabled
    if (s.colorChangeEnabled) {
        if (timeReached(now, s.rotateTimeout)) {
            s.blendOffset = (s.blendOffset + 1) % (s.numLeds * s.colorCount); // Move one LED at a time
            s.rotateTimeout = now + 500; // Move every 500ms for slower, more relaxed scrolling
        }
    }

    return true;
}
