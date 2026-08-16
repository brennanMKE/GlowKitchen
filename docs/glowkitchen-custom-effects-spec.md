# GlowKitchen: Ad Hoc Custom Effects Spec

## Goal

Let users pick their own colors and effect (blend, flicker, chase, etc.) over
MQTT, with an optional auto-revert timeout — **without breaking the existing
IR remote, control panel, MQTT commands, or theme-cycling behavior.**

Built-in themes stop being special-cased code paths and become named presets
of the same generic effect engine that ad hoc requests use. Rainbow becomes
"BLEND effect with the Rainbow color set." Green becomes "FLICKER effect
with the Green color set." Same output, same commands, new engine underneath.

---

## Compatibility guarantees

- `GREEN`, `RAINBOW`, `PINK_PONY`, `OCEAN_WAVES`, `SUNSET`, `FOREST` — unchanged.
  Same strings, same behavior, same visual output.
- `NEXT_THEME` / `PREV_THEME` — cycle only through the original 6 built-ins,
  in the original order. Custom effects are never inserted into the cycle.
- `ON`, `OFF`, `TOGGLE`, `SET_BRIGHTNESS`, `SET_DEVICE_NAME`, `SET_IR_FLAG`,
  `STATUS` — unchanged.
- IR remote and control panel — unchanged; they only ever send the commands
  above, so nothing about them needs to know custom effects exist.
- Persistence (NVS) — existing fields keep their meaning. New fields are
  additive.

---

## Data model changes

### 1. New effect mode, orthogonal to theme

```cpp
enum EffectMode {
    EFFECT_BLEND = 0,      // existing slowBlend() behavior
    EFFECT_FLICKER = 1,    // existing flickerLEDs() behavior
    EFFECT_CHASE = 2,      // new
    EFFECT_WIPE = 3,       // new
    EFFECT_SCAN = 4,       // new (Larson/Cylon)
    EFFECT_SPARKLE = 5,    // new
    EFFECT_PULSE = 6,      // new (breathe)
    EFFECT_STROBE = 7,     // new
    EFFECT_COLORLOOP = 8   // new
};
```

Each built-in theme gets a fixed `EffectMode` mapping so nothing about their
rendering changes:

| Theme       | EffectMode      |
|-------------|------------------|
| Green       | EFFECT_FLICKER   |
| Rainbow     | EFFECT_BLEND     |
| Pink Pony   | EFFECT_BLEND     |
| Ocean Waves | EFFECT_BLEND     |
| Sunset      | EFFECT_BLEND     |
| Forest      | EFFECT_BLEND     |

### 2. New theme slot for ad hoc state

```cpp
enum HueTheme {
    THEME_GREEN = 0,
    THEME_RAINBOW = 1,
    THEME_PINK_PONY = 2,
    THEME_OCEAN_WAVES = 3,
    THEME_SUNSET = 4,
    THEME_FOREST = 5,
    THEME_CUSTOM = 6,      // new — holds ad hoc user config
    THEME_COUNT = 7
};

// NEXT_THEME / PREV_THEME must clamp/modulo against 6, not THEME_COUNT,
// so THEME_CUSTOM is unreachable by cycling.
const int CYCLEABLE_THEME_COUNT = 6;
```

### 3. Runtime custom effect config

```cpp
#define MAX_CUSTOM_COLORS 8

struct CustomEffectConfig {
    EffectMode mode;
    CRGB colors[MAX_CUSTOM_COLORS];
    uint8_t colorCount;
    uint8_t speed;         // 0-255, default 128
    uint8_t intensity;     // 0-255, meaning is per-effect (flicker depth,
                            // chase width, sparkle density, etc.), default 128
    unsigned long timeoutMs;   // 0 = no timeout
    unsigned long activatedAt; // millis() when this config was applied
    HueTheme revertTheme;      // theme active before custom was applied
};

CustomEffectConfig customEffect;
```

### 4. Generalize the render lookup

Existing `getCurrentHueArray()` / `getCurrentHueCount()` stay as-is for the
6 built-ins. Add a parallel lookup for effect mode and color source that the
renderer actually calls:

```cpp
EffectMode getCurrentEffectMode() {
    if (currentTheme == THEME_CUSTOM) return customEffect.mode;
    return BUILTIN_EFFECT_MODE[currentTheme]; // static table from section 1
}

// Returns either a built-in hue array (converted to CRGB) or
// customEffect.colors, depending on currentTheme.
const CRGB* getCurrentColorArray();
int getCurrentColorCount();
```

