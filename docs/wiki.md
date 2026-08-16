= Glow Lights: Control with Scripts & MQTT =

Welcome! This guide shows you how to control the Glow Lights installations using shell scripts and MQTT messages.

== Overview ==

The Glow Lights project manages LED installations at three locations:
* '''Kitchen''' - Features an IR receiver that responds to remote control
* '''TV''' - Standalone LED display
* '''Desk''' - Standalone LED display

Each installation runs an ESP32-C3 microcontroller connected to a WiFi network. You can control them by publishing MQTT messages from your computer.

=== Installation Images ===

<gallery widths="300px" heights="300px">
  File:Glow1.jpg|Kitchen & TV Installations
  File:Glow2.jpg|Desk Installation
</gallery>

== Setup (One-Time) ==

=== Prerequisites ===

* macOS or Linux
* MQTT broker credentials (see [[Network]] for current information):
** '''Broker:''' <code>homeassistant.local</code>
** '''Port:''' <code>1883</code>
** '''Username:''' <code>mqtt</code>
** '''Password:''' Same WiFi password

=== 1. Install MQTT Publishing Tool ===

==== macOS (via Homebrew) ====
<syntaxhighlight lang="bash">
brew install mosquitto
</syntaxhighlight>

==== Linux (Debian/Ubuntu) ====
<syntaxhighlight lang="bash">
sudo apt-get install mosquitto-clients
</syntaxhighlight>

=== 2. Set Environment Variable ===

Add your MQTT password to your shell profile. This is required by all the control scripts.

<syntaxhighlight lang="bash">
# Add this to ~/.zshrc (macOS Zsh) or ~/.bashrc (Linux Bash)
export MQTT_PASSWORD="your_wifi_password"

# Reload your shell
source ~/.zshrc
# or
source ~/.bashrc
</syntaxhighlight>

'''Why?''' The scripts use the <code>$MQTT_PASSWORD</code> environment variable so you don't have to type your password each time.

== Using the Scripts ==

All scripts are located in the <code>scripts/</code> directory of this project.

=== Change Themes ===

