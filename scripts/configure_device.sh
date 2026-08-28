#!/bin/zsh

# Configuration
SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/lib/broker.sh"
parse_broker_args "$@"
set -- "${ARGS[@]}"

# Check arguments
if [[ $# -lt 2 ]]; then
    echo "Usage: $0 [--host <addr>] [device] [option] [value]"
    echo ""
    echo "$BROKER_USAGE"
    echo ""
    echo "Options:"
    echo "  num_leds <count>        Set number of LEDs"
    echo "  leds_per_color <count>  Set LEDs per color"
    echo "  brightness <0-255>      Set brightness"
    echo "  name <name>             Rename device"
    echo "  ir <true|false>         Enable/disable IR receiver"
    echo "  mirror <true|false>     Enable/disable mirror mode"
    echo "  ota_auto <true|false>   Enable/disable automatic GitHub OTA checks"
    echo ""
    echo "Examples:"
    echo "  $0 kitchen num_leds 240"
    echo "  $0 desk brightness 200"
    echo "  $0 all ir false"
    echo "  $0 --host 192.168.88.254 all ir false"
    exit 1
fi

DEVICE=$1
OPTION=$2
VALUE=$3

TOPIC="lights/$DEVICE/cmd"

case "$OPTION" in
    num_leds)
        PAYLOAD="SET_NUM_LEDS:$VALUE"
        ;;
    leds_per_color)
        PAYLOAD="SET_LEDS_PER_COLOR:$VALUE"
        ;;
    brightness)
        PAYLOAD="SET_BRIGHTNESS:$VALUE"
        ;;
    name)
        PAYLOAD="SET_DEVICE_NAME:$VALUE"
        ;;
    ir)
        PAYLOAD="SET_IR_FLAG:$VALUE"
        ;;
    mirror)
        PAYLOAD="SET_MIRROR:$VALUE"
        ;;
    ota_auto)
        PAYLOAD="OTA_AUTO:$VALUE"
        ;;
    *)
        echo "Error: Unknown option '$OPTION'"
        exit 1
        ;;
esac

echo "Sending $PAYLOAD to $TOPIC..."
/opt/homebrew/bin/mosquitto_pub -h "$BROKER" -p 1883 -u mqtt -P "$MQTT_PASSWORD" \
  -t "$TOPIC" -m "$PAYLOAD"

echo "Configuration sent."
