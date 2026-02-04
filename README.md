# GlowKitchen

GlowKitchen is an ESP32-C3 based LED controller designed for kitchen installations and other decorative lighting environments. It supports multiple visual themes, remote control via IR, and advanced network control via MQTT.

## Features

- **Dynamic Visual Themes**: Includes Green (flicker), Rainbow, Pink Pony Club, Ocean Waves, Sunset, and Forest.
- **Dual Control Methods**:
  - **IR Remote**: Supports standard NEC protocol remotes for direct control (brightness, theme switching, power, auto-color change).
  - **MQTT Network Control**: Full integration with home automation systems like Home Assistant.
- **Smart Device Identification**: Automatically generates a unique ID from the hardware MAC address and supports setting a custom device name (e.g., "kitchen", "bar") via MQTT.
- **Robust MQTT Handling**: Supports case-insensitive commands and persistent subscriptions to both its hardware ID and custom name.
- **Persistence**: Remembers your theme, brightness, device name, and IR settings even after a power loss.
- **Always-on IR**: IR functionality is always included in the firmware, with a runtime setting to enable or disable processing.

## Hardware Platform

- **Microcontroller**: ESP32-C3
- **LEDs**: WS2812B (addressable) on DATA_PIN 4.
- **IR Receiver**: Connected to GPIO 3.

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
- `STATUS`: Request the current state of the device.

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
