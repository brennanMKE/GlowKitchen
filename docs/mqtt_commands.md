# MQTT Command Documentation

This document lists all available MQTT commands for the GlowKitchen lighting system.

## MQTT Topics

Commands can be sent to the following topics:

| Topic | Description |
| :--- | :--- |
| `lights/all/cmd` | Broadcast topic. All connected devices will react. |
| `lights/kitchen/cmd` | Kitchen specific topic. |
| `lights/tv/cmd` | TV specific topic. |
| `lights/desk/cmd` | Desk specific topic. |

---

## Commands

All commands should be sent as plain string payloads.

### Power Control

| Payload | Description |
| :--- | :--- |
| `ON` | Turns the LEDs ON using the current theme. |
| `OFF` | Turns the LEDs OFF (sets all to black). |
| `TOGGLE` | Toggles the global LED state between ON and OFF. |

### Theme Control

| Payload | Description |
| :--- | :--- |
| `NEXT_THEME` | Cycles to the next available theme (the six below; skips Custom). |
| `PREV_THEME` | Cycles to the previous theme (the six below; skips Custom). |
| `GREEN` | Switches immediately to the Green theme (candle flicker). |
| `RAINBOW` | Switches immediately to the Rainbow theme (gradient blend). |
| `PINK_PONY` | Switches immediately to the Pink Pony Club theme (pink/magenta). Alias: `PINK_PONY_CLUB`. |
| `OCEAN_WAVES` | Switches immediately to the Ocean Waves theme (blues/teals). Alias: `OCEAN`. |
| `SUNSET` | Switches immediately to the Sunset theme (warm oranges/pinks). |
| `FOREST` | Switches immediately to the Forest theme (greens/earth tones). |

*Note: The system also supports `SCENE_1` (Green) and `SCENE_2` (Rainbow) for backward compatibility. Every theme name above also accepts a `THEME_` prefix (e.g. `THEME_GREEN`).*

*A seventh theme, Custom, exists but is not in this cycle — it holds whatever effect `SET_EFFECT` last configured (see "Custom Effects" below) and is only reachable through `SET_EFFECT`, never through `NEXT_THEME`/`PREV_THEME` or a theme name. Sending any theme name above while a custom effect is active cancels the effect and switches normally.*

### Brightness Control

| Payload | Description |
| :--- | :--- |
| `BRIGHT_UP` | Increases brightness by 10 (Max: 225). |
| `BRIGHT_DOWN` | Decreases brightness by 10 (Min: 75). |
| `SET_BRIGHTNESS:N` | Sets absolute brightness to N (0-255). |

### Animation Control

| Payload | Description |
| :--- | :--- |
| `COLOR_CHANGE_ON` | Enables automatic color cycling/rotation within the theme. |
| `COLOR_CHANGE_OFF` | Freezes the colors in their current state. |

### Custom Effects

Ad hoc effects with arbitrary colors, independent of the six built-in themes. All prior commands above (and below) are **unaffected** by this feature — `ON`/`OFF`/`TOGGLE`/`NEXT_THEME`/`PREV_THEME`/the six theme names/`SET_BRIGHTNESS`/`SET_DEVICE_NAME`/`SET_IR_FLAG`/`STATUS`/the OTA family all behave exactly as before.

| Payload | Description |
| :--- | :--- |
| `SET_EFFECT:{...}` | Applies a custom effect with the JSON object described below. |
| `CLEAR_EFFECT` | Reverts immediately to the theme that was active before the effect was applied. |

**`SET_EFFECT:{...}`** — the payload after the prefix is a JSON object with these fields:

| Field | Required | Type | Description |
| :--- | :--- | :--- | :--- |
| `mode` | yes | string | One of the nine mode names below (case-insensitive). A bare number is **not** accepted. |
| `colors` | yes | array of strings | 1–8 colors as `#RRGGBB` or `RRGGBB` hex strings (the leading `#` is optional; shorthand `#RGB` is not supported). |
| `speed` | no | integer 0–255 | Animation speed. Default **128**. An out-of-range value (including a quoted number, e.g. `"128"`) is rejected. |
| `intensity` | no | integer 0–255 | Effect-specific intensity (run width, density, duty cycle, etc. — see the mode). Default **128**. Same rejection rule as `speed`. |
| `timeout` | no | integer, seconds | How long the effect runs before reverting on its own. Default **0** (until explicitly changed). **A value over 28800 (8 hours) is accepted and reduced to 8 hours — it is a clamp, not a rejection.** |

