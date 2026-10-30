#!/bin/zsh

# Trigger the late-night brightness swell on demand, for checking it without
# waiting for the top of a late-night hour.
#
# Usage: ./scripts/swell.sh [--host <addr>] [device]
#   device: device name or 'all' for broadcast (default: all)
#   --host: MQTT broker hostname or IP (default: $MQTT_BROKER or homeassistant.local)
#
# Examples:
#   ./scripts/swell.sh
#   ./scripts/swell.sh kitchen
#   ./scripts/swell.sh --host 192.168.88.254

# Configuration
SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/lib/broker.sh"
parse_broker_args "$@"
set -- "${ARGS[@]}"
require_mqtt_password

DEVICE=${1:-all}
TOPIC="lights/$DEVICE/cmd"

# No retain flag (-r): a retained SWELL would replay, and swell again, every
# time a device reconnects to the broker.
echo "Sending SWELL to $TOPIC..."
/opt/homebrew/bin/mosquitto_pub -h "$BROKER" -p "$MQTT_PORT" -u "$MQTT_USER" -P "$MQTT_PASSWORD" \
  -t "$TOPIC" -m "SWELL" || exit $?

echo "Swell sent."
