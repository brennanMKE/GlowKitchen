#!/bin/zsh

# Tell device(s) to check GitHub for an OTA firmware update right now.
# The firmware handles OTA_UPDATE on both lights/<device>/cmd and lights/all/cmd:
# it fetches the latest release tag and, if it differs from the running version,
# downloads and flashes it, then reboots.
#
# Usage: ./scripts/ota_update.sh [--host <addr>] [device]
#   --host: MQTT broker hostname or IP (default: $MQTT_BROKER or homeassistant.local)
#   device: kitchen, tv, desk, a hardware id, or all   (default: all)
#
# Examples:
#   ./scripts/ota_update.sh                             # all devices
#   ./scripts/ota_update.sh kitchen                     # just the kitchen
#   ./scripts/ota_update.sh --host 192.168.88.254 all   # broker by IP

# Configuration
SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/lib/broker.sh"
parse_broker_args "$@"
set -- "${ARGS[@]}"

DEVICE="${1:-all}"
TOPIC="lights/$DEVICE/cmd"

echo "Sending OTA_UPDATE to $TOPIC..."

# NOTE: deliberately NOT retained (no -r). OTA_UPDATE is a one-shot trigger;
# a retained trigger would make every device re-run the check each time it
# reconnects to the broker.
/opt/homebrew/bin/mosquitto_pub -h "$BROKER" -p 1883 -u mqtt -P "$MQTT_PASSWORD" \
  -t "$TOPIC" -m "OTA_UPDATE"

echo "OTA check requested."
echo "Confirm afterward with: ./scripts/list_devices.sh  (look at the Firmware line)"
