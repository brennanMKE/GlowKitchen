#!/bin/zsh

# Configuration
SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/lib/broker.sh"
parse_broker_args "$@"
set -- "${ARGS[@]}"

# Check arguments
if [[ $# -lt 2 ]]; then
    echo "Usage: $0 [--host <addr>] [device] [UP|DOWN]"
    echo "$BROKER_USAGE"
    echo "Device: device name or 'all' for broadcast"
    echo "Example: $0 all UP"
    echo "Example: $0 --host 192.168.88.254 all UP"
    exit 1
fi

DEVICE=$1
ACTION=$2

# Standardize action to uppercase
ACTION=${(U)ACTION}

case "$ACTION" in
    UP)
        PAYLOAD="BRIGHT_UP"
        ;;
    DOWN)
        PAYLOAD="BRIGHT_DOWN"
        ;;
    *)
        echo "Error: Action must be UP or DOWN"
        exit 1
        ;;
esac

TOPIC="lights/$DEVICE/cmd"

echo "Sending $PAYLOAD to $TOPIC..."
/opt/homebrew/bin/mosquitto_pub -h "$BROKER" -p 1883 -u mqtt -P "$MQTT_PASSWORD" \
  -t "$TOPIC" -m "$PAYLOAD"

echo "Brightness adjustment sent."
