#pragma once

// Living looks: the renderers the six built-in themes use, and the
// cross-fade between two looks.
//
// The built-ins used to be FLICKER (Green) and BLEND (everything else). BLEND
// is a static gradient that moves one LED every 500 ms. On a 240-LED strip a
// full pass takes minutes, so it reads as a still picture. Each theme here
// gets something that is visibly alive, and Green keeps its candle, now
// eased rather than snapping.
//
//   CANDLE  Green        per-LED flicker that eases, small hue jitter per LED
//   FLOW    Rainbow      gradient that travels, brightness ripple against it
//   GLITTER Pink Pony    slower gradient, white and hot-pink sparkles
//   WAVES   Ocean        three interfering swells, pale foam on the crests
//   EMBER   Sunset       eased flicker whose colour follows its heat
//   CANOPY  Forest       drifting light and shade, warm flecks where it's brightest
//
// Same include-order contract as src/effects.h (CRGB, CHSV, sin8, min, max
// must already be in scope), and header-only for the same reason: the native
// tests link it without main.cpp. Every helper here is fx-prefixed so none of
// them collide with FastLED's own scale8/lerp8by8/ease8InOutCubic on device,
// which the shim doesn't provide on the host.
//
// All six run on a fixed 20 ms frame and redraw every LED each frame. That's
// what makes them smooth, and it's also what lets the cross-fade and mirror
// treat them like BLEND. Speed scales their clocks (128 = as designed),
// intensity their depth (128 = as designed), so the built-ins, which pass
// 128/128, get the look described above and SET_EFFECT can still tune them.

#include <stdint.h>
#include "effects.h"

// ---- Small integer helpers ----------------------------------------------

inline uint8_t fxScale8(uint8_t v, uint8_t s) {
    return (uint8_t)(((uint16_t)v * (uint16_t)(s + 1)) >> 8);
}

inline uint8_t fxLerp8(uint8_t a, uint8_t b, uint8_t f) {
    return (uint8_t)((int)a + (((int)b - (int)a) * (int)f) / 255);
}

// Smoothstep, 0 -> 0 and 255 -> 255 exactly.
inline uint8_t fxEaseInOut(uint8_t x) {
    uint32_t t = x;
    return (uint8_t)((t * t * (765u - 2u * t)) / 65025u);
}

