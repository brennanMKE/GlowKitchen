#pragma once

#include <stdint.h>
#include <string.h>

// Pulse: a flash for Home Assistant or GlowPanel to fire on an event, ported
// from the GeminiLamps firmware (GlowLamp src/main.cpp, "===== Pulse =====")
// so a "Pulse" button looks the same on every device. Four phases:
//
//   dip      down to a fifth of the current brightness   40% of the lead-in
//   rise     back up past it to the peak, accelerating   60% of the lead-in
//   peak     the peak brightness, every other LED white  PULSE_PEAK_FRAMES
//   return   back to the colour and brightness it had    the rest
//
// The lead-in is whatever the total leaves after the peak and the return. The
// dip is what makes it visible on a strip that is already bright, and white
// is the only way past full: a saturated colour lights one or two of each
// LED's channels, white lights all three.
//
// It is an overlay. Only the brightness passed to FastLED.show() and, for the
// frames around the peak, the even LEDs of the frame being shown change; the
// leds[] buffer is restored straight after the show, so the running effect,
// the theme and the late-night swell carry on underneath exactly as they
// would have. Nothing is saved.
//
// Lives in its own header, free of Arduino and FastLED, so the native test
// suite (test/test_pulse/) links against the real implementation.

static const uint32_t PULSE_DEFAULT_MS = 750;
static const uint32_t PULSE_MIN_MS = 300;
static const uint32_t PULSE_MAX_MS = 5000;
static const uint32_t PULSE_PEAK_FRAMES = 1;
static const uint32_t PULSE_RETURN_MS = 75;
static const uint8_t PULSE_DIP_PERCENT = 40;   // of the lead-in; the rise gets the rest
static const uint8_t PULSE_DIP_FLOOR = 51;     // fraction of brightness at the bottom, /255
static const uint8_t PULSE_WHITEN_FROM = 192;  // how far into the rise white starts, /255

// GlowLamp's frame. The peak's time budget is reserved in these units so the
// lead-in has the same length on both firmwares; GlowKitchen's own frames are
// counted (see pulseFrame()) rather than timed, so the peak is always shown.
static const uint32_t PULSE_FRAME_MS = 16;

static const char PULSE_COMMAND[] = "PULSE";

// ---- 8-bit helpers, matching FastLED's lib8tion (FASTLED_SCALE8_FIXED) ----

static inline uint8_t pulseScale8(uint8_t i, uint8_t scale) {
    return (uint8_t)(((uint16_t)i * (1 + (uint16_t)scale)) >> 8);
}

static inline uint8_t pulseLerp8(uint8_t a, uint8_t b, uint8_t frac) {
    if (b > a) return (uint8_t)(a + pulseScale8((uint8_t)(b - a), frac));
    return (uint8_t)(a - pulseScale8((uint8_t)(a - b), frac));
}

static inline uint8_t pulseEaseInOutQuad(uint8_t i) {
    uint8_t j = i;
    if (j & 0x80) j = (uint8_t)(255 - j);
    uint8_t jj = pulseScale8(j, j);
    uint8_t jj2 = (uint8_t)(jj << 1);
    if (i & 0x80) jj2 = (uint8_t)(255 - jj2);
    return jj2;
}

// ---- Command ----

/// Clamp a requested duration: 0 means the default, anything else is held to
/// PULSE_MIN_MS..PULSE_MAX_MS.
static inline uint32_t pulseClampMs(uint32_t ms) {
    if (ms == 0) return PULSE_DEFAULT_MS;
    if (ms < PULSE_MIN_MS) return PULSE_MIN_MS;
    if (ms > PULSE_MAX_MS) return PULSE_MAX_MS;
    return ms;
}

// Leading decimal digits of `s`, saturating, stopping at the first non-digit.
// Returns false when there are none (empty, a sign, a word).
static inline bool pulseParseUint(const char *s, uint32_t &out) {
    if (s == nullptr || *s < '0' || *s > '9') return false;
    uint32_t v = 0;
    for (; *s >= '0' && *s <= '9'; s++) {
        uint32_t d = (uint32_t)(*s - '0');
        v = v > (0xFFFFFFFFUL - d) / 10 ? 0xFFFFFFFFUL : v * 10 + d;
    }
    out = v;
    return true;
}

