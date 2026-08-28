# Driving Custom Effects from Glow Panel

How to build an effect picker in [Glow Panel](https://github.com/brennanMKE/glowpanel) (`~/Developer/RaspberryPi/glowpanel`) on top of the firmware's `SET_EFFECT` command: what to publish, what comes back, what the controls actually do, and the three traps that will otherwise cost an afternoon.

This is the **integration** guide. `docs/mqtt_commands.md` is the command reference and stays authoritative on payload shape; `docs/glowkitchen-custom-effects-spec.md` is the original design and is now partly out of date with what shipped. Where they disagree, this file follows the firmware.

---

## The two commands

Everything a UI needs is two payloads on `lights/<device>/cmd`.

```
SET_EFFECT:{"mode":"CHASE","colors":["#FF6600","#00AAFF"],"speed":128,"intensity":180,"timeout":300}
CLEAR_EFFECT
```

`SET_EFFECT:` is a literal prefix followed by a JSON object — the payload as a whole is **not** JSON, so build it as a string with a JSON body, not by marshalling a wrapper struct.

| Field | Required | Type | Notes |
|---|---|---|---|
| `mode` | yes | string | One of the nine names below. Case-insensitive. A number is rejected. |
| `colors` | yes | array | 1–8 strings, `#RRGGBB` or `RRGGBB`. Three-digit `#RGB` is **not** supported. |
| `speed` | no | int 0–255 | Default 128. **0 is slowest, 255 fastest.** A quoted `"128"` is rejected. |
| `intensity` | no | int 0–255 | Default 128. Meaning is per-mode — see the table below. Same quoting rule. |
| `timeout` | no | int seconds | Default 0 = runs until changed. Over 28800 (8 h) is clamped down, not rejected. |

`CLEAR_EFFECT` takes no arguments and reverts to whatever theme was showing before the effect. It is a no-op when no effect is active, so a "Stop" button never needs to be conditionally disabled.

---

## The nine modes, and what the two sliders do to each

`speed` always maps to a frame interval — higher is faster, in every mode. `intensity` means something different in each one, which is the part a UI has to get right if the slider is going to feel connected to anything.

| Mode | `intensity` controls | Sensible range on a short strip |
|---|---|---|
| `BLEND` | *nothing* — ignored | n/a |
| `FLICKER` | *nothing* — ignored | n/a |
| `CHASE` | Width of the travelling run, 1 LED → about a third of the strip | 100–255 |
| `WIPE` | Contrast between the filled part and the part still to come — 255 leaves the unfilled section black, 0 makes both equally bright | 150–255 |
| `SCAN` | Width of the sweeping band, same mapping as CHASE | 100–255 |
| `SPARKLE` | How many LEDs are lit at once (density) | 60–255 |
| `PULSE` | Depth of the breath — 255 fades to black at the bottom, 0 barely dims | 150–255 |
| `STROBE` | Duty cycle — how much of each period is "on" | 30–200 |
| `COLORLOOP` | Saturation, mapped to the top half of the range (`128 + intensity/2`), so even 0 stays reasonably colourful | any |

`COLORLOOP` is also the one mode that ignores the device's "colour change" flag entirely — cycling is the whole effect, so it keeps looping regardless ([#0018](../issues/0018.md)).

**Grey out the intensity slider for BLEND and FLICKER only.** Those two hard-code their own values and ignore the field; the other seven all use it. Note that WIPE, PULSE and COLORLOOP use it for depth or saturation rather than for size, so the same slider changes how *vivid* the effect is rather than how *big* it is — worth a per-mode label if the UI has room for one.

Two mode-specific notes worth surfacing in the UI:

- **BLEND and FLICKER discard saturation.** Both hard-code saturation to 255 so they render byte-identically to the pre-refactor built-ins. Pastels sent to these two come out fully saturated. If the user picks a desaturated colour, steer them to CHASE, WIPE, SCAN, SPARKLE, PULSE, STROBE or COLORLOOP.
- **COLORLOOP was fixed in [#0018](../issues/0018.md)** and now cycles a three-colour palette in a few seconds instead of ~30. Its `speed` slider is the one that changed meaning most: it scales the hue *step*, not just the frame interval, so the low end is genuinely slow rather than static.

### Speed on short strips

The renderers were tuned for roughly 240 LEDs. On the 10-LED dev board anything above ~150 crosses the whole strip in a fraction of a second and reads as a flash rather than a movement. If Glow Panel knows a device's `numLeds` (it does — `STATUS` reports it), scale the slider's usable range by strip length rather than exposing a raw 0–255 that is unusable at one end.

---

## Reading state back

Publish `STATUS` to `lights/<device>/cmd`; the device replies on `lights/<device>/state`. Glow Panel already subscribes to `lights/+/state` in `mqtt.go`, so this needs no new plumbing.

While an effect is running the reply gains one object:

```json
{"theme":"Custom", "numLeds":10, "brightness":113, ...,
 "custom":{"mode":"SPARKLE","timeoutRemaining":56}}
```

- The `custom` key is **absent** whenever `theme` is not `"Custom"`. Treat absence as "no effect", not as an error.
- `timeoutRemaining` is whole seconds, rounded up. **`-1` means no timeout** — an effect running until explicitly changed. It is never `null` and the key is never missing while `custom` is present, so a plain int decode is safe.
- There is deliberately **no `colors` field.** The panel that sent the effect already knows the colours; keep them in the panel's own state if you want to redisplay them. Do not expect to recover them from the device.

For a countdown display, poll `STATUS` rather than running a local timer — the device's clock is the one that matters, and it survives reboots that the panel's timer will not.

---

## Three traps

### 1. Do not publish effects retained

`mqtt.go`'s `Publish` takes a `retain` flag, and `app.go`'s `SetTheme` currently passes **`true` unconditionally**:

```go
if err := a.broker.Publish(id, true); err != nil {
```

A retained command is redelivered to every device on every reconnect, forever, by anything subscribed to that topic. This has already cost real debugging time on this project: a retained `FOREST` sitting on `lights/all/cmd` snapped the dev board back to Forest seconds after every effect was applied, and survived reboots of both the board and the broker, because nothing was publishing it — MQTT was replaying it.

**Publish `SET_EFFECT` and `CLEAR_EFFECT` with `retain: false`.** An effect is an event, not a configuration. A retained `SET_EFFECT` with a `timeout` is especially bad: every reconnect restarts the countdown.

Worth fixing the theme path at the same time, or at least routing it through the configurable `a.cfg.Retain` the way `SetBrightness` and `SetPower` already do, rather than a hard-coded `true`.

### 2. Publish to the device topic, not the broadcast topic

As of [#0021](../issues/0021.md), **an active custom effect ignores theme commands arriving on `lights/all/cmd`.** Device state wins over a fleet-wide broadcast, so a Home Assistant automation publishing to `/all/` can no longer cancel a running effect.

Commands sent to a device's own topic (`lights/<device>/cmd`) are treated as someone deliberately taking control of that strip and still override an effect immediately. Glow Panel already publishes per-device — keep it that way. If you add a broadcast path, know that a theme sent there will be ignored by any device currently running an effect, while `SET_EFFECT` and `CLEAR_EFFECT` still work on `/all/` normally.

### 3. A rejected effect is silent

On any parse failure the device changes nothing and **publishes nothing** — no error topic, no NACK. The rejection is logged to serial only. Silence is indistinguishable from a lost message.

So validate in the panel before publishing, because the device will not tell you what was wrong:

- `mode` is one of the nine names
- `colors` has 1–8 entries, each 6 hex digits after an optional `#`
- `speed` and `intensity` are unquoted integers 0–255
- `timeout` is an unquoted integer

Then confirm by effect rather than by reply: publish, wait, and poll `STATUS`. If `theme` came back `"Custom"` with the mode you sent, it landed. A round trip is about 150 ms on a healthy device.

The device's MQTT buffer is 512 bytes. The largest legal payload (9-character mode, 8 colours, all three optional fields) fits comfortably, but a client-side length guard is cheap insurance.

---

## Suggested UI shape

The nine presets in `scripts/demo_effect.sh` are a working set of defaults, already tuned, and worth shipping as one-tap buttons before any custom builder:

| Preset | Mode | Colours | speed | intensity | Default seconds |
|---|---|---|---|---|---|
| Dolly | `SPARKLE` | `#FF69B4` `#FF1493` `#FFB6C1` | 128 | 255 | 60 |
| Cylon | `SCAN` | `#FF0000` | 0 | 255 | 60 |
| Wipe | `WIPE` | `#FF0000` | 140 | 200 | 60 |
| Chase | `CHASE` | `#FF6600` `#00AAFF` | 150 | 200 | 60 |
| Candle | `FLICKER` | `#FF7000` `#FF3000` `#FFA000` | 100 | 180 | 300 |
| Breathe | `PULSE` | `#0033FF` | 90 | 220 | 180 |
| Strobe | `STROBE` | `#FFFFFF` | 200 | 128 | 15 |
| Loop | `COLORLOOP` | `#FF0000` `#00FF00` `#0000FF` | 255 | 255 | 60 |
| Blend | `BLEND` | `#FF0000` `#FFAA00` | 120 | 128 | 60 |

A reasonable build order:

1. **Preset buttons + a Stop button.** Nine `SET_EFFECT` payloads and one `CLEAR_EFFECT`, all `retain: false`. This alone is most of the value.
2. **A duration control** on each preset — a few fixed choices (5 min / 1 h / until stopped) beats a free-text seconds field. `0` means "until stopped".
3. **A "Custom" builder** — mode dropdown, 1–8 colour swatches, the two sliders with intensity disabled for BLEND and FLICKER. See **Custom colours** below: which modes show the whole palette at once, which discard saturation, and why swatch order matters.
4. **A running-effect banner** driven by `custom.mode` and `custom.timeoutRemaining` from `STATUS`, with the Stop button attached to it.


---

## Custom colours

This is the part Glow Panel has no UI for today, and the part with the most
firmware behaviour hiding behind it. `colors` is 1–8 `#RRGGBB` strings, but
**how the palette is consumed differs per mode**, and three modes throw part of
each colour away. A swatch editor that ignores this will produce effects that
look nothing like what the user picked.

### How each mode consumes the palette

| Mode | Uses | What the user sees |
|---|---|---|
| `SPARKLE` | **All colours at once**, by LED position (`colors[i % count]`) | Every colour visible simultaneously, interleaved along the strip |
| `WIPE` | **Two at once** — the incoming colour and the one it is replacing | The palette advancing, one hand-off at a time |
| `CHASE` | One at a time, advances each cycle | Colours in sequence |
| `SCAN` | One at a time, advances each sweep | Colours in sequence |
| `PULSE` | One at a time, advances each breath | Colours in sequence |
| `STROBE` | One at a time, advances each flash | Colours in sequence |
| `COLORLOOP` | **Hue only** — walks between successive hues | A continuous sweep through the palette's hues |
| `BLEND` | Hues only, saturation forced to 255 | Gradient across the palette |
| `FLICKER` | Hues only, saturation forced to 255 | Candle flicker across the palette |

Two consequences worth designing around:

- **SPARKLE is the only mode that shows the whole palette at once.** It is the
  best preview mode for a palette the user is building, and the only one where
  "what do these eight colours look like together" has a spatial answer.
- **Order matters everywhere except SPARKLE**, where order maps to LED position
  rather than to time. For the six sequential modes, reordering swatches
  reorders the animation. A drag-to-reorder swatch list is therefore doing real
  work, not decoration.

### What gets discarded

| Mode | Discarded | Practical effect |
|---|---|---|
| `BLEND`, `FLICKER` | Saturation and value — both forced to full | A pastel comes out fully saturated. `#FFB6C1` renders as vivid pink, not the soft pink shown in the picker. |
| `COLORLOOP` | Saturation and value — saturation comes from `intensity` instead | Every colour renders at the same saturation regardless of what was picked. |
| the other six | nothing | Full HSV of each colour is used. |

**So: if the user picks any desaturated colour, steer them away from BLEND,
FLICKER and COLORLOOP.** The cleanest way to surface this is to disable those
three modes (with a reason) the moment a swatch is not fully saturated, rather
than letting the user pick a combination the firmware will quietly flatten.

### What you pick is not exactly what you get

Colours are converted to HSV on the device and back to RGB by FastLED's
`hsv2rgb_rainbow()`, which warps hue deliberately to produce more pleasing
yellows. It does not round-trip exactly. Measured on a real strip with the
three Dolly pinks:

| Requested | Rendered | Blue/Red requested | Blue/Red rendered |
|---|---|---|---|
| `#FF1493` | `#26000c` | 0.58 | 0.32 |
| `#FF69B4` | `#290812` | 0.71 | 0.44 |
| `#FFB6C1` | `#31191b` | 0.76 | 0.55 |

Everything shifts somewhat toward red, and the strip renders at roughly half
the configured brightness. Colours stay correct *relative to each other* and
clearly in the right family — see [#0019](../issues/0019.md) — but a swatch
preview in the panel will look brighter and cooler than the strip does. Do not
promise a colour match; a preview is an approximation.

### Building the payload

`colors` is the only field that needs care in Go — the rest are plain ints.
There is no `SetEffect` on `App` yet; this is the shape it wants:

```go
// SetEffect publishes a custom effect. colors are "#RRGGBB" strings, 1-8 of
// them; speed and intensity are 0-255; timeoutSec 0 means "until changed".
func (a *App) SetEffect(mode string, colors []string, speed, intensity, timeoutSec int) string {
    if a.broker == nil {
        return "not ready"
    }
    if len(colors) < 1 || len(colors) > 8 {
        return "pick between 1 and 8 colors"
    }
    quoted := make([]string, len(colors))
    for i, c := range colors {
        if !hexColor.MatchString(c) {          // ^#?[0-9A-Fa-f]{6}$ -- no #RGB shorthand
            return "bad color: " + c
        }
        quoted[i] = `"` + c + `"`
    }
    payload := fmt.Sprintf(
        `SET_EFFECT:{"mode":"%s","colors":[%s],"speed":%d,"intensity":%d,"timeout":%d}`,
        mode, strings.Join(quoted, ","), speed, intensity, timeoutSec)

    // retain MUST be false -- a retained effect restarts its countdown on
    // every reconnect, forever. See "Do not publish effects retained" above.
    if err := a.broker.Publish(payload, false); err != nil {
        return err.Error()
    }
    return ""
}

func (a *App) ClearEffect() string {
    if a.broker == nil {
        return "not ready"
    }
    if err := a.broker.Publish("CLEAR_EFFECT", false); err != nil {
        return err.Error()
    }
    return ""
}
```

Validate in Go rather than only in the UI: the device answers a bad payload
with silence, so a typo that slips through is indistinguishable from a dropped
message.

### A swatch editor that fits the existing frontend

`frontend/dist/app.js` is ~450 lines of hand-rolled DOM with no framework, so
this stays in the same idiom — no build step, no dependency:

- **A row of swatches, 1–8.** Each is `<input type="color">`, which hands back
  `#rrggbb` lowercase — already the accepted format, no conversion. Add a `+`
  button up to 8 and an `x` per swatch down to 1.
- **Reordering** via drag, or simply left/right arrows per swatch. Cheaper to
  build and it matters for six of the nine modes.
- **A live strip preview** — ten `<div>`s coloured by the same rule the
  firmware uses for the selected mode (`colors[i % count]` for SPARKLE, the
  first colour for the sequential modes). Approximate, and worth labelling as
  such given the hue shift above.
- **Save a palette by name** in the existing config, so "Dolly" and
  "Christmas" survive a restart. The device never stores or reports colours —
  `STATUS` deliberately omits them — so the panel is the only place they can
  live.

Keep the nine built-in presets as one-tap buttons alongside the builder. Most
uses are "make it look like Christmas for an hour", not "compose a palette".

---

## Testing against real hardware

The dev board is `84fce68774a4`. Everything above can be exercised from the shell before writing any Go:

```bash
./scripts/demo_effect.sh --list
./scripts/demo_effect.sh 84fce68774a4 dolly 20
./scripts/demo_effect.sh 84fce68774a4 chase 30 --speed 20 --intensity 200
./scripts/demo_effect.sh 84fce68774a4 stop
./scripts/get_device_status.sh 84fce68774a4
```

To watch what the firmware makes of a payload — including the reason for a rejection, which never reaches the broker — run this alongside, in another terminal:

```bash
./scripts/monitor_serial.py /tmp/glow.log && tail -f /tmp/glow.log
```

Use that script rather than `pio device monitor` or anything pyserial-based: attaching with pyserial drives DTR/RTS, which the ESP32-C3 reads as a reset request and reboots the board mid-test. See issue #0020.

## Related

- `docs/mqtt_commands.md` — full command reference, including reboot semantics for timed effects.
- `docs/glowkitchen-custom-effects-spec.md` — original design. Predates the implementation; `colors` in the `STATUS` reply and the ArduinoJson parser described there were both dropped.
- [#0018](../issues/0018.md) (COLORLOOP rate) and [#0019](../issues/0019.md) (colour saturation) are both resolved. #0019 records the residual hue shift described under **Custom colours**, which is `hsv2rgb_rainbow()` behaviour rather than a bug.
