#!/bin/zsh

# Configuration
BROKER="homeassistant.local"
TIMEOUT=3  # Wait 3 seconds for responses

echo "Requesting status from all devices..."

# Request all devices to report status
/opt/homebrew/bin/mosquitto_pub -h "$BROKER" -p 1883 -u mqtt -P "$MQTT_PASSWORD" \
  -t "lights/all/cmd" -m "STATUS"

echo "Listening for responses (${TIMEOUT}s timeout)..."
echo ""

# Collect responses to temp file
TMPFILE=$(mktemp)
trap "rm -f $TMPFILE" EXIT

# Subscribe with verbose output to get topic names
/opt/homebrew/bin/mosquitto_sub -h "$BROKER" -p 1883 -u mqtt -P "$MQTT_PASSWORD" \
  -t "lights/+/state" -W $TIMEOUT -v 2>/dev/null > "$TMPFILE"

# Process responses, deduplicating by device
declare -A seen
while IFS= read -r line; do
    # Split on first space: topic is everything before first space, payload is rest
    topic="${line%% *}"
    payload="${line#* }"

    # Skip empty lines
    [[ -z "$topic" ]] && continue

    # Extract device name from topic (lights/<device>/state)
    device=$(echo "$topic" | cut -d'/' -f2)

    # Skip if we've already seen this device
    if [[ -n "${seen[$device]}" ]]; then
        continue
    fi
    seen[$device]=1

    # Parse JSON response using jq
    theme=$(printf '%s\n' "$payload" | jq -r '.theme // "unknown"' 2>/dev/null)
    brightness=$(printf '%s\n' "$payload" | jq -r '.brightness // "?"' 2>/dev/null)
    numLeds=$(printf '%s\n' "$payload" | jq -r '.numLeds // "?"' 2>/dev/null)
    ledsEnabled=$(printf '%s\n' "$payload" | jq -r '.ledsEnabled // "?"' 2>/dev/null)
    irEnabled=$(printf '%s\n' "$payload" | jq -r '.irEnabled // "unknown"' 2>/dev/null)

    echo "Device: $device"
    echo "  Theme: $theme"
    echo "  LEDs: $numLeds (enabled: $ledsEnabled)"
    echo "  Brightness: $brightness"
    echo "  IR: $irEnabled"
    echo ""
done < "$TMPFILE"

echo "Done."
