#!/bin/zsh

# Configuration
BROKER="homeassistant.local"
TOPIC="lights/all/cmd"
STATE_FILE="$HOME/.glow_kitchen_theme"

# Define available themes in order
THEMES=(GREEN RAINBOW PINK_PONY OCEAN_WAVES SUNSET FOREST)
THEME_COUNT=$#THEMES

# Read current theme from state file, default to first theme
if [[ -f "$STATE_FILE" ]]; then
    CURRENT_THEME=$(cat "$STATE_FILE")
else
    CURRENT_THEME="${THEMES[1]}"
fi

# Find index of current theme (zsh arrays are 1-indexed)
CURRENT_INDEX=-1
for i in {1..$THEME_COUNT}; do
    if [[ "${THEMES[$i]}" == "${CURRENT_THEME}" ]]; then
        CURRENT_INDEX=$i
        break
    fi
done

# Calculate next theme index
if [[ $CURRENT_INDEX -eq -1 ]]; then
    CURRENT_INDEX=1
fi
NEXT_INDEX=$(( (CURRENT_INDEX % THEME_COUNT) + 1 ))
NEXT_THEME="${THEMES[$NEXT_INDEX]}"

# Save next theme to state file
echo "$NEXT_THEME" > "$STATE_FILE"

# Publish to MQTT with retain flag (-r) to keep devices in sync
echo "Switching to $NEXT_THEME..."
/opt/homebrew/bin/mosquitto_pub -h "$BROKER" -p 1883 -u mqtt -P "$MQTT_PASSWORD" \
  -t "$TOPIC" -m "$NEXT_THEME" -r
