#!/bin/zsh

# Named demo presets for the custom-effects engine (issues #0013 / #0015 / #0016 / #0017).
#
# Each preset is a SET_EFFECT payload with a palette, a mode, and a timeout, so
# it reverts to the previous theme on its own and nothing is left running.
#
# Usage:
#   scripts/demo_effect.sh [--host <addr>] <device> <preset> [seconds] [options]
#   scripts/demo_effect.sh --list
#   scripts/demo_effect.sh kitchen dolly
#   scripts/demo_effect.sh kitchen cylon 120
#   scripts/demo_effect.sh kitchen cylon --speed 30      # override the preset
#   scripts/demo_effect.sh kitchen cylon --intensity 128
#   scripts/demo_effect.sh kitchen stop          # CLEAR_EFFECT, revert now
#
# speed and intensity are 0-255. speed 0 is the SLOWEST, 255 the fastest --
# it scales the per-step interval, not a rate. On a short strip the useful
# range sits near 0: the renderers' timing was tuned for ~240 LEDs, so a
# 10-LED sweep at speed 170 crosses the whole strip in a fraction of a second.
#
# Credentials come from the repo .env (MQTT_USERNAME / MQTT_PASSWORD).

SCRIPT_DIR="${0:A:h}"
REPO_ROOT="${SCRIPT_DIR:h}"

# Load .env BEFORE parse_broker_args: broker.sh resolves the default host from
# $MQTT_BROKER, so sourcing afterwards leaves it falling back to
# homeassistant.local -- which does not resolve on every network the devices
# live on. An explicit --host still wins over both.
if [[ -f "$REPO_ROOT/.env" ]]; then
    set -a; source "$REPO_ROOT/.env"; set +a
fi

source "$SCRIPT_DIR/lib/broker.sh"
parse_broker_args "$@"
set -- "${ARGS[@]}"

# Presets: name -> "MODE|colors|speed|intensity|default_seconds|description"
#
# IMPORTANT: BLEND and FLICKER hard-code saturation to 255 (they must render
# byte-identically to the pre-#0015 renderers), so they DISCARD the saturation
# of a custom palette. Any preset with pastel or desaturated colors must use a
# saturation-preserving mode: CHASE, WIPE, SCAN, SPARKLE, PULSE, STROBE or
# COLORLOOP. See issue #0016's ## Review for the measured round-trip.
typeset -A PRESETS
PRESETS=(
  dolly   'SPARKLE|"#FF69B4","#FF1493","#FFB6C1"|128|255|60|Dolly Parton — three shades of pink, sparkling'
  cylon   'SCAN|"#FF0000"|0|255|60|Cylon / KITT — red band sweeping back and forth'
  wipe    'WIPE|"#FF0000"|140|200|60|Red wipe — fills from one end and holds (contrast with cylon)'
  chase   'CHASE|"#FF6600","#00AAFF"|150|200|60|Chase — a lit run travelling along the strip'
  candle  'FLICKER|"#FF7000","#FF3000","#FFA000"|100|180|300|Candlelight — warm amber flicker (fully saturated, safe for FLICKER)'
  breathe 'PULSE|"#0033FF"|90|220|180|Breathing — deep blue fading up and down'
  strobe  'STROBE|"#FFFFFF"|200|128|15|Strobe — hard white flashing. Deliberately short.'
  loop    'COLORLOOP|"#FF0000","#00FF00","#0000FF"|255|255|60|Color loop — see #0018, crawls at default speed'
  blend   'BLEND|"#FF0000","#FFAA00"|120|128|60|Blend — proves the built-in renderer runs through the new engine'
)
PRESET_ORDER=(dolly cylon wipe chase candle breathe strobe loop blend)

list_presets() {
    echo "Presets:"
    for name in $PRESET_ORDER; do
        local spec="${PRESETS[$name]}"
        printf '  %-8s %-10s %4ss  %s\n' \
            "$name" "${spec%%|*}" "$(echo "$spec" | cut -d'|' -f5)" "${spec##*|}"
    done
    echo ""
    echo "  stop      CLEAR_EFFECT — revert to the previous theme immediately"
}

if [[ "$1" == "--list" || "$1" == "-l" ]]; then
    list_presets
    exit 0
