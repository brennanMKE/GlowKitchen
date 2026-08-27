# Adding New Themes to GlowKitchen

This document provides instructions for adding new built-in themes to the
GlowKitchen LED project.

## The model: a theme is an (EffectMode, color array) pair

A built-in theme is not "Green-style" or "everything-else-style" — that
`if (currentTheme == THEME_GREEN)` framing is gone as of [issue
#0014](../issues/0014.md). Every theme is a pair:

- an **`EffectMode`** (which renderer draws it — `EFFECT_BLEND`,
  `EFFECT_FLICKER`, or, as of [issue #0015](../issues/0015.md), any of the
  seven new modes: `EFFECT_CHASE`, `EFFECT_WIPE`, `EFFECT_SCAN`,
  `EFFECT_SPARKLE`, `EFFECT_PULSE`, `EFFECT_STROBE`, `EFFECT_COLORLOOP`)
- a **color array** — a `const PaletteColor[]` of `{h, s, v}` triples (see
  "Palette representation" below)

`BUILTIN_EFFECT_MODE[]` maps each of the six built-in themes to its
`EffectMode`. Adding a theme means adding both halves: a color array *and*
a `BUILTIN_EFFECT_MODE[]` entry — a step that didn't exist before issue
#0014 and is easy to forget.

### Palette representation — PaletteColor triples

As of [issue #0015](../issues/0015.md) (per [issue #0013](../issues/0013.md)'s
palette-representation decision), a color array entry is not a bare hue
byte — it's a `PaletteColor{h, s, v}` triple:

```cpp
struct PaletteColor { uint8_t h, s, v; };
```

Every built-in theme entry is `{hue, 255, 200}` — full saturation, the
historical fixed brightness value. This is what makes white, gray, and
pastel colors expressible for ad hoc custom effects (issue #0016), which
a hue-only representation could not do. `renderBlend()`'s hue-circle
interpolation still operates on the `.h` field alone — widening the storage
type did not change what `BLEND` renders (`test/test_effects/` is the proof
of that: it stays green through the widening).

### Where things live now

| Thing | File |
|---|---|
| `HueTheme` enum, `THEME_NAMES[]`, the six built-in `PaletteColor` arrays and their `NUM_*_HUES` counts, `PaletteColor` itself, `CYCLEABLE_THEME_COUNT` | `src/themes.h` |
| `EffectMode` enum, `BUILTIN_EFFECT_MODE[]`, `getCurrentEffectMode()`, `CustomEffectConfig`, `EffectState`, every renderer (`renderBlend()`, `renderFlicker()`, and the seven `render*()` functions from issue #0015) | `src/effects.h` |
| The accessors (`getCurrentColorArray()`, `getCurrentColorCount()`), the MQTT theme dispatch, `getThemeMqttCommand()`, `loadAllPreferences()`'s theme validation, `switchToNextTheme()`/`switchToPreviousTheme()` | `src/main.cpp` |

`src/effects.h` `#include`s `src/themes.h`, never the reverse — `effects.h`
needs `HueTheme` (for `CustomEffectConfig::revertTheme` and the
`BUILTIN_EFFECT_MODE[]` table), while `themes.h` has no need of anything
`EffectMode`-related. Keep it that way; the two headers would form a
circular include otherwise.

## Adding a new built-in theme

### Step 1: Insert into the `HueTheme` enum — BEFORE `THEME_CUSTOM`

This is the step most likely to be done wrong. `THEME_CUSTOM` and
`THEME_COUNT` must stay at the end:

```cpp
enum HueTheme {
    THEME_GREEN = 0,
    THEME_RAINBOW = 1,
    THEME_PINK_PONY = 2,
    THEME_OCEAN_WAVES = 3,
    THEME_SUNSET = 4,
    THEME_FOREST = 5,
    THEME_YOUR_THEME = 6,   // insert HERE, before THEME_CUSTOM
    THEME_CUSTOM = 7,       // bump
    THEME_COUNT = 8         // bump
};
```

Also bump `CYCLEABLE_THEME_COUNT` (in `src/themes.h`, next to the enum) from
6 to 7 — this is what keeps `NEXT_THEME`/`PREV_THEME` cycling exactly the
themes you want and `THEME_CUSTOM` unreachable by cycling.

Two `static_assert`s will catch a half-done job:
`sizeof(THEME_NAMES)/sizeof(THEME_NAMES[0]) == THEME_COUNT` in
`src/themes.h`, and `sizeof(BUILTIN_EFFECT_MODE)/sizeof(BUILTIN_EFFECT_MODE[0])
== CYCLEABLE_THEME_COUNT` in `src/effects.h`. If either fails to compile,
you've missed a table entry somewhere below.

**Appending at the end instead (past `THEME_CUSTOM`) is wrong** — it
collides with `THEME_CUSTOM`'s value, which is exactly the bug this
document used to describe before issue #0015.

### Step 2: Add the theme name — at the matching index, not appended

In `src/themes.h`, insert the name at the same position as the enum value
(before `"Custom"`):

```cpp
static const char* const THEME_NAMES[] = {
    "Green",
    "Rainbow",
    "Pink Pony Club",
    "Ocean Waves",
    "Sunset",
    "Forest",
    "Your Theme Name",  // matches THEME_YOUR_THEME's position
    "Custom"
};
```

### Step 3: Define the color array

In `src/themes.h`, add a `PaletteColor` array for your theme. Use the
`PC(hue)` macro pattern the existing six use (defined and `#undef`'d right
around the arrays) if it's still there, or write triples out directly:

```cpp
// Your Theme - description
const PaletteColor YOUR_THEME_HUES[] = {
    {0,   255, 200},  // first color
    {32,  255, 200},  // second color
    {64,  255, 200},  // third color
    // ... more colors as needed
};

const int NUM_YOUR_THEME_HUES = sizeof(YOUR_THEME_HUES) / sizeof(YOUR_THEME_HUES[0]);
```

A flat `255, 200` for saturation/value matches every existing built-in and
is the safe default. If your theme genuinely wants a pastel or a white
entry, this is exactly the representation that makes it possible — set
`s`/`v` accordingly per entry.

### Step 4: Add the `BUILTIN_EFFECT_MODE[]` entry — new as of issue #0015

In `src/effects.h`, add the matching entry to `BUILTIN_EFFECT_MODE[]`, at
the same index as the enum value, choosing from the nine `EffectMode`
values (see "The nine effect modes" below):

```cpp
static const EffectMode BUILTIN_EFFECT_MODE[CYCLEABLE_THEME_COUNT] = {
    EFFECT_FLICKER,     // THEME_GREEN
    EFFECT_BLEND,       // THEME_RAINBOW
    EFFECT_BLEND,       // THEME_PINK_PONY
    EFFECT_BLEND,       // THEME_OCEAN_WAVES
    EFFECT_BLEND,       // THEME_SUNSET
    EFFECT_BLEND,       // THEME_FOREST
    EFFECT_CHASE,       // THEME_YOUR_THEME -- new
};
```

This step didn't exist before issue #0014 introduced `EffectMode` — a theme
used to imply its own renderer via `currentTheme == THEME_GREEN`. Now the
two are independent, and forgetting this entry either fails the
`static_assert` (if you forgot to grow the array) or silently renders your
new theme with whatever mode happens to land at that table index (if you
grew the array but left a placeholder).

### Step 5: Update the accessors

In `src/main.cpp`, add your theme to both:

```cpp
const PaletteColor* getCurrentColorArray() {
    switch (currentTheme) {
        case THEME_GREEN: return GREEN_HUES;
        // ...
        case THEME_YOUR_THEME: return YOUR_THEME_HUES;
        case THEME_CUSTOM: return customPalette;
        default: return GREEN_HUES;
    }
}

int getCurrentColorCount() {
    switch (currentTheme) {
        case THEME_GREEN: return NUM_GREEN_HUES;
        // ...
        case THEME_YOUR_THEME: return NUM_YOUR_THEME_HUES;
        case THEME_CUSTOM: return customEffect.colorCount;
        default: return NUM_GREEN_HUES;
    }
}
```

### Step 6: Add MQTT command recognition

In `onMqttMessage()` (`src/main.cpp`), add your theme to the identification
block:

```cpp
} else if (msgUpper == "YOUR_THEME" || msgUpper == "THEME_YOUR_THEME") {
    newTheme = THEME_YOUR_THEME;
    themeIdentified = true;
}
```

### Step 7: Update `getThemeMqttCommand()`

```cpp
const char* getThemeMqttCommand(HueTheme theme) {
    switch (theme) {
        case THEME_GREEN: return "GREEN";
        // ...
        case THEME_YOUR_THEME: return "YOUR_THEME";
        default: return "GREEN";
    }
}
```

### Step 8: Add a native test fixture

In `test/test_effects/test_effects.cpp`, add your theme to `THEMES[]`:

```cpp
static const ThemeFixture THEMES[] = {
    {"Green",       GREEN_HUES,     NUM_GREEN_HUES},
    // ...
    {"Your Theme",  YOUR_THEME_HUES, NUM_YOUR_THEME_HUES},
};
```

**`static_assert(NUM_THEME_FIXTURES == CYCLEABLE_THEME_COUNT)` fails the
native build if you skip this** — a real trap, not a hypothetical one: it's
what forces the test fixtures to grow in lockstep with the theme table
instead of silently going stale.

## The nine effect modes

`BLEND` and `FLICKER` are the two original renderers, extracted in issue
#0014. The other seven were added in issue #0015 and are available to any
built-in theme's `BUILTIN_EFFECT_MODE[]` entry, not just to ad hoc custom
effects. Every mode is modulated by `speed` and `intensity` (0–255, default
128) through one documented linear mapping per knob — built-in themes get
the default 128/128 for both.

### Speed endpoints (one tick's meaning, at `speed=128`)

| mode | slowMs (speed=0) | fastMs (speed=255) | @128 | one tick does |
|---|---|---|---|---|
| `CHASE` | 40 | 2 | 21 ms | head advances 1 LED |
| `WIPE` | 40 | 2 | 21 ms | front advances 1 LED |
| `SCAN` | 25 | 1 | 13 ms | eye advances 1 LED |
| `SPARKLE` | 60 | 5 | 33 ms | spawn + one fade step |
| `PULSE` | 40 | 4 | 22 ms | phase += 1 (256 = one breath) |
| `STROBE` | 600 | 40 | 319 ms | one full on+off cycle (hard floor: 40ms/25Hz) |
| `COLORLOOP` | 60 | 2 | 31 ms | hue moves 1 unit toward target |

### Intensity mappings (at `numLeds=240`)

| mode | `intensity` modulates | @0 | @128 | @255 |
|---|---|---|---|---|
| `CHASE` | run width | 1 LED | 30 LEDs | 60 LEDs |
| `WIPE` | contrast of the un-wiped region | previous color at full brightness | ~50% | black |
| `SCAN` | band width | 1 LED | 15 LEDs | 30 LEDs |
| `SPARKLE` | spawns per tick | 1 | 3 | 6 |
| `PULSE` | trough depth | no visible pulse | dips to ~50% | dips to black |
| `STROBE` | duty cycle | ~6% on | 50% on | ~94% on |
| `COLORLOOP` | saturation | pastel (128) | 192 | pure (255) |

The three position-based modes (`CHASE`, `WIPE`, `SCAN`) advance one LED per
tick, so their traverse time scales with `numLeds` — a 500-LED strip chases
about twice as slowly as a 240-LED one at the same `speed`.

Full derivations, justifications, and the native test gates that pin these
numbers down live in [issue #0015](../issues/0015.md)'s `## Plan` and
`## Fix` — treat this table as the summary, that ticket as the source.

## Caution: persisted theme indices shift when you insert a theme

`currentTheme` is stored in NVS (`preferences`) as a raw index
(`saveThemeToPreferences()` / `loadAllPreferences()`). Inserting a new theme
before `THEME_CUSTOM` moves every enum value at or after the insertion point
up by one — most importantly, it moves `THEME_CUSTOM` itself (e.g. from 6 to
7). A device that upgrades over OTA with a persisted index equal to the
*old* `THEME_CUSTOM` value boots into whatever theme now occupies that slot,
not into custom.

This is exactly what a settings-version byte (`SETTINGS_VERSION`, see
[issue #0016](../issues/0016.md)) is for — detecting a stale NVS layout and
re-initializing rather than trusting a raw index across a schema change.
Until that lands, inserting at the end of the *cycleable* range (i.e. still
before `THEME_CUSTOM`, but after every existing theme) is the safest
available option, and even that still shifts `THEME_CUSTOM`'s own index.
Flag any theme addition that touches a fielded device for this reason.

## Theme behavior notes

- **`FLICKER`**-mode themes (currently only Green) render via
  `renderFlicker()` — no color blending, no `speed`/`intensity` response;
  it's the original per-LED candle flicker.
- **`BLEND`**-mode themes render via `renderBlend()` — smooth gradient
  scrolling with the hue-circle shortest-path interpolation described above.
- Every other mode (`CHASE` through `COLORLOOP`) responds to `speed` and
  `intensity` per the tables above, and mirrors normally
  (`mirrorEnabled`) since `loopLED()`'s mirror dispatch only special-cases
  `FLICKER`.
