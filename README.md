# GlowKitchen

GlowKitchen is an ESP32-C3 based LED controller designed for kitchen installations and other decorative lighting environments. It supports multiple visual themes, remote control via IR, and advanced network control via MQTT.

## Features

- **Living Themes**: each built-in theme has its own animation (`src/looks.h`), and changing theme cross-fades over about 2 s, ending on a whole second so strips told together finish together.
  - **Green**: soft candle (`CANDLE`): eased per-LED flicker with a little hue jitter
  - **Rainbow**: travelling gradient with a brightness ripple (`FLOW`)
  - **Pink Pony Club**: pink gradient with white and hot-pink glitter (`GLITTER`)
  - **Ocean Waves**: interfering swells with pale foam on the crests (`WAVES`)
  - **Sunset**: embers whose colour follows their heat, purple to gold (`EMBER`)
  - **Forest**: drifting light and shade with warm sun flecks (`CANOPY`)

  The mode names also work in `SET_EFFECT`, where `speed` and `intensity` (128 = the theme's own setting) tune them.
- **Dual Control Methods**:
  - **IR Remote**: Supports standard NEC protocol remotes for direct control (brightness, theme switching, power, auto-color change).
  - **MQTT Network Control**: Full integration with home automation systems like Home Assistant.
- **Smart Device Identification**: Automatically generates a unique ID from the hardware MAC address and supports setting a custom device name (e.g., "kitchen", "bar") via MQTT.
- **Robust MQTT Handling**: Supports case-insensitive commands and persistent subscriptions to both its hardware ID and custom name.
- **Late-Night Warning**: On the hour from 7pm through 11pm, the brightness swells up and down for 15 seconds, leaving the effect and colors as they are.
- **Pulse**: A `PULSE` command flashes the strip once (a dip, a rise to a white-sparked peak, a quick return), the same pulse the GeminiLamps use, then leaves it exactly as it was.
- **Persistence**: Remembers your theme, brightness, device name, and IR settings even after a power loss.
- **Always-on IR**: IR functionality is always included in the firmware, with a runtime setting to enable or disable processing.

## Hardware Platform

- **Microcontroller**: ESP32-C3
- **LEDs**: WS2812B (addressable) on DATA_PIN 4.
- **IR Receiver**: Connected to GPIO 3.
- **Button (optional)**: between GPIO 5 and GND; the internal pull-up is used, so no resistor. Don't use GPIO 2, 8 or 9 -- they are the C3's strapping pins, and a button on GPIO 2 left a board unable to boot or take a flash.

### Button

| Press | Does |
|---|---|
| Tap | Brightness under 50%: every strip to 100%. Otherwise steps the work light: look -> soft white -> full white -> look |
| Double tap | Next look |
| Hold 3 s | Every strip to 20%, and the work light off |

A tap acts about 0.3 s after release, while the board waits to see whether a second tap follows. "Every strip" is `lights/all/cmd`, as with the remote's Vol+/Vol-.

The **work light** is solid white for close work: soft is a slightly warm white at brightness 110, full is plain white at the ceiling (225). It fades in over 400 ms, is sent a few times once settled and then not again, so nothing on the data line can make it flicker. It isn't saved -- a power cut brings the strip back on its theme -- and any theme or effect command ends it. Over MQTT: `WORK_LIGHT` steps it like a tap, `WORK_LIGHT:SOFT|FULL|OFF` sets it, and state reports `"workLight"`.

## Configuration

The project uses a configuration file `lib/Config/Config.h` to store sensitive credentials and initial settings. This file is ignored by git to keep your information secure.

1.  Copy `lib/Config/Config.h.sample` to `lib/Config/Config.h`.
2.  Update your WiFi and MQTT broker credentials.
3.  Set your desired initial defaults.

## MQTT Control

Each device listens on the following topics:
- `lights/all/cmd` (Broadcast to all devices)
- `lights/[device_id]/cmd` (Unique hardware ID, e.g., `lights/7cdfa1123456/cmd`)
- `lights/[device_name]/cmd` (Custom name if set, e.g., `lights/kitchen/cmd`)

### Common Commands (Case-Insensitive)
- `ON` / `OFF`: Toggle power.
- `TOGGLE`: Toggle between ON/OFF states.
- `NEXT_THEME` / `PREV_THEME`: Cycle through available themes.
- `SET_BRIGHTNESS:[0-255]`: Set LED brightness.
- `SET_DEVICE_NAME:[name]`: Assign a friendly name to the device.
- `SET_IR_FLAG:[true/false]`: Enable or disable the IR receiver.
- `SET_MAX_MA:N`: Set the strip's power limit in mA (100-1000, default 500).
- `STATUS`: Request the current state of the device.
- `SWELL`: Run the late-night brightness swell now.
- `PULSE`, `PULSE:<ms>`, `PULSE:<ms>:<level>`: Flash the strip once (default 750 ms, 300-5000; level 1-255 peak, 0 or omitted = full), the same pulse as the GeminiLamps. An overlay: nothing is saved, ignored while off, never send it retained. See [docs/mqtt_commands.md](docs/mqtt_commands.md#pulse).

Themes can also be set directly by name: `GREEN`, `RAINBOW`, `PINK_PONY`, `OCEAN`, `SUNSET`, `FOREST`.

## Adding New Themes

For a detailed guide on adding your own visual themes, see [docs/add_new_theme.md](docs/add_new_theme.md).

## Build Instructions

This project uses [PlatformIO](https://platformio.org/).

```bash
# Build the project
pio run

# Upload to device
pio run --target upload

# Monitor serial output
pio run --target monitor
```