/// Parse `PULSE`, `PULSE:<ms>` or `PULSE:<ms>:<level>` (already upper-cased
/// and trimmed). Returns false only when the message is not a pulse command at
/// all. Anything malformed inside one falls back to its default rather than
/// rejecting the pulse, as GlowLamp's onPulse does: an automation firing a
/// pulse wants a pulse, not a lesson in syntax.
///
/// @param ms     the clamped duration (PULSE_DEFAULT_MS when missing or bad)
/// @param level  1-255 for a peak brightness, 0 for "full" (missing, 0 or out
///               of range); the firmware caps it at its own ceiling
static inline bool parsePulseCommand(const char *msg, uint32_t &ms, uint8_t &level) {
    const size_t n = sizeof(PULSE_COMMAND) - 1;
    if (msg == nullptr || strncmp(msg, PULSE_COMMAND, n) != 0) return false;
    if (msg[n] != '\0' && msg[n] != ':') return false;  // PULSEX is something else

    ms = PULSE_DEFAULT_MS;
    level = 0;
    if (msg[n] == '\0') return true;

    const char *arg = msg + n + 1;
    uint32_t v = 0;
    if (pulseParseUint(arg, v)) ms = pulseClampMs(v);

    const char *second = strchr(arg, ':');
    if (second != nullptr && pulseParseUint(second + 1, v) && v >= 1 && v <= 255) {
        level = (uint8_t)v;
    }
    return true;
}

/// The brightness the pulse climbs to: `level` (0 meaning full), never above
/// the firmware's ceiling `maxBrightness`.
static inline uint8_t pulsePeakLevel(uint8_t level, uint8_t maxBrightness) {
    if (level == 0 || level > maxBrightness) return maxBrightness;
    return level;
}

// ---- Frames ----

struct PulseFrame {
    uint8_t bright;  // the brightness to show
    uint8_t white;   // how far the whitened LEDs go towards white (0 none, 255 full)
};

/// One frame of the pulse, `into` ms after it started.
///
/// @param durMs       the whole pulse, already clamped (pulseClampMs)
/// @param base        the brightness the strip would show without the pulse
///                    (the configured level, or the swell's this frame)
/// @param peakLevel   pulsePeakLevel(); the peak is never below `base`
/// @param peakFrames  frames shown at the peak so far; updated here. The peak is
///                    counted in frames, not ms, because a frame is the unit the
///                    eye actually gets: the first PULSE_PEAK_FRAMES frames at or
///                    after the end of the rise are the peak, however late they
///                    land, and every frame after that is the return, which
///                    eases back to `base` by `durMs`.
static inline PulseFrame pulseFrame(uint32_t into, uint32_t durMs, uint8_t base,
                                    uint8_t peakLevel, uint8_t &peakFrames) {
    PulseFrame out = {base, 0};
    if (into >= durMs) return out;

    const uint8_t peak = peakLevel > base ? peakLevel : base;
    const uint8_t bottom = pulseScale8(base, PULSE_DIP_FLOOR);
    const uint32_t peakMs = PULSE_PEAK_FRAMES * PULSE_FRAME_MS;
    const uint32_t lead = durMs - peakMs - PULSE_RETURN_MS;  // durMs >= PULSE_MIN_MS
    const uint32_t dipMs = lead * PULSE_DIP_PERCENT / 100;
    const uint32_t riseMs = lead - dipMs;

    if (into < dipMs) {
        uint8_t f = (uint8_t)((into * 255U) / dipMs);
        out.bright = pulseLerp8(base, bottom, pulseEaseInOutQuad(f));
        return out;
    }
    if (into < lead) {
        uint8_t f = (uint8_t)(((into - dipMs) * 255U) / riseMs);
        // Squared: slow off the bottom, fastest into the peak, so the flash
        // arrives as a climax rather than a ramp.
        out.bright = pulseLerp8(bottom, peak, pulseScale8(f, f));
        out.white = f > PULSE_WHITEN_FROM
                        ? (uint8_t)(((uint16_t)(f - PULSE_WHITEN_FROM) * 255U) / (255 - PULSE_WHITEN_FROM))
                        : 0;
        return out;
    }
    if (peakFrames < PULSE_PEAK_FRAMES) {
        peakFrames++;
        out.bright = peak;
        out.white = 255;
        return out;
    }
    const uint32_t back = durMs - lead;  // the peak's budget plus PULSE_RETURN_MS
    const uint32_t r = into - lead;
    uint8_t f = r >= back ? 255 : (uint8_t)((r * 255U) / back);
    out.bright = pulseLerp8(peak, base, f);
    out.white = (uint8_t)(255 - f);
    return out;
}

/// Which LEDs go white: every other one, starting at the first, so any strip
/// length (odd, even, a single LED) gets the same alternating pattern. Behind
/// a diffuser half white and half coloured reads as a bright flash of the
/// effect's colour, where all white reads as the strip turning into a bulb.
static inline bool pulseWhitensLed(int index) {
    return (index & 1) == 0;
}

/// One channel pushed `white` of the way to full: s -> 0 and v -> 255 in HSV
/// terms, which is what GlowLamp does to its pixels.
static inline uint8_t pulseWhitenChannel(uint8_t c, uint8_t white) {
    return (uint8_t)(c + pulseScale8((uint8_t)(255 - c), white));
}
