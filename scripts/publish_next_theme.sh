#!/bin/bash

# Configuration
BROKER="192.168.88.254"
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
