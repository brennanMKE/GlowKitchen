#!/bin/zsh

# Configuration
SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/lib/broker.sh"
parse_broker_args "$@"
set -- "${ARGS[@]}"
STATE_FILE="$HOME/.glow_kitchen_theme"

# Check arguments
if [[ $# -lt 2 ]]; then
    echo "Usage: $0 [--host <addr>] [device] [THEME]"
    echo "$BROKER_USAGE"
    echo "Device: device name or 'all' for broadcast"
    echo "Themes: GREEN, RAINBOW, PINK_PONY, OCEAN_WAVES, SUNSET, FOREST"
    echo "Example: $0 all PINK_PONY"
    echo "Example: $0 --host 192.168.88.254 all PINK_PONY"
    exit 1
fi

DEVICE=$1
THEME=$2

# Standardize theme to uppercase
THEME=${(U)THEME}

# Validation
VALID_THEMES=(GREEN RAINBOW PINK_PONY OCEAN_WAVES SUNSET FOREST)
if [[ ! " ${VALID_THEMES[@]} " =~ " ${THEME} " ]]; then
    echo "Error: Invalid theme name."
    echo "Valid options are: ${VALID_THEMES[*]}"
    exit 1
fi

TOPIC="lights/$DEVICE/cmd"

# Save to state file if affecting 'all'
if [[ "$DEVICE" == "all" ]]; then
    echo "$THEME" > "$STATE_FILE"
fi

echo "Setting theme to $THEME for $DEVICE..."
/opt/homebrew/bin/mosquitto_pub -h "$BROKER" -p 1883 -u mqtt -P "$MQTT_PASSWORD" \
  -t "$TOPIC" -m "$THEME" -r

echo "Theme update sent."
