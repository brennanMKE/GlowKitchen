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

// 7 entries -- THEME_CUSTOM included. THEME_NAMES[] is indexed unguarded by
// currentTheme in saveThemeToPreferences(), publishState(), the heartbeat
// log and the switch-theme logs; a 6-entry array here would be an
// out-of-bounds read the moment currentTheme == THEME_CUSTOM (issue #0014
// plan, section 3 -- flagged as the single most likely crash in this phase).
const char* THEME_NAMES[] = {
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

// Green Theme - Original candle-like greens
const uint8_t GREEN_HUES[] = {
  85,  // dark green
  90,  // darker medium green
  95,  // medium green
  100, // medium-light green
  105, // light green
  100, // medium-light green (back down)
  95,  // medium green
  90   // darker medium green (back to start)
};

// Rainbow Theme - Full spectrum cycling
const uint8_t RAINBOW_HUES[] = {
  0,   // red
  32,  // orange
  64,  // yellow
  96,  // green
  128, // cyan
  160, // blue
  192, // purple
  224  // magenta
};

// Pink Pony Club Theme - Pink, magenta, and pony colors
const uint8_t PINK_PONY_HUES[] = {
  200, // deep magenta
  210, // bright magenta
  220, // hot pink
  230, // light pink
  240, // pink-red
  245, // cotton candy pink
  250, // bubble gum pink
  255  // soft pink
};

// Ocean Waves Theme - Deep blues and teal
 const uint8_t OCEAN_HUES[] = {
   190, // deep navy
   185, // dark teal
   180, // medium teal
   175, // light teal
   170, // very light blue
   165, // pale blue
   160, // sky blue
   155  // light blue
 };

// Sunset Theme - Warm oranges and pinks
 const uint8_t SUNSET_HUES[] = {
   0,   // deep red
   5,   // dark red-orange
   10,  // red-orange
   15,  // orange-red
   20,  // bright orange
   25,  // orange-yellow
   30,  // yellow-orange
   35,  // bright yellow
   40,  // warm yellow
   45,  // soft orange-yellow
   50,  // warm pink-orange
   55,  // soft pink
   60,  // light pink
   65,  // pale pink
   70,  // soft purple-pink
   75,  // deep purple
   80,  // very dark purple
   85   // deep indigo
 };

// Forest Theme - Natural greens and earth tones
 const uint8_t FOREST_HUES[] = {
   70,  // deep forest green
   75,  // dark forest green
   80,  // medium forest green
   85,  // light forest green
   90,  // moss green
   95,  // olive green
   100, // forest blue-green
   105, // earth brown-green
   110, // dark moss green
   115, // forest olive
   120, // deep woodland green
   125, // forest twilight green
   130, // dark forest night green
   135, // verdant forest green
   140, // deep forest emerald
   145  // rich forest jungle green
 };

const int NUM_GREEN_HUES = sizeof(GREEN_HUES) / sizeof(GREEN_HUES[0]);
const int NUM_RAINBOW_HUES = sizeof(RAINBOW_HUES) / sizeof(RAINBOW_HUES[0]);
const int NUM_PINK_PONY_HUES = sizeof(PINK_PONY_HUES) / sizeof(PINK_PONY_HUES[0]);
const int NUM_OCEAN_HUES = sizeof(OCEAN_HUES) / sizeof(OCEAN_HUES[0]);
const int NUM_SUNSET_HUES = sizeof(SUNSET_HUES) / sizeof(SUNSET_HUES[0]);
const int NUM_FOREST_HUES = sizeof(FOREST_HUES) / sizeof(FOREST_HUES[0]);
