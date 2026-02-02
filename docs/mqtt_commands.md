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
| `NEXT_THEME` | Cycles to the next available theme. |
| `PREV_THEME` | Cycles to the previous theme. |
| `GREEN` | Switches immediately to the Green theme (candle flicker). |
| `RAINBOW` | Switches immediately to the Rainbow theme (gradient blend). |
| `PINK_PONY` | Switches immediately to the Pink Pony Club theme (pink/magenta). |
| `HALLOWEEN` | Switches immediately to the Halloween theme (orange/purple). |
| `CHRISTMAS` | Switches immediately to the Christmas theme (red/green). |

*Note: The system also supports `SCENE_1` (Green) and `SCENE_2` (Rainbow) for backward compatibility.*

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

### Hardware Configuration

These commands update the hardware settings in the device's persistent memory.

| Payload Prefix | Example | Description |
| :--- | :--- | :--- |
| `SET_NUM_LEDS:` | `SET_NUM_LEDS:150` | Sets the total number of LEDs connected (Max: 500). |
| `SET_LEDS_PER_COLOR:` | `SET_LEDS_PER_COLOR:15` | Sets the number of LEDs per color group in animations. |

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
*   **THEME**: `GREEN`, `RAINBOW`, `PINK_PONY`, `HALLOWEEN`, or `CHRISTMAS`.
*   Uses the **Retain** flag to keep devices in sync.