The main render loop switches on `getCurrentEffectMode()` instead of the old
`if (currentTheme == THEME_GREEN) flickerLEDs(); else slowBlend();` check.
`slowBlend` and `flickerLEDs` become `renderBlend()` and `renderFlicker()`,
joined by `renderChase()`, `renderWipe()`, `renderScan()`, `renderSparkle()`,
`renderPulse()`, `renderStrobe()`, `renderColorloop()` — each taking the
color array/count and `speed`/`intensity` from `customEffect` (built-ins pass
fixed defaults for speed/intensity so their look doesn't change).

---

## New MQTT command

Add one new command, `SET_EFFECT`, alongside the existing string commands.
Payload is JSON:

```
SET_EFFECT:{"mode":"CHASE","colors":["#FF6600","#00AAFF"],"speed":128,"intensity":180,"timeout":300}
```

Fields:
- `mode` — required. One of `BLEND`, `FLICKER`, `CHASE`, `WIPE`, `SCAN`,
  `SPARKLE`, `PULSE`, `STROBE`, `COLORLOOP`.
- `colors` — required, 1–8 hex strings.
- `speed` — optional, 0–255, default 128.
- `intensity` — optional, 0–255, default 128.
- `timeout` — optional, seconds. If present and > 0, device reverts to the
  theme that was active before this command after that many seconds.
  If omitted or 0, stays until explicitly changed.

On receipt:
1. Parse JSON (pull in ArduinoJson if not already a dependency — check
   `platformio.ini` first).
2. If `currentTheme != THEME_CUSTOM`, save it into `customEffect.revertTheme`.
3. Populate `customEffect` from the payload, set `activatedAt = millis()`.
4. Set `currentTheme = THEME_CUSTOM`.
5. Persist to NVS (same pattern as existing `saveSettings()`).

Add a lightweight `CLEAR_EFFECT` command too, so users/panel can explicitly
drop back to the pre-custom theme without waiting on a timeout:

```
CLEAR_EFFECT
```

→ sets `currentTheme = customEffect.revertTheme`.

### Timeout check

In the main loop, alongside existing `EVERY_N_MILLISECONDS` state checks:

```cpp
if (currentTheme == THEME_CUSTOM && customEffect.timeoutMs > 0 &&
    millis() - customEffect.activatedAt >= customEffect.timeoutMs) {
    currentTheme = customEffect.revertTheme;
    saveSettings();
}
```

### STATUS response

Extend the existing `STATUS` reply to include whether a custom effect is
active and its remaining timeout, without changing any existing fields:

```json
{
  "theme": "CUSTOM",
  "custom": {
    "mode": "CHASE",
    "colors": ["#FF6600", "#00AAFF"],
    "timeoutRemaining": 214
  }
}
```

If `currentTheme != THEME_CUSTOM`, omit the `custom` block entirely — old
clients parsing `STATUS` see no change.

---

## OTA rollout compatibility

Devices update via GitHub Releases OTA, so this ships to units that are
already running the old firmware and old NVS layout — there's no clean-flash
guarantee.

- **NVS default safety**: `customEffect` occupies NVS space that didn't
  exist in the old settings blob. On first boot after OTA, explicitly
  zero-initialize `customEffect` (mode = EFFECT_BLEND, colorCount = 0,
  timeoutMs = 0) rather than trusting whatever bytes happen to be in that
  region. Add a settings-version byte to the persisted struct (e.g.
  `SETTINGS_VERSION = 2`); if the stored version doesn't match, reset
  `customEffect` to defaults and rewrite. This also protects future changes.
- **`currentTheme == THEME_CUSTOM` guard**: if a device somehow boots with
  `currentTheme` read as `THEME_CUSTOM` but `customEffect` is uninitialized
  (version mismatch above), fall back to `THEME_GREEN` rather than rendering
  an empty color array.
- **Partition/size check**: adding ArduinoJson increases binary size. Check
  `platformio.ini` / partition table for OTA partition headroom before
  merging — an OTA push that doesn't fit fails without a clear error to the
  end user.
- **Malformed JSON safety**: `SET_EFFECT` payloads come from users typing
  MQTT messages by hand or from scripts — validate `mode` against the known
  enum values and `colors` count (1–8) before applying, and ignore the
  message (keep prior state) on any parse failure rather than applying a
  partial config. This matters more with OTA in the picture since a bad
  state that only manifests after a specific malformed message is harder to
  debug remotely than one you can just walk over and re-flash.

---

## Docs to update

- `docs/add_new_theme.md` — add a short section noting built-in themes now
  map to `(EffectMode, color array)` pairs; the step-by-step for adding a
  *new built-in* theme is otherwise unchanged.
- Whatever doc covers MQTT commands (mentioned in README, presumably a
  separate file) — add `SET_EFFECT` and `CLEAR_EFFECT` with the JSON schema
  above; explicitly note all prior commands are unaffected.
- `README.md` feature list — add a line under Dynamic Visual Themes for the
  ad hoc effect system.

---

## Suggested build order

1. Introduce `EffectMode` enum + `BUILTIN_EFFECT_MODE[]` table; rewire
   render loop to dispatch on mode. Verify all 6 built-ins look identical
   (regression check before adding anything new).
2. Add `THEME_CUSTOM` + `CustomEffectConfig`, wire `getCurrentColorArray()`
   for it. Confirm `NEXT_THEME`/`PREV_THEME` still skip it.
3. Implement new render functions one at a time: Chase first (explicitly
   requested), then Wipe/Scan/Sparkle/Pulse/Strobe/Colorloop.
4. Add `SET_EFFECT` / `CLEAR_EFFECT` MQTT parsing + NVS persistence.
5. Add timeout check in main loop.
6. Extend `STATUS` JSON.
7. Update docs.
