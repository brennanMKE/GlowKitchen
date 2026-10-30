#pragma once

#include <stdint.h>

// Late-night warning: on the hour, every hour from 7pm on, the strip's
// brightness swells smoothly up and back down for 15 seconds. Only the output
// brightness moves -- the running effect, its colours and its timing are left
// exactly as they were, and the configured brightness is never rewritten.
//
// Lives in its own header, free of Arduino and FastLED, so the native test
// suite (test/test_late_night_swell/) links against the real implementation.
//
// The swing is large on purpose. LED brightness reads roughly
// logarithmically, so a modest rise (150 -> 188) is barely visible; a hard
// on/off pulse is obvious but reads as flashing. A big swing that is smooth
// is noticed from across a room without ever blinking.
//
// The swing has to come from dipping, not rising. FastLED's power limit
// (setMaxPowerInVoltsAndMilliamps, 500 mA) already holds most strips at their
// ceiling at ordinary brightness, so it scales a rise to 225 straight back
// down and only the dip shows. v0.0.9 dipped to 90 at most, which against a
// capped 180 was barely visible; dipping to an eighth of the configured level
// reads clearly whatever the cap is doing.

static const int LATE_NIGHT_START_HOUR = 19;       // 7pm; runs through 11pm
static const uint32_t SWELL_MS = 15000;            // the whole warning
static const uint32_t SWELL_PERIOD_MS = 1200;      // one up-and-down
static const uint8_t SWELL_DEPTH_DIVISOR = 8;      // the bottom of a swell is base / 8
static const uint8_t SWELL_FLOOR = 2;              // but never dark
static const uint32_t SWELL_IN_MS = 400;           // easing into the swells
static const uint32_t SWELL_OUT_MS = 1500;         // and back out to the configured level

// Whether the top of `hour` (0-23, local time) gets a late-night swell.
static inline bool lateNightSwellHour(int hour) {
    return hour >= LATE_NIGHT_START_HOUR && hour <= 23;
}

// Cubic ease in/out on 0-255: soft at both ends rather than corners.
static inline uint8_t swellEase(uint8_t x) {
    uint32_t t = x;
    // 3t^2 - 2t^3, scaled to 0-255.
    return (uint8_t)((3UL * t * t * 255UL - 2UL * t * t * t) / (255UL * 255UL));
}

// A smooth 0 -> 255 -> 0 wave over one period; `phase` is 0-255.
static inline uint8_t swellWave(uint8_t phase) {
    uint8_t tri = phase < 128 ? (uint8_t)(phase * 2) : (uint8_t)((255 - phase) * 2);
    return swellEase(tri);
}

/// The output brightness a swell wants `into` ms after it started.
/// @param base  the configured brightness, which both ends of the swell return to
/// @param peak  the top of a swell (the power ceiling, MAX_BRIGHTNESS)
/// @param into  ms since the swell started; at or past SWELL_MS returns `base`
/// @return the brightness to show this frame
static inline uint8_t swellBrightness(uint8_t base, uint8_t peak, uint32_t into) {
    if (into >= SWELL_MS || base == 0) return base;
    if (peak < base) peak = base;

    // Dip to a fraction of the configured level so the swell shows even when
    // the power limit flattens the rise; keep a floor so the strip never
    // blinks off, without lifting a very dim strip above its own level.
    uint8_t low = base / SWELL_DEPTH_DIVISOR;
    if (low < SWELL_FLOOR) low = SWELL_FLOOR;
    if (low > base) low = base;
    // The wave starts at 0, so the first swell rises from the bottom.
    uint8_t wave = swellWave((uint8_t)(((into % SWELL_PERIOD_MS) * 256UL) / SWELL_PERIOD_MS));
    uint8_t target = (uint8_t)(low + ((uint16_t)(peak - low) * wave) / 255);

    uint32_t env = 255;  // how much of the swell to apply
    if (into < SWELL_IN_MS) {
        env = swellEase((uint8_t)((into * 255UL) / SWELL_IN_MS));
    } else if (into > SWELL_MS - SWELL_OUT_MS) {
        env = swellEase((uint8_t)(((SWELL_MS - into) * 255UL) / SWELL_OUT_MS));
    }
    return (uint8_t)((base * (255 - env) + target * env) / 255);
}