inline uint8_t fxClamp8(int v) {
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// Shortest way around the hue wheel, so red to magenta doesn't go through green.
inline uint8_t fxHueLerp(uint8_t a, uint8_t b, uint8_t f) {
    int d = (int8_t)(uint8_t)(b - a);
    return (uint8_t)(a + (d * (int)f) / 255);
}

// pos is 8.8 fixed point: the whole part indexes the palette (wrapping), the
// fraction blends toward the next entry. `ease` makes it rest on each entry
// and move between them, the way GlowLamp's blend does.
inline PaletteColor fxPaletteAt(const PaletteColor* c, int count, uint32_t pos, bool ease) {
    int i = (int)((pos >> 8) % (uint32_t)count);
    int j = (i + 1) % count;
    uint8_t f = (uint8_t)(pos & 0xFF);
    if (ease) f = fxEaseInOut(f);
    PaletteColor o;
    o.h = fxHueLerp(c[i].h, c[j].h, f);
    o.s = fxLerp8(c[i].s, c[j].s, f);
    o.v = fxLerp8(c[i].v, c[j].v, f);
    return o;
}

// A non-wrapping position for value in 0..255: 0 is the first entry, 255 is
// the last. For looks that map a quantity (heat, depth) onto the palette and
// must not blend the last colour back into the first.
inline PaletteColor fxPaletteRamp(const PaletteColor* c, int count, uint8_t value) {
    if (count <= 1) return c[0];
    uint32_t pos = ((uint32_t)value * (uint32_t)(count - 1) * 256u) / 256u;
    if (pos >= (uint32_t)(count - 1) * 256u) return c[count - 1];
    return fxPaletteAt(c, count, pos, false);
}

// t milliseconds expressed in 8.8 units of `msPerUnit`, without the overflow
// a plain t * 256 / msPerUnit hits after 4.6 hours.
inline uint32_t fxTimeUnits(uint32_t t, uint32_t msPerUnit) {
    return (t / msPerUnit) * 256u + ((t % msPerUnit) * 256u) / msPerUnit;
}

// A phase byte that goes once round every periodMs.
inline uint8_t fxTimePhase(uint32_t t, uint32_t periodMs) {
    return (uint8_t)(((t % periodMs) * 256u) / periodMs);
}

inline uint8_t fxHash8(uint32_t x, uint8_t seed) {
    uint32_t h = x * 0x9E3779B1u + (uint32_t)seed * 0x85EBCA77u;
    h ^= h >> 15; h *= 0x2C1B3C6Du;
    h ^= h >> 12; h *= 0x297A2D39u;
    h ^= h >> 15;
    return (uint8_t)(h >> 24);
}

// 1-D value noise. x is 8.8 fixed point in lattice cells. FastLED has
// inoise8, but the native shim doesn't, and this is all CANOPY and EMBER need.
inline uint8_t fxNoise8(uint32_t x, uint8_t seed) {
    uint32_t i = x >> 8;
    uint8_t f = fxEaseInOut((uint8_t)(x & 0xFF));
    return fxLerp8(fxHash8(i, seed), fxHash8(i + 1, seed), f);
}

// Pushes v away from the middle: value noise spends most of its time near 128.
inline uint8_t fxContrast(uint8_t v, int num, int den) {
    return fxClamp8(128 + (((int)v - 128) * num) / den);
}

// ---- Frame clock ---------------------------------------------------------

static const uint32_t LOOK_FRAME_MS = 20;

// Gates a look to one frame every LOOK_FRAME_MS and advances its clocks by the
// elapsed time, scaled by speed: 32/256 of real time at 0, real time at 128,
// about 1.9x at 255. colorT only moves while colour change is on, so the
// remote's colour-change toggle holds a look's colours and leaves its motion.
inline bool fxLookTick(EffectState& s, uint32_t now) {
    if (!timeReached(now, s.effectTimeout)) return false;
    uint32_t dt = now - s.lastFrameMs;
    if (s.lastFrameMs == 0 || dt > 200) dt = LOOK_FRAME_MS;  // first frame, or after a stall
    s.lastFrameMs = now ? now : 1;
    s.effectTimeout = now + LOOK_FRAME_MS;

    uint32_t mul = 32u + ((uint32_t)s.speed * 7u) / 4u;  // 256 at speed 128
    s.animFrac += dt * mul;
    uint32_t step = s.animFrac >> 8;
    s.animFrac &= 0xFF;
    s.animT += step;
    if (s.colorChangeEnabled) s.colorT += step;
    return true;
}

inline uint8_t fxBase(const EffectState& s) {
    return (uint8_t)min(s.maxBrightness, s.brightness);
}

inline bool fxLookReady(const EffectState& s) {
    return s.numLeds > 0 && s.colorCount > 0 && s.colors && s.leds;
}

inline bool fxLookHasLedState(const EffectState& s) {
    return s.timeouts && s.ledLevel && s.ledTarget && s.ledAux;
}

// Eases one LED's level toward its target: fast up, slower down. A flame
// flares and then sags. Never overshoots, always moves at least one step.
inline uint8_t fxFlameStep(uint8_t cur, uint8_t target) {
    int c = cur, d = (int)target - c;
    if (d > 0) c += (d + 1) / 2;
    else if (d < 0) c += (d - 3) / 4;
    return (uint8_t)c;
}

// Depth knob shared by CANDLE and EMBER: the lowest level a retarget can pick.
// `at128` is the floor the built-in theme gets; 0 means no dip, 255 bottoms out.
inline int fxFlameFloor(uint8_t intensity, int at128) {
    int f = 255 - ((int)intensity * (255 - at128)) / 128;
    return f < 0 ? 0 : (f > 254 ? 254 : f);
}

// ---- CANDLE (Green) ------------------------------------------------------
// FLICKER, eased. FLICKER sets each LED to a new random level every 500-750 ms
// and holds it, so the strip steps. Here each LED picks a new target more
// often (120-420 ms) and eases to it, flaring fast and sagging slowly, with
// the odd deep gutter. Each LED also carries a hue offset of up to +/-6, so
// the strip is a handful of greens rather than one. The base colour drifts
// through the palette, resting on each entry for about 2 s.
inline bool renderCandle(EffectState& s, uint32_t now) {
    if (!fxLookReady(s) || !fxLookHasLedState(s) || !s.rng) return false;
    if (!fxLookTick(s, now)) return false;

    uint32_t scale = 448 - ((uint32_t)s.speed * 3) / 2;  // 256 at speed 128
    uint32_t loMs = (120u * scale) / 256u;
    uint32_t hiMs = (420u * scale) / 256u;
    if (hiMs <= loMs) hiMs = loMs + 1;
    int floorV = fxFlameFloor(s.intensity, 110);
    uint8_t base = fxBase(s);

    uint32_t pos = fxTimeUnits(s.colorT, 2000);
    PaletteColor c = fxPaletteAt(s.colors, s.colorCount, pos, true);
    s.hueIndex = (int)((pos >> 8) % (uint32_t)s.colorCount);

    for (int i = 0; i < s.numLeds; i++) {
        if (timeReached(now, s.timeouts[i])) {
            s.timeouts[i] = now + s.rng(loMs, hiMs);
            uint8_t t = (uint8_t)s.rng((uint32_t)floorV, 256);
            if (s.rng(0, 48) == 0) t = (uint8_t)(t / 3);  // a gust
            s.ledTarget[i] = t;
            if (s.rng(0, 4) == 0) s.ledAux[i] = (uint8_t)((int)s.rng(0, 13) - 6);
        }
        s.ledLevel[i] = fxFlameStep(s.ledLevel[i], s.ledTarget[i]);
        uint8_t hue = (uint8_t)(c.h + (int8_t)s.ledAux[i]);
        s.leds[i] = CHSV(hue, c.s, fxScale8(s.ledLevel[i], base));
    }
    return true;
}

// ---- FLOW (Rainbow) ------------------------------------------------------

// One pixel of a gradient that travels toward the far end of the strip, one
// palette entry every scrollMs, under a brightness ripple travelling back
// toward the start. Shared by FLOW and GLITTER. Returns the pixel at full
// level in .v so a caller can add to it before dimming.
inline CHSV fxGradientPixel(const EffectState& s, int i, uint32_t scrollMs,
                            uint32_t rippleMs, uint8_t rippleDepth) {
    int span = s.ledsPerColor > 0 ? s.ledsPerColor : 1;
    uint32_t total = (uint32_t)s.colorCount * 256u;
    uint32_t scroll = fxTimeUnits(s.colorT, scrollMs) % total;
    uint32_t off = (((uint32_t)i * 256u) / (uint32_t)span) % total;
    uint32_t pos = (scroll + total - off) % total;
    PaletteColor c = fxPaletteAt(s.colors, s.colorCount, pos, false);

    // Ripple about one and a half palette entries long.
    uint32_t waveLen = (uint32_t)span * 3u / 2u;
    if (waveLen < 2) waveLen = 2;
    uint8_t phase = (uint8_t)(((uint32_t)i * 256u) / waveLen + fxTimePhase(s.animT, rippleMs));
    uint8_t w = sin8(phase);
    uint8_t level = (uint8_t)(255 - fxScale8((uint8_t)(255 - w), rippleDepth));
    return CHSV(c.h, c.s, level);
}

// The gradient moves one palette entry every 4 s, so a rainbow of eight
// colours goes round in about half a minute instead of BLEND's several
// minutes. The ripple dips to about 60% at intensity 128.
inline bool renderFlow(EffectState& s, uint32_t now) {
    if (!fxLookReady(s)) return false;
    if (!fxLookTick(s, now)) return false;
    uint8_t base = fxBase(s);
    uint8_t depth = (uint8_t)(((uint32_t)s.intensity * 3u) / 4u);
    for (int i = 0; i < s.numLeds; i++) {
        CHSV p = fxGradientPixel(s, i, 4000, 3000, depth);
        s.leds[i] = CHSV(p.hue, p.sat, fxScale8(p.val, base));
    }
    return true;
}

// ---- GLITTER (Pink Pony Club) --------------------------------------------
// A slower, gentler FLOW underneath, and sparkles on top. A sparkle starts at
// full and decays by an eighth every frame, so it flashes and is gone in a
// fraction of a second. A third of them are white and the rest hot pink.
// Intensity is density: at 128 each LED sparkles about once every 8 s, so a
// 240-LED strip shows about 30 a second and a 10-LED one about one.
inline bool renderGlitter(EffectState& s, uint32_t now) {
    if (!fxLookReady(s) || !fxLookHasLedState(s) || !s.rng) return false;
    if (!fxLookTick(s, now)) return false;
    uint8_t base = fxBase(s);
    uint8_t depth = (uint8_t)(s.intensity / 3);
    uint32_t chance = ((uint32_t)s.intensity + 16u) * 164u / 144u;  // per 65536, per frame

    for (int i = 0; i < s.numLeds; i++) {
        uint8_t e = fxScale8(s.ledLevel[i], 223);
        if (s.rng(0, 65536) < chance) {
            e = 255;
            s.ledAux[i] = (s.rng(0, 3) == 0) ? 0 : 1;  // 0 white, 1 hot pink
        }
        s.ledLevel[i] = e;

        CHSV p = fxGradientPixel(s, i, 6000, 4500, depth);
        uint8_t hue = p.hue, sat = p.sat, val = p.val;
        if (e) {
            if (s.ledAux[i] == 0) {
                sat = (uint8_t)(sat - fxScale8(sat, fxScale8(e, 230)));
            } else {
                hue = fxHueLerp(hue, 232, e);
                sat = (uint8_t)(sat - fxScale8(sat, fxScale8(e, 60)));
            }
            val = (uint8_t)(val + fxScale8((uint8_t)(255 - val), e));
        }
        s.leds[i] = CHSV(hue, sat, fxScale8(val, base));
    }
    return true;
}

// ---- WAVES (Ocean Waves) -------------------------------------------------
// Three sine swells of different lengths and periods, two rolling one way and
// one the other. Where their crests meet, the water is bright and takes the
// light end of the palette, and the very top of a crest loses saturation,
// which reads as foam. The contrast stretch is kept mild (5/4): at 3/2 the
// sum clipped, and on an 11-LED bench strip one LED sat at full foam for
// most of a second, which read as stuck rather than as water. Troughs are dark and take the deep end. Intensity is
// depth: troughs drop to about 30% at 128.
inline bool renderWaves(EffectState& s, uint32_t now) {
    if (!fxLookReady(s)) return false;
    if (!fxLookTick(s, now)) return false;
    uint8_t base = fxBase(s);
    uint32_t L = (uint32_t)(s.ledsPerColor > 1 ? s.ledsPerColor : 2);
    int lo = 255 - ((int)s.intensity * 175) / 128;
    if (lo < 0) lo = 0;

    uint8_t t1 = fxTimePhase(s.animT, 5000);
    uint8_t t2 = fxTimePhase(s.animT, 3700);
    uint8_t t3 = fxTimePhase(s.animT, 11000);
    // The whole sea slowly leans lighter and deeper over about 20 s.
    int lean = ((int)sin8(fxTimePhase(s.colorT, 20000)) - 128) / 4;

    for (int i = 0; i < s.numLeds; i++) {
        uint32_t ui = (uint32_t)i;
        uint8_t w1 = sin8((uint8_t)((ui * 256u) / (L * 2u) - t1));
        uint8_t w2 = sin8((uint8_t)((ui * 2560u) / (L * 13u) + t2));
        uint8_t w3 = sin8((uint8_t)((ui * 256u) / (L * 5u) - t3));
        uint8_t c = fxContrast((uint8_t)(((uint32_t)w1 * 2u + w2 + w3) / 4u), 5, 4);

        uint8_t level = (uint8_t)(lo + fxScale8(c, (uint8_t)(255 - lo)));
        PaletteColor col = fxPaletteRamp(s.colors, s.colorCount, fxClamp8((int)c + lean));
        uint8_t sat = col.s;
        if (c > 225) sat = (uint8_t)(sat - min((int)sat, ((int)c - 225) * 5));
        s.leds[i] = CHSV(col.h, sat, fxScale8(level, base));
    }
    return true;
}

// ---- EMBER (Sunset) ------------------------------------------------------
// CANDLE's eased flicker with a deeper floor and a slower window, but each
// LED's colour follows its heat: a flaring ember runs to orange and gold, a
// dying one sinks to rose and purple. Slow noise along the strip shifts the
// heat too, so there are warmer and cooler patches that drift.
inline bool renderEmber(EffectState& s, uint32_t now) {
    if (!fxLookReady(s) || !fxLookHasLedState(s) || !s.rng) return false;
    if (!fxLookTick(s, now)) return false;

    uint32_t scale = 448 - ((uint32_t)s.speed * 3) / 2;
    uint32_t loMs = (200u * scale) / 256u;
    uint32_t hiMs = (650u * scale) / 256u;
    if (hiMs <= loMs) hiMs = loMs + 1;
    int floorV = fxFlameFloor(s.intensity, 60);
    uint8_t base = fxBase(s);
    uint32_t L = (uint32_t)(s.ledsPerColor > 1 ? s.ledsPerColor : 2);
    uint32_t drift = fxTimeUnits(s.colorT, 3000);

    for (int i = 0; i < s.numLeds; i++) {
        if (timeReached(now, s.timeouts[i])) {
            s.timeouts[i] = now + s.rng(loMs, hiMs);
            s.ledTarget[i] = (uint8_t)s.rng((uint32_t)floorV, 256);
        }
        uint8_t lvl = fxFlameStep(s.ledLevel[i], s.ledTarget[i]);
        s.ledLevel[i] = lvl;

        uint8_t patch = fxNoise8(((uint32_t)i * 256u) / L + drift, 7);
        uint8_t heat = (uint8_t)(((uint32_t)lvl * 5u + (uint32_t)patch * 3u) / 8u);
        PaletteColor col = fxPaletteRamp(s.colors, s.colorCount, fxContrast(heat, 3, 2));
        s.leds[i] = CHSV(col.h, col.s, fxScale8(lvl, base));
    }
    return true;
}

// ---- CANOPY (Forest) -----------------------------------------------------
// Light through leaves: two layers of noise drifting at different rates make
// patches of light and shade that wander and change shape. A third, slower
// layer moves the green itself through the palette. The light stretch is 3/2:
// at 2/1 about a third of the strip sat clipped at full sun at any moment. The brightest spots
// shift toward a warm yellow-green, the sun breaking through. Intensity is
// how deep the shade goes, to about 20% at 128.
inline bool renderCanopy(EffectState& s, uint32_t now) {
    if (!fxLookReady(s)) return false;
    if (!fxLookTick(s, now)) return false;
    uint8_t base = fxBase(s);
    uint32_t L = (uint32_t)(s.ledsPerColor > 1 ? s.ledsPerColor : 2);
    int shade = 255 - ((int)s.intensity * 200) / 128;
    if (shade < 0) shade = 0;

    // Offset so the subtraction below never wraps through zero, which would
    // jump the lattice once.
    const uint32_t ORIGIN = 1u << 28;
    uint32_t a = fxTimeUnits(s.animT, 2500);
    uint32_t b = fxTimeUnits(s.animT, 4100);
    uint32_t g = fxTimeUnits(s.colorT, 9000);

    for (int i = 0; i < s.numLeds; i++) {
        uint32_t x = ((uint32_t)i * 256u) / L;
        uint8_t n1 = fxNoise8(ORIGIN + x + a, 1);
        uint8_t n2 = fxNoise8(ORIGIN + x / 2u - b, 2);
        uint8_t light = fxContrast((uint8_t)(((uint32_t)n1 * 3u + n2) / 4u), 3, 2);
        uint8_t level = (uint8_t)(shade + fxScale8(light, (uint8_t)(255 - shade)));

        uint8_t leaf = fxNoise8(ORIGIN + x / 3u + g, 3);
        PaletteColor col = fxPaletteRamp(s.colors, s.colorCount, fxContrast(leaf, 2, 1));
        uint8_t hue = col.h, sat = col.s;
        if (light > 200) {
            uint8_t sun = fxClamp8(((int)light - 200) * 4);
            hue = fxHueLerp(hue, 70, sun);
            sat = (uint8_t)(sat - fxScale8(sat, fxScale8(sun, 50)));
        }
        s.leds[i] = CHSV(hue, sat, fxScale8(level, base));
    }
    return true;
}

// ---- Cross-fade ----------------------------------------------------------
// Blends one shown pixel toward another, around the colour wheel.
//
// The first version mixed red, green and blue channel by channel, and then
// scaled the result back up so the fade wouldn't dim. Two colours far apart
// on the wheel mix to grey -- green (0,200,0) and pink (200,0,120) average to
// (100,100,60) -- and scaling that grey up to full brightness made the middle
// of every such fade flash white. Now hue travels the short way round the
// wheel while saturation and value blend separately, as GlowLamp does, so
// green to pink passes through yellow, orange and red at full colour.
//
// Both ends go to HSV through rgbToPaletteColor() (src/effects.h, exact
// integer HSV) and the blend comes back through fxSpectrumRgb() below, its
// inverse. Not through FastLED's CHSV -> CRGB: that is a "rainbow" mapping
// that doesn't invert rgbToPaletteColor(), and on the bench strip the
// round-trip error it left behind turned the middle of Green -> Pink Pony a
// muddy grey-brown. The round trip here lands within a couple of steps, and
// that remainder is fed back in, weighted toward each end, so f = 0 is
// exactly a and f = 255 exactly b.
//
// Black and grey carry no hue. Fading up from black, or into a white, takes
// hue (and, from black, saturation) from the other end, rather than starting
// from red or passing through pastel.

// Standard HSV -> RGB in FastLED's 0-255 hue space: six equal sectors, the
// inverse of rgbToPaletteColor(). FastLED's own hsv2rgb_spectrum compresses
// hue to 0-191 first, so it isn't an exact inverse either.
inline CRGB fxSpectrumRgb(uint8_t hue, uint8_t sat, uint8_t val) {
    uint32_t h6 = ((uint32_t)hue * 1530u + 128u) / 256u;  // 0..1530
    if (h6 >= 1530u) h6 -= 1530u;
    uint32_t sector = h6 / 255u, frac = h6 % 255u;
    uint32_t v = val, s = sat;
    uint32_t p = (v * (255u - s) + 127u) / 255u;
    uint32_t q = (v * (255u - (s * frac + 127u) / 255u) + 127u) / 255u;
    uint32_t t = (v * (255u - (s * (255u - frac) + 127u) / 255u) + 127u) / 255u;
    switch (sector) {
        case 0:  return CRGB((uint8_t)v, (uint8_t)t, (uint8_t)p);
        case 1:  return CRGB((uint8_t)q, (uint8_t)v, (uint8_t)p);
        case 2:  return CRGB((uint8_t)p, (uint8_t)v, (uint8_t)t);
        case 3:  return CRGB((uint8_t)p, (uint8_t)q, (uint8_t)v);
        case 4:  return CRGB((uint8_t)t, (uint8_t)p, (uint8_t)v);
        default: return CRGB((uint8_t)v, (uint8_t)p, (uint8_t)q);
    }
}

inline uint8_t fxMax3(const CRGB& c) {
    uint8_t m = c.r > c.g ? c.r : c.g;
    return m > c.b ? m : c.b;
}

inline uint8_t fxCorrected(uint8_t mid, uint8_t a, uint8_t aRound, uint8_t b, uint8_t bRound, uint8_t f) {
    int ea = (int)a - (int)aRound;
    int eb = (int)b - (int)bRound;
    return fxClamp8((int)mid + (ea * (255 - (int)f)) / 255 + (eb * (int)f) / 255);
}

inline CRGB fxCrossfade(const CRGB& a, const CRGB& b, uint8_t f) {
    if (f == 0) return a;
    if (f == 255) return b;

    PaletteColor ha = rgbToPaletteColor(a.r, a.g, a.b);
    PaletteColor hb = rgbToPaletteColor(b.r, b.g, b.b);
    CRGB ra = fxSpectrumRgb(ha.h, ha.s, ha.v);
    CRGB rb = fxSpectrumRgb(hb.h, hb.s, hb.v);

    // Borrow what an end doesn't have from the other end.
    if (ha.v == 0) { ha.h = hb.h; ha.s = hb.s; }
    else if (ha.s == 0) ha.h = hb.h;
    if (hb.v == 0) { hb.h = ha.h; hb.s = ha.s; }
    else if (hb.s == 0) hb.h = ha.h;

    CRGB mid = fxSpectrumRgb(fxHueLerp(ha.h, hb.h, f), fxLerp8(ha.s, hb.s, f), fxLerp8(ha.v, hb.v, f));
    return CRGB(fxCorrected(mid.r, a.r, ra.r, b.r, rb.r, f),
                fxCorrected(mid.g, a.g, ra.g, b.g, rb.g, f),
                fxCorrected(mid.b, a.b, ra.b, b.b, rb.b, f));
}
