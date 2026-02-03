#!/bin/bash

# Configuration
BROKER="192.168.88.254"
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
