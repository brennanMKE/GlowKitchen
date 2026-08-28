#!/bin/zsh

# Configuration
SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/lib/broker.sh"
parse_broker_args "$@"
set -- "${ARGS[@]}"
require_mqtt_password

# Check if device ID was provided
if [[ -z "$1" ]]; then
    echo "Usage: $0 [--host <addr>] <device-id>"
    echo "$BROKER_USAGE"
    echo "Example: $0 84fce68773d0"
    echo "Example: $0 --host 192.168.88.254 84fce68773d0"
    exit 1
fi

DEVICE_ID="$1"
TOPIC="lights/$DEVICE_ID/state"

echo "Removing retained message for device: $DEVICE_ID"
echo "Topic: $TOPIC"

# Publish empty message with retain flag to delete the retained message
/opt/homebrew/bin/mosquitto_pub -h "$BROKER" -p "$MQTT_PORT" -u "$MQTT_USER" -P "$MQTT_PASSWORD" \
  -t "$TOPIC" -n -r

if [[ $? -eq 0 ]]; then
    echo "✓ Device message removed successfully"
else
    echo "✗ Failed to remove device message"
    exit 1
fi
