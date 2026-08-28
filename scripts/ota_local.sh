#!/bin/zsh

# Push a locally-built firmware to ONE device over the LAN, without cutting a
# GitHub release. Serves .pio/build/esp32c3/firmware.bin over plain HTTP and
# tells the device to flash it via the MQTT OTA_URL command.
#
# The firmware only accepts OTA_URL on a device-specific topic (never
# lights/all/cmd) and only when the host is on the local network, so this
# cannot be pointed at the internet or fan out to the whole fleet.
#
# Usage: ./scripts/ota_local.sh [--host <addr>] <device> [port]
#   --host: MQTT broker hostname or IP (default: $MQTT_BROKER or homeassistant.local)
#   device: kitchen, tv, desk, workbench, recycling, Dev, or a hardware id
#   port:   HTTP port to serve on (default 8000)
#
# Examples:
#   ./scripts/ota_local.sh kitchen
#   ./scripts/ota_local.sh --host 192.168.88.254 kitchen
#
# NOTE: a device you push to this way is still subject to the nightly GitHub
# check, which would replace your dev build with the released tag. Pin it first:
#   ./scripts/configure_device.sh kitchen ota_auto false
# and re-enable when you're done:
#   ./scripts/configure_device.sh kitchen ota_auto true

# Configuration
SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/lib/broker.sh"
parse_broker_args "$@"
set -- "${ARGS[@]}"
# The release env deliberately, not esp32c3-debug: a verbose build is ~45 KB
# larger than the 0x140000 OTA slot, so it cannot be pushed over the air at all.
# Debug builds have to go on over USB (pio run -e esp32c3-debug -t upload).
BIN=".pio/build/esp32c3/firmware.bin"

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 [--host <addr>] <device> [port]"
    echo "$BROKER_USAGE"
    echo "Example: $0 kitchen"
    echo "Example: $0 --host 192.168.88.254 kitchen"
    exit 1
fi

DEVICE=$1
PORT="${2:-8000}"

if [[ "$DEVICE" == "all" ]]; then
    echo "Error: refusing to push to 'all'. Local OTA targets one device at a time."
    exit 1
fi

if [[ ! -f "$BIN" ]]; then
    echo "Error: $BIN not found. Build first with:  pio run"
    exit 1
fi

# Pick the LAN address on the interface that actually reaches the broker, rather
# than guessing en0 — works on both Wi-Fi and Ethernet.
IP=$(route get "$BROKER" 2>/dev/null | awk '/interface: /{print $2}' | xargs -I{} ipconfig getifaddr {} 2>/dev/null)
if [[ -z "$IP" ]]; then
    IP=$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1 2>/dev/null)
fi
if [[ -z "$IP" ]]; then
    echo "Error: could not determine this machine's LAN IP address."
    exit 1
fi

URL="http://$IP:$PORT/firmware.bin"
SIZE=$(wc -c < "$BIN" | tr -d ' ')

echo "Serving $BIN ($SIZE bytes) at $URL"

# Serve just the build directory, in the background, and always clean up.
python3 -m http.server "$PORT" --bind "$IP" --directory "$(dirname "$BIN")" >/dev/null 2>&1 &
SERVER_PID=$!
trap 'kill $SERVER_PID 2>/dev/null' EXIT

sleep 1
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "Error: could not start HTTP server on port $PORT (already in use?)"
    exit 1
fi

echo "Sending OTA_URL to lights/$DEVICE/cmd..."
# Deliberately NOT retained: a retained OTA_URL would re-flash the device every
# time it reconnected to the broker.
/opt/homebrew/bin/mosquitto_pub -h "$BROKER" -p 1883 -u mqtt -P "$MQTT_PASSWORD" \
  -t "lights/$DEVICE/cmd" -m "OTA_URL:$URL"

echo "Waiting for $DEVICE to download and reboot (up to 90s)..."
echo "Watching lights/$DEVICE/state for the firmware version..."

/opt/homebrew/bin/mosquitto_sub -h "$BROKER" -p 1883 -u mqtt -P "$MQTT_PASSWORD" \
  -t "lights/$DEVICE/state" -W 90 2>/dev/null | while IFS= read -r line; do
    ver=$(printf '%s\n' "$line" | /opt/homebrew/bin/jq -r '.firmwareVersion // "?"' 2>/dev/null)
    echo "  $DEVICE reported firmware $ver"
done

echo "Done. Confirm with: ./scripts/get_device_status.sh $DEVICE"