fi

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 [--host <addr>] <device> <preset> [seconds]"
    echo "$BROKER_USAGE"
    echo ""
    list_presets
    exit 1
fi

DEVICE=$1
PRESET=$2
shift 2

OVERRIDE_SECONDS=""
OVERRIDE_SPEED=""
OVERRIDE_INTENSITY=""
while (( $# > 0 )); do
    case "$1" in
        --speed)      OVERRIDE_SPEED="$2";     shift 2 ;;
        --speed=*)    OVERRIDE_SPEED="${1#*=}"; shift ;;
        --intensity)  OVERRIDE_INTENSITY="$2"; shift 2 ;;
        --intensity=*) OVERRIDE_INTENSITY="${1#*=}"; shift ;;
        ''|*[!0-9]*)
            echo "Error: unrecognized argument '$1'." >&2
            exit 1 ;;
        *)  OVERRIDE_SECONDS="$1"; shift ;;   # bare number = duration
    esac
done

for pair in "speed:$OVERRIDE_SPEED" "intensity:$OVERRIDE_INTENSITY"; do
    name="${pair%%:*}"; val="${pair#*:}"
    if [[ -n "$val" ]]; then
        if [[ "$val" != <-> ]] || (( val < 0 || val > 255 )); then
            echo "Error: --$name must be 0-255 (got '$val')." >&2
            exit 1
        fi
    fi
done

TOPIC="lights/$DEVICE/cmd"

if [[ -z "$MQTT_PASSWORD" ]]; then
    echo "Error: MQTT_PASSWORD is not set. Populate $REPO_ROOT/.env (see .env.example)." >&2
    exit 1
fi

publish() {
    /opt/homebrew/bin/mosquitto_pub -h "$BROKER" -p "${MQTT_PORT:-1883}" \
        -u "${MQTT_USERNAME:-mqtt}" -P "$MQTT_PASSWORD" -t "$TOPIC" -m "$1"
}

if [[ "$PRESET" == "stop" ]]; then
    echo "Clearing effect on $DEVICE..."
    publish "CLEAR_EFFECT" && echo "Reverted to the previous theme."
    exit $?
fi

if [[ -z "${PRESETS[$PRESET]}" ]]; then
    echo "Error: unknown preset '$PRESET'." >&2
    echo ""
    list_presets
    exit 1
fi

spec="${PRESETS[$PRESET]}"
MODE=$(echo "$spec" | cut -d'|' -f1)
COLORS=$(echo "$spec" | cut -d'|' -f2)
SPEED=${OVERRIDE_SPEED:-$(echo "$spec" | cut -d'|' -f3)}
INTENSITY=${OVERRIDE_INTENSITY:-$(echo "$spec" | cut -d'|' -f4)}
SECONDS_VAL=${OVERRIDE_SECONDS:-$(echo "$spec" | cut -d'|' -f5)}
DESC=$(echo "$spec" | cut -d'|' -f6)

# The firmware clamps above 28800 (8h) rather than rejecting, but say so here
# too so an over-long request isn't silently shortened without explanation.
if (( SECONDS_VAL > 28800 )); then
    echo "Note: $SECONDS_VAL s exceeds the 8-hour maximum; the device will clamp it to 28800."
fi

PAYLOAD="SET_EFFECT:{\"mode\":\"$MODE\",\"colors\":[$COLORS],\"speed\":$SPEED,\"intensity\":$INTENSITY,\"timeout\":$SECONDS_VAL}"

echo "$DESC"
echo "  device:  $DEVICE  (topic $TOPIC, broker $BROKER)"
echo "  mode:    $MODE   speed=$SPEED intensity=$INTENSITY   (speed 0 = slowest)"
echo "  reverts: after ${SECONDS_VAL}s — or run '$0 $DEVICE stop'"
echo ""

# Payload must fit PubSubClient's buffer. The firmware raised it to 512 in
# #0016, but an oversized PUBLISH is dropped with no callback and no log, so
# warn rather than let it vanish silently.
if (( ${#PAYLOAD} > 400 )); then
    echo "Warning: payload is ${#PAYLOAD} bytes, close to the MQTT buffer limit." >&2
fi

publish "$PAYLOAD" && echo "Sent."
