#pragma once

// Theme identity and static color data: the HueTheme enum, the six built-in
// hue arrays, and THEME_NAMES[]. (The theme -> EffectMode mapping,
// BUILTIN_EFFECT_MODE[], lives in src/effects.h instead -- it needs
// EffectMode, and this header is included BY effects.h, not the other way
// around, so it stays free of that dependency.)
//
// Moved out of main.cpp (issue #0014) so the native test suite
// (test/test_effects/) can link against the REAL tables -- same rationale
// as src/time_utils.h for timeReached() (issue #0008): a test that links a
// hand-copied stand-in of these arrays could silently drift from what
// ships, and drift is exactly what the frame-capture diff exists to catch.
//
// Header-only, like src/effects.h: [env:native] does not build main.cpp
// (test_build_src is off), so a .cpp here would not be linkable from the
// native test without dragging Arduino/FastLED into the host build. This
// header has no FastLED dependency at all -- it only needs uint8_t/int -- so
// there is no include-order contract to document, unlike src/effects.h.

#include <stdint.h>

enum HueTheme {
    THEME_GREEN = 0,
    THEME_RAINBOW = 1,
    THEME_PINK_PONY = 2,
    THEME_OCEAN_WAVES = 3,
    THEME_SUNSET = 4,
    THEME_FOREST = 5,
    THEME_CUSTOM = 6,   // new (issue #0014) -- holds ad hoc user config,
                        // unreachable by NEXT_THEME/PREV_THEME cycling
    THEME_COUNT = 7
};

// NEXT_THEME / PREV_THEME must modulo against this, not THEME_COUNT, so
// THEME_CUSTOM stays unreachable by cycling.
const int CYCLEABLE_THEME_COUNT = 6;

// Palette widening (issue #0015, per #0013's "Palette representation --
// CHSV triples" decision). The engine as built in #0014 stored one hue byte
// per color and rendered CHSV(hue, 255, 200) -- saturation and value were
// fixed, so white/gray/pastel colors were inexpressible. Every built-in
// theme below is now an array of PaletteColor triples instead of bare hue
// bytes; the six arrays are {hue, 255, 200} triples, so BLEND/FLICKER's
// painted output is byte-identical to before (see
// test/test_effects/test_effects.cpp, which must stay green through this
// change -- that's the acceptance test for the widening).
struct PaletteColor {
    uint8_t h, s, v;
};

// 7 entries -- THEME_CUSTOM included. THEME_NAMES[] is indexed unguarded by
// currentTheme in saveThemeToPreferences(), publishState(), the heartbeat
// log and the switch-theme logs; a 6-entry array here would be an
// out-of-bounds read the moment currentTheme == THEME_CUSTOM (issue #0014
// plan, section 3 -- flagged as the single most likely crash in this phase).
// static const char* const, not const char* -- issue #0015 deferred item
// from #0014's Gotchas: a bare `const char* THEME_NAMES[]` has external
// linkage, so a second linked TU that includes this header (e.g. a second
// test_effects folder, per issue #0015's plan) would be a duplicate-symbol
// link error. `static` gives internal linkage; `const char* const` is the
// correct const-ness for a string-literal table that's never reassigned.
static const char* const THEME_NAMES[] = {
    "Green",
    "Rainbow",
    "Pink Pony Club",
    "Ocean Waves",
    "Sunset",
    "Forest",
    "Custom"
};
static_assert(sizeof(THEME_NAMES) / sizeof(THEME_NAMES[0]) == THEME_COUNT,
              "one name per theme, including THEME_CUSTOM");

// PC(hue) -- shorthand for the {hue, 255, 200} triple every built-in theme
// entry becomes under the widening. Undefined immediately after the six
// arrays so it doesn't leak into anything that includes this header.
#define PC(hue) {(uint8_t)(hue), 255, 200}

// Green Theme - Original candle-like greens
const PaletteColor GREEN_HUES[] = {
  PC(85),  // dark green
  PC(90),  // darker medium green
  PC(95),  // medium green
  PC(100), // medium-light green
  PC(105), // light green
  PC(100), // medium-light green (back down)
  PC(95),  // medium green
  PC(90)   // darker medium green (back to start)
};

// Rainbow Theme - Full spectrum cycling
const PaletteColor RAINBOW_HUES[] = {
  PC(0),   // red
  PC(32),  // orange
  PC(64),  // yellow
  PC(96),  // green
  PC(128), // cyan
  PC(160), // blue
  PC(192), // purple
  PC(224)  // magenta
};

// Pink Pony Club Theme - Pink, magenta, and pony colors
const PaletteColor PINK_PONY_HUES[] = {
  PC(200), // deep magenta
  PC(210), // bright magenta
  PC(220), // hot pink
  PC(230), // light pink
  PC(240), // pink-red
  PC(245), // cotton candy pink
  PC(250), // bubble gum pink
  PC(255)  // soft pink
};

// Ocean Waves Theme - Deep blues and teal
 const PaletteColor OCEAN_HUES[] = {
   PC(190), // deep navy
   PC(185), // dark teal
   PC(180), // medium teal
   PC(175), // light teal
   PC(170), // very light blue
   PC(165), // pale blue
   PC(160), // sky blue
   PC(155)  // light blue
 };

// Sunset Theme - dusk purple through rose and red to gold.
// Ordered cool to hot: EMBER (src/looks.h) picks a position by heat, so the
// dimmest embers sit at the purple end and the brightest at gold. The old
// table ran hue 0-85 and labelled its upper half pink, purple and indigo,
// but on FastLED's wheel 64-85 is yellow and yellow-green, so the strip went
// green at the cool end. These are real sunset hues.
const PaletteColor SUNSET_HUES[] = {
  PC(192), // dusk purple
  PC(210), // violet
  PC(226), // magenta-pink
  PC(242), // rose
  PC(0),   // red
  PC(14),  // red-orange
  PC(28),  // orange
  PC(42)   // gold
};

// Forest Theme - deep greens to sunlit leaf and moss.
// The old table ran up to hue 145, which is aqua and cyan rather than green.
// This one stays in leaf colours (68-110), and its warm end is slightly
// desaturated so moss and olive read as earthy rather than neon. Ordered so
// neighbours are close: CANOPY drifts through it by position.
const PaletteColor FOREST_HUES[] = {
  {110, 255, 200}, // deep woodland green
  {104, 255, 200}, // forest green
  { 98, 255, 200}, // green
  { 92, 250, 200}, // leaf green
  { 86, 240, 200}, // fresh leaf
  { 80, 225, 200}, // yellow-green
  { 74, 205, 200}, // moss
  { 68, 190, 200}  // olive
};

#undef PC

const int NUM_GREEN_HUES = sizeof(GREEN_HUES) / sizeof(GREEN_HUES[0]);
const int NUM_RAINBOW_HUES = sizeof(RAINBOW_HUES) / sizeof(RAINBOW_HUES[0]);
const int NUM_PINK_PONY_HUES = sizeof(PINK_PONY_HUES) / sizeof(PINK_PONY_HUES[0]);
const int NUM_OCEAN_HUES = sizeof(OCEAN_HUES) / sizeof(OCEAN_HUES[0]);
const int NUM_SUNSET_HUES = sizeof(SUNSET_HUES) / sizeof(SUNSET_HUES[0]);
const int NUM_FOREST_HUES = sizeof(FOREST_HUES) / sizeof(FOREST_HUES[0]);
