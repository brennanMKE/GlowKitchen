#!/bin/zsh

# Configuration
SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/lib/broker.sh"
parse_broker_args "$@"
set -- "${ARGS[@]}"
TIMEOUT=2

# Check arguments
if [[ $# -lt 1 ]]; then
    echo "Usage: $0 [--host <addr>] [device]"
    echo "$BROKER_USAGE"
    echo "Example: $0 kitchen"
    echo "Example: $0 --host 192.168.88.254 kitchen"
    exit 1
fi

DEVICE=$1

echo "Requesting status from $DEVICE..."

# Request device to report status
/opt/homebrew/bin/mosquitto_pub -h "$BROKER" -p 1883 -u mqtt -P "$MQTT_PASSWORD" \
  -t "lights/$DEVICE/cmd" -m "STATUS"

echo "Listening for response (${TIMEOUT}s timeout)..."
echo ""

# Subscribe to specific device state topic
/opt/homebrew/bin/mosquitto_sub -h "$BROKER" -p 1883 -u mqtt -P "$MQTT_PASSWORD" \
  -t "lights/$DEVICE/state" -W $TIMEOUT 2>/dev/null | while read line; do
    # Pretty print JSON
    echo "$line" | /opt/homebrew/bin/jq '.' 2>/dev/null || echo "$line"
done

echo ""
echo "Done."