The nine mode names: `BLEND`, `FLICKER`, `CHASE`, `WIPE`, `SCAN`, `SPARKLE`, `PULSE`, `STROBE`, `COLORLOOP`.

Examples:

```
lights/kitchen/cmd  →  SET_EFFECT:{"mode":"CHASE","colors":["#FF6600"]}
lights/kitchen/cmd  →  SET_EFFECT:{"mode":"COLORLOOP","colors":["#FF0000","#00FF00","#0000FF","#FFFF00","#FF00FF","#00FFFF","#FFFFFF","#FFA500"],"speed":180,"intensity":200}
lights/kitchen/cmd  →  SET_EFFECT:{"mode":"STROBE","colors":["#FFFFFF"],"timeout":30}
```

Behavior notes:

*   **No topic restriction.** Unlike `OTA_URL`/`RESTART`, `SET_EFFECT` is safe on `lights/all/cmd` — it sets the whole installation to one effect, which is a normal thing to want and trivially undone with `CLEAR_EFFECT`.
*   **A malformed payload changes nothing.** On any parse failure the device stays exactly on whatever theme or effect it was already running — no partial application, no state change, no MQTT reply. **The failure is logged to serial only; silence on the broker is the expected behavior for a rejected command**, not a sign it was dropped in transit.
*   **The effect survives a reboot** — it is persisted to NVS and reloaded at boot. See "Reboot behavior" below for exactly how a `timeout` is handled across a power cycle.
*   **The timeout is acted on.** When it elapses, the strip automatically reverts to the theme that was active before `SET_EFFECT` was sent — the same thing `CLEAR_EFFECT` does manually, just on its own schedule. No MQTT command is needed to make it happen.
*   An explicit theme command (any name in "Theme Control" above) cancels an active custom effect (and its timeout) rather than leaving it armed to fire later onto a theme you've since moved away from.
*   `NEXT_THEME`/`PREV_THEME` sent while a custom effect is active land on Rainbow/Forest respectively (Custom's position in the cycle order is arbitrary but harmless), and also cancel the timeout.
*   `OFF`/`TOGGLE` do **not** cancel a timed effect — a timer keeps running (and can still expire and revert) while the strip is off; turning it back `ON` shows whatever the timeout left in place.
*   **Payload size.** The device's MQTT buffer is 512 bytes. The longest legal `SET_EFFECT` command (9-character mode name, 8 colors, all three optional fields) is well under that with a realistic device/topic name; a command rejected outright by the broker or client library before it ever reaches the device produces the same silence as a parse rejection, so keep payloads to the documented shape.

**`CLEAR_EFFECT`** — reverts immediately to the theme that was active before `SET_EFFECT` was sent (or Green, if that's not determinable), regardless of any timeout. **Explicitly safe on `lights/all/cmd`**, unlike `OTA_URL`/`RESTART` — the worst case is the whole installation returning to its normal theme, which is the recovery action anyway. A no-op if no custom effect is currently active.

**`STATUS`** — while a custom effect is active, the reply gains a `custom` object:

```json
{"theme":"Custom", ..., "custom":{"mode":"COLORLOOP","timeoutRemaining":214}}
```

The `custom` object is **omitted entirely** whenever `theme` isn't `"Custom"`, so a client that ignores unknown fields sees a byte-identical payload to before this feature existed.

| Field | Meaning |
| :--- | :--- |
| `mode` | The active effect's mode name (one of the nine above). |
| `timeoutRemaining` | Whole seconds left before the effect auto-reverts, rounded **up**. **`-1` means "no timeout" — runs until explicitly changed.** `0`–`28800` is a real remainder; `0` is reachable only in the brief window where a `STATUS` races the revert. `-1` is used instead of `0`, `null`, or omitting the key: `0` is ambiguous with "about to expire", `null`/an absent key both push a type check onto every consumer (this device's own shell scripts included), and `-1` is a single always-present integer that's unambiguously outside the legal range. |

Deliberately **no `colors`** in the `custom` object, even though this may look incomplete: `SET_EFFECT` is the only thing that sets the colors, the sender already knows them, and re-serializing up to eight `#RRGGBB` strings into every retained `STATUS` publish spends flash and wire for a field with no consumer. If this ever needs to change, it fits: the buffer is 512 bytes and the full block (mode + up to 8 colors + timeoutRemaining) is still comfortably under that.

### Reboot behavior (timed effects)

A `SET_EFFECT` with a `timeout` persists an **absolute wall-clock end time** (UTC), not just the remaining duration, specifically so a reboot handles it correctly instead of either losing the effect early or restarting its timer from full duration:

| At boot | Result |
| :--- | :--- |
| Wall clock synced, effect not yet expired | **Resumes** with the correct remaining time. |
| Wall clock synced, effect already expired while powered down | **Reverts immediately** to the prior theme. |
| Wall clock not yet synced (device just booted, no WiFi yet) | The effect keeps rendering for up to ~2 minutes while the device waits for the clock to sync; if it syncs in time, one of the two rows above applies. If it never syncs within that window, the effect **reverts**. |
| The effect was originally set while the clock was unsynced (no network at `SET_EFFECT` time) | Not resumable across a reboot at all — reverts on boot, since there is no wall-clock end time to resume from. |

A 30-second effect that was already running when the device rebooted will have expired long before any of this matters. An 8-hour event effect interrupted by a brief power blip resumes with the correct remaining time rather than restarting from 8 hours or being lost entirely.

### Hardware Configuration

These commands update the hardware settings in the device's persistent memory.

| Payload Prefix | Example | Description |
| :--- | :--- | :--- |
| `SET_NUM_LEDS:` | `SET_NUM_LEDS:150` | Sets the total number of LEDs connected (Max: 500). |
| `SET_LEDS_PER_COLOR:` | `SET_LEDS_PER_COLOR:15` | Sets the number of LEDs per color group in animations. |

### Firmware Updates

| Payload Prefix | Example | Description |
| :--- | :--- | :--- |
| `OTA_UPDATE` | `OTA_UPDATE` | Check GitHub for the latest release and install it if the tag differs. |
| `OTA_CHECK` | `OTA_CHECK` | Same as `OTA_UPDATE`. |
| `OTA_URL:` | `OTA_URL:http://192.168.1.50:8000/firmware.bin` | Install firmware directly from a URL, skipping the version check. |
| `OTA_AUTO:` | `OTA_AUTO:false` | Enable/disable the automatic startup and nightly GitHub checks. |

`OTA_URL` is deliberately restricted, because it installs an unsigned binary:

*   **Refused on `lights/all/cmd`.** It must be sent to a single device's topic, so one stray broadcast cannot re-flash the whole fleet.
*   **The host must be on the local network** — an mDNS `.local` name, an address on the device's own subnet, or a private range (`10.x`, `192.168.x`, `172.16–31.x`). Public addresses and bare DNS names are rejected.

Set `OTA_AUTO:false` before pushing a local build, otherwise the automatic check will replace it with the released tag roughly 15 seconds after the device's next boot (the comparison is "tag differs", not "tag is newer", so it downgrades). Re-enable with `OTA_AUTO:true` when finished.

---

## Helper Scripts

The following scripts are available in the `scripts/` folder to simplify common tasks:

### 1. Cycle Themes Syncronously
`./scripts/publish_next_theme.sh`
*   Tracks the current theme locally on your Mac.
*   Publishes the specific theme name to `lights/all/cmd` with the **Retain** flag.
*   Ensures all boards stay in sync even after reboots.

### 2. Configure Device Hardware
`./scripts/configure_device.sh [location] [numLeds] [optional: ledsPerColor]`
*   **Location**: `kitchen`, `tv`, `desk`, or `all`.
*   **numLeds**: The total count of LEDs.
*   **ledsPerColor**: The size of color groups.

### 3. Adjust Brightness
`./scripts/adjust_brightness.sh [location] [UP|DOWN]`
*   **Location**: `kitchen`, `tv`, `desk`, or `all`.
*   **UP**: Increases brightness.
*   **DOWN**: Decreases brightness.

### 4. Set Specific Theme
`./scripts/set_theme.sh [location] [THEME]`
*   **Location**: `kitchen`, `tv`, `desk`, or `all`.
*   **THEME**: `GREEN`, `RAINBOW`, `PINK_PONY`, `OCEAN_WAVES`, `SUNSET`, or `FOREST`.
*   Uses the **Retain** flag to keep devices in sync.
