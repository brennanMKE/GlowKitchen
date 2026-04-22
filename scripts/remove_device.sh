#!/bin/zsh

# Configuration
BROKER="homeassistant.local"

# Check if device ID was provided
if [[ -z "$1" ]]; then
    echo "Usage: $0 <device-id>"
    echo "Example: $0 84fce68773d0"
    exit 1
fi

DEVICE_ID="$1"
TOPIC="lights/$DEVICE_ID/state"

echo "Removing retained message for device: $DEVICE_ID"
echo "Topic: $TOPIC"

# Publish empty message with retain flag to delete the retained message
/opt/homebrew/bin/mosquitto_pub -h "$BROKER" -p 1883 -u mqtt -P "$MQTT_PASSWORD" \
  -t "$TOPIC" -n -r

if [[ $? -eq 0 ]]; then
    echo "✓ Device message removed successfully"
else
    echo "✗ Failed to remove device message"
    exit 1
fi