'''Cycle to next theme''' (tracks which theme you're on):
<syntaxhighlight lang="bash">
./scripts/publish_next_theme.sh
</syntaxhighlight>

Available themes cycle through: GREEN → RAINBOW → PINK_PONY → OCEAN_WAVES → SUNSET → FOREST → (back to GREEN)

'''Set a specific theme''' for one or all locations:
<syntaxhighlight lang="bash">
# Set theme for all installations
./scripts/set_theme.sh all GREEN
./scripts/set_theme.sh all RAINBOW
./scripts/set_theme.sh all PINK_PONY

# Set theme for a specific location
./scripts/set_theme.sh kitchen SUNSET
./scripts/set_theme.sh tv FOREST
./scripts/set_theme.sh desk OCEAN_WAVES
</syntaxhighlight>

Valid theme names: <code>GREEN</code>, <code>RAINBOW</code>, <code>PINK_PONY</code>, <code>OCEAN_WAVES</code>, <code>SUNSET</code>, <code>FOREST</code>

=== Adjust Brightness ===

<syntaxhighlight lang="bash">
# Increase brightness for all lights
./scripts/adjust_brightness.sh all UP

# Decrease brightness for specific location
./scripts/adjust_brightness.sh kitchen DOWN
./scripts/adjust_brightness.sh tv UP
</syntaxhighlight>

Each UP/DOWN increments brightness by ~10 steps. Valid locations: <code>kitchen</code>, <code>tv</code>, <code>desk</code>, <code>all</code>

=== Configure Hardware ===

If you change the number of LEDs or need to adjust animation parameters:

<syntaxhighlight lang="bash">
# Set number of LEDs
./scripts/configure_device.sh tv 120

# Set LEDs and color group size
./scripts/configure_device.sh kitchen 150 15

# Apply to all installations
./scripts/configure_device.sh all 100 10
</syntaxhighlight>

=== Example: Automated Light Show ===

Run a sequence of theme changes:

<syntaxhighlight lang="bash">
# Cycle through 3 themes with 3-second pauses
./scripts/run_sequence.sh
</syntaxhighlight>

Or create your own script:

<syntaxhighlight lang="bash">
#!/bin/bash
./scripts/set_theme.sh all GREEN
sleep 2
./scripts/set_theme.sh all RAINBOW
sleep 2
./scripts/adjust_brightness.sh all UP
sleep 2
./scripts/adjust_brightness.sh all DOWN
</syntaxhighlight>

== Direct MQTT Publishing ==

You can also send commands directly using <code>mosquitto_pub</code>:

<syntaxhighlight lang="bash">
# Turn lights on
mosquitto_pub -h homeassistant.local -u mqtt -P "$MQTT_PASSWORD" \
  -t lights/all/cmd -m "ON"

# Turn lights off
mosquitto_pub -h homeassistant.local -u mqtt -P "$MQTT_PASSWORD" \
  -t lights/all/cmd -m "OFF"

# Toggle brightness
mosquitto_pub -h homeassistant.local -u mqtt -P "$MQTT_PASSWORD" \
  -t lights/kitchen/cmd -m "BRIGHT_UP"
</syntaxhighlight>

'''Topics:'''
* <code>lights/all/cmd</code> - All installations
* <code>lights/kitchen/cmd</code> - Kitchen only
* <code>lights/tv/cmd</code> - TV only
* <code>lights/desk/cmd</code> - Desk only

== Complete Command Reference ==

=== Power Control ===

{| class="wikitable"
|-
! Command !! Effect
|-
| <code>ON</code> || Turn LEDs on with current theme
|-
| <code>OFF</code> || Turn LEDs off (all black)
|-
| <code>TOGGLE</code> || Toggle between ON and OFF
|}

=== Theme Switching ===

{| class="wikitable"
|-
! Command !! Effect
|-
| <code>GREEN</code> || Candle flicker effect
|-
| <code>RAINBOW</code> || Rainbow gradient blend
|-
| <code>PINK_PONY</code> || Pink/magenta colors
|-
| <code>OCEAN_WAVES</code> || Blue/cyan gradient
|-
| <code>SUNSET</code> || Orange/red gradient
|-
| <code>FOREST</code> || Green gradient
|-
| <code>NEXT_THEME</code> || Cycle to next theme
|-
| <code>PREV_THEME</code> || Cycle to previous theme
|}

=== Brightness ===

{| class="wikitable"
|-
! Command !! Effect
|-
| <code>BRIGHT_UP</code> || Increase brightness by ~10
|-
| <code>BRIGHT_DOWN</code> || Decrease brightness by ~10
|-
| <code>SET_BRIGHTNESS:N</code> || Set absolute brightness (0-255)
|}

=== Animations ===

{| class="wikitable"
|-
! Command !! Effect
|-
| <code>COLOR_CHANGE_ON</code> || Enable automatic color cycling
|-
| <code>COLOR_CHANGE_OFF</code> || Freeze current colors
|}

== Writing Your Own Script ==

Here's a template for creating new scripts:

<syntaxhighlight lang="bash">
#!/bin/bash

# Configuration
BROKER="homeassistant.local"
LOCATION="all"  # or: kitchen, tv, desk
TOPIC="lights/$LOCATION/cmd"

# Publish command
mosquitto_pub -h "$BROKER" -u mqtt -P "$MQTT_PASSWORD" \
  -t "$TOPIC" -m "GREEN"

echo "Command sent to $TOPIC"
</syntaxhighlight>

'''Tips:'''
* Use <code>-r</code> flag to "retain" the message (devices will apply it even if they reconnect later)
* Use <code>-p 1883</code> if you need to specify the port explicitly
* Check the output for errors

== Troubleshooting ==

'''Command not working?'''
* Verify <code>$MQTT_PASSWORD</code> is set: <code>echo $MQTT_PASSWORD</code>
* Check that the broker is reachable: <code>ping homeassistant.local</code>
* Verify the WiFi name matches (LEDs won't connect without WiFi)

'''Want to monitor messages?'''

See all messages being sent (in a separate terminal):
<syntaxhighlight lang="bash">
mosquitto_sub -h homeassistant.local -u mqtt -P "$MQTT_PASSWORD" \
  -t "lights/#"
</syntaxhighlight>

== Learning More ==

* '''MQTT basics:''' [https://mqtt.org/ MQTT.org] - What is MQTT and how it works
* '''mosquitto documentation:''' [https://mosquitto.org/man/mosquitto_pub-1.html Man page for mosquitto_pub]
* '''Adding new themes:''' See the project's <code>docs/add_new_theme.md</code>
* '''Full MQTT command list:''' See the project's <code>docs/mqtt_commands.md</code>

== Questions or Ideas? ==

* Suggest improvements or new features to the Makerspace community!

== Appendix: Complete Script Contents ==

=== publish_next_theme.sh ===

<syntaxhighlight lang="bash">
#!/bin/bash

# Configuration
BROKER="homeassistant.local"
TOPIC="lights/all/cmd"
STATE_FILE="$HOME/.glow_kitchen_theme"

# Define available themes in order
THEMES=("GREEN" "RAINBOW" "PINK_PONY" "OCEAN_WAVES" "SUNSET" "FOREST")
THEME_COUNT=${#THEMES[@]}

# Read current theme from state file, default to first theme
if [ -f "$STATE_FILE" ]; then
    CURRENT_THEME=$(cat "$STATE_FILE")
else
    CURRENT_THEME="${THEMES[0]}"
fi

# Find index of current theme
CURRENT_INDEX=-1
for i in "${!THEMES[@]}"; do
   if [[ "${THEMES[$i]}" == "${CURRENT_THEME}" ]]; then
       CURRENT_INDEX=$i
       break
   fi
done

# Calculate next theme index
NEXT_INDEX=$(( (CURRENT_INDEX + 1) % THEME_COUNT ))
NEXT_THEME="${THEMES[$NEXT_INDEX]}"

# Save next theme to state file
echo "$NEXT_THEME" > "$STATE_FILE"

# Publish to MQTT with retain flag (-r) to keep devices in sync
echo "Switching to $NEXT_THEME..."
/opt/homebrew/bin/mosquitto_pub -h "$BROKER" -p 1883 -u mqtt -P "$MQTT_PASSWORD" \
  -t "$TOPIC" -m "$NEXT_THEME" -r
</syntaxhighlight>

=== set_theme.sh ===

<syntaxhighlight lang="bash">
#!/bin/bash

# Configuration
BROKER="homeassistant.local"
STATE_FILE="$HOME/.glow_kitchen_theme"

# Check arguments
if [ "$#" -lt 2 ]; then
    echo "Usage: $0 [location] [THEME]"
    echo "Locations: kitchen, tv, desk, all"
    echo "Themes: GREEN, RAINBOW, PINK_PONY, OCEAN_WAVES, SUNSET, FOREST"
    echo "Example: $0 all PINK_PONY"
    exit 1
fi

LOCATION=$1
THEME=$2

# Standardize theme to uppercase
THEME=$(echo "$THEME" | tr '[:lower:]' '[:upper:]')

# Validation
VALID_THEMES=("GREEN" "RAINBOW" "PINK_PONY" "OCEAN_WAVES" "SUNSET" "FOREST")
IS_VALID=false
for t in "${VALID_THEMES[@]}"; do
    if [ "$THEME" == "$t" ]; then
        IS_VALID=true
        break
    fi
done

if [ "$IS_VALID" = false ]; then
    echo "Error: Invalid theme name."
    echo "Valid options are: ${VALID_THEMES[*]}"
    exit 1
fi

TOPIC="lights/$LOCATION/cmd"

# Save to state file if affecting 'all'
if [ "$LOCATION" == "all" ]; then
    echo "$THEME" > "$STATE_FILE"
fi

echo "Setting theme to $THEME for $LOCATION..."
/opt/homebrew/bin/mosquitto_pub -h "$BROKER" -p 1883 -u mqtt -P "$MQTT_PASSWORD" \
  -t "$TOPIC" -m "$THEME" -r

echo "Theme update sent."
</syntaxhighlight>

=== adjust_brightness.sh ===

<syntaxhighlight lang="bash">
#!/bin/bash

# Configuration
BROKER="homeassistant.local"

# Check arguments
if [ "$#" -lt 2 ]; then
    echo "Usage: $0 [location] [UP|DOWN]"
    echo "Locations: kitchen, tv, desk, all"
    echo "Example: $0 all UP"
    exit 1
fi

LOCATION=$1
ACTION=$2

# Standardize action to uppercase
ACTION=$(echo "$ACTION" | tr '[:lower:]' '[:upper:]')

if [ "$ACTION" == "UP" ]; then
    PAYLOAD="BRIGHT_UP"
elif [ "$ACTION" == "DOWN" ]; then
    PAYLOAD="BRIGHT_DOWN"
else
    echo "Error: Action must be UP or DOWN"
    exit 1
fi

TOPIC="lights/$LOCATION/cmd"

echo "Sending $PAYLOAD to $TOPIC..."
/opt/homebrew/bin/mosquitto_pub -h "$BROKER" -p 1883 -u mqtt -P "$MQTT_PASSWORD" \
  -t "$TOPIC" -m "$PAYLOAD"

echo "Brightness adjustment sent."
</syntaxhighlight>

=== configure_device.sh ===

<syntaxhighlight lang="bash">
#!/bin/bash

# Configuration
BROKER="homeassistant.local"

# Check arguments
if [ "$#" -lt 2 ]; then
    echo "Usage: $0 [location] [numLeds] [optional: ledsPerColor]"
    echo "Locations: kitchen, tv, desk, all"
    echo "Example: $0 tv 120 15"
    exit 1
fi

LOCATION=$1
NUM_LEDS=$2
LEDS_PER_COLOR=$3

TOPIC="lights/$LOCATION/cmd"

# Set Number of LEDs
echo "Sending SET_NUM_LEDS:$NUM_LEDS to $TOPIC..."
/opt/homebrew/bin/mosquitto_pub -h "$BROKER" -p 1883 -u mqtt -P "$MQTT_PASSWORD" \
  -t "$TOPIC" -m "SET_NUM_LEDS:$NUM_LEDS"

# Set LEDs Per Color if provided
if [ -n "$LEDS_PER_COLOR" ]; then
    echo "Sending SET_LEDS_PER_COLOR:$LEDS_PER_COLOR to $TOPIC..."
    /opt/homebrew/bin/mosquitto_pub -h "$BROKER" -p 1883 -u mqtt -P "$MQTT_PASSWORD" \
      -t "$TOPIC" -m "SET_LEDS_PER_COLOR:$LEDS_PER_COLOR"
fi

echo "Configuration sent."
</syntaxhighlight>

=== run_sequence.sh ===

<syntaxhighlight lang="bash">
#!/bin/bash

sleep 3
./scripts/set_theme.sh all GREEN
sleep 3
./scripts/set_theme.sh all RAINBOW
sleep 3
./scripts/set_theme.sh all PINK_PONY
sleep 3
./scripts/set_theme.sh all GREEN
sleep 3
./scripts/set_theme.sh all RAINBOW
sleep 3
./scripts/set_theme.sh all PINK_PONY
</syntaxhighlight>

== Appendix: Firmware MQTT Handler ==

The ESP32-C3 firmware processes incoming MQTT messages in the <code>onMqttMessage()</code> function. Here's a summary of how commands are processed:

=== Supported MQTT Commands ===

* <code>NEXT_THEME</code> - Cycle to next theme
* <code>PREV_THEME</code> - Cycle to previous theme
* <code>STATUS</code> - Publish current device status
* <code>SET_NUM_LEDS:N</code> - Set total number of LEDs
* <code>SET_LEDS_PER_COLOR:N</code> - Set LEDs per color group
* <code>SET_BRIGHTNESS:N</code> - Set absolute brightness (0-255)
* <code>SET_DEVICE_NAME:name</code> - Change device name and subscribe to new topic
* <code>SET_IR_FLAG:true|false</code> - Enable/disable IR receiver
* Theme commands: <code>GREEN</code>, <code>RAINBOW</code>, <code>PINK_PONY</code>, <code>OCEAN_WAVES</code>, <code>SUNSET</code>, <code>FOREST</code>
* Power commands: <code>ON</code>, <code>OFF</code>, <code>TOGGLE</code>
* Brightness: <code>BRIGHT_UP</code>, <code>BRIGHT_DOWN</code>
* Animation: <code>COLOR_CHANGE_ON</code>, <code>COLOR_CHANGE_OFF</code>

=== Theme Definitions ===

<syntaxhighlight lang="cpp">
// Theme System
enum HueTheme {
    THEME_GREEN = 0,
    THEME_RAINBOW = 1,
    THEME_PINK_PONY = 2,
    THEME_OCEAN_WAVES = 3,
    THEME_SUNSET = 4,
    THEME_FOREST = 5,
    THEME_COUNT = 6
};

const char* THEME_NAMES[] = {
    "Green",
    "Rainbow",
    "Pink Pony Club",
    "Ocean Waves",
    "Sunset",
    "Forest"
};

// Green Theme - Candle-like effect
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

// Rainbow Theme - Full spectrum
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

// Pink Pony Club Theme
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

// Ocean Waves Theme
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

// Sunset Theme
const uint8_t SUNSET_HUES[] = {
  0,   // red
  10,  // red-orange
  20,  // orange
  30,  // yellow-orange
  40,  // yellow
  50,  // yellow-green
  60,  // green
  70   // blue-green
};

// Forest Theme
const uint8_t FOREST_HUES[] = {
  90,  // yellow-green
  100, // lime
  110, // light green
  120, // green
  130, // forest green
  140, // dark green-blue
  150, // teal
  160  // cyan
};
</syntaxhighlight>

=== MQTT Message Handler (Simplified) ===

The firmware handles MQTT messages as follows:

<syntaxhighlight lang="cpp">
void onMqttMessage(char* topic, byte* payload, unsigned int len) {
    // Convert payload to string
    String msg;
    for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
    String msgUpper = msg;
    msgUpper.toUpperCase(); // Case-insensitive matching

    // Handle theme commands
    if (msgUpper == "GREEN") {
        currentTheme = THEME_GREEN;
        // ... update display and save state
    } else if (msgUpper == "RAINBOW") {
        currentTheme = THEME_RAINBOW;
        // ... update display and save state
    }
    // ... similar for other themes

    // Handle power commands
    if (msgUpper == "ON") {
        ledsEnabled = true;
        FastLED.show();
    } else if (msgUpper == "OFF") {
        fill_solid(leds, numLeds, CRGB::Black);
        FastLED.show();
    } else if (msgUpper == "TOGGLE") {
        ledsEnabled = !ledsEnabled;
    }

    // Handle brightness
    if (msgUpper == "BRIGHT_UP") {
        increaseBrightness();
    } else if (msgUpper == "BRIGHT_DOWN") {
        decreaseBrightness();
    } else if (msgUpper.startsWith("SET_BRIGHTNESS:")) {
        int val = msgUpper.substring(15).toInt();
        if (val >= 0 && val <= 255) {
            FastLED.setBrightness(val);
        }
    }

    // Handle hardware configuration
    if (msgUpper.startsWith("SET_NUM_LEDS:")) {
        numLeds = msgUpper.substring(13).toInt();
        // ... validate and save
    }
}</syntaxhighlight>
