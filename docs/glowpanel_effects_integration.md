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

**Grey out the intensity slider for BLEND and FLICKER only.** Those two hard-code their own values and ignore the field; the other seven all use it. Note that WIPE, PULSE and COLORLOOP use it for depth or saturation rather than for size, so the same slider changes how *vivid* the effect is rather than how *big* it is — worth a per-mode label if the UI has room for one.

Two mode-specific notes worth surfacing in the UI:

- **BLEND and FLICKER discard saturation.** Both hard-code saturation to 255 so they render byte-identically to the pre-refactor built-ins. Pastels sent to these two come out fully saturated. If the user picks a desaturated colour, steer them to CHASE, WIPE, SCAN, SPARKLE, PULSE, STROBE or COLORLOOP.
- **COLORLOOP is currently broken** — it advances one hue unit per tick, so a three-colour palette takes about 30 seconds and reads as a static colour. Tracked as issue #0018. Either hide it or label it until that lands.

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

As of issue #0020, **an active custom effect ignores theme commands arriving on `lights/all/cmd`.** Device state wins over a fleet-wide broadcast, so a Home Assistant automation publishing to `/all/` can no longer cancel a running effect.

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
3. **A "Custom" builder** — mode dropdown, 1–8 colour swatches, the two sliders with intensity disabled for the five modes that ignore it.
4. **A running-effect banner** driven by `custom.mode` and `custom.timeoutRemaining` from `STATUS`, with the Stop button attached to it.

Colour pickers hand back `#RRGGBB`, which is exactly the accepted format — no conversion needed.

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
- Issues [#0018](../issues/0018.md) (COLORLOOP crawls) and [#0019](../issues/0019.md) (colour saturation) are the two known rendering defects a UI should be aware of.
