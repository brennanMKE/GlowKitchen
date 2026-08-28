#!/bin/zsh

# Shared MQTT broker resolution for the scripts in scripts/.
#
# The broker is named `homeassistant.local` at home, but that name only exists
# where the Home Assistant host runs an mDNS responder. At sites where it does
# not, the broker has to be addressed by IP or by a real DNS name instead.
# Every script therefore accepts an explicit host.
#
# Precedence (highest first):
#   1. --host <addr> / --broker <addr>  (also --host=<addr>)
#   2. $MQTT_BROKER
#   3. homeassistant.local
#
# Usage from a script:
#   SCRIPT_DIR="${0:A:h}"
#   source "$SCRIPT_DIR/lib/broker.sh"
#   parse_broker_args "$@"
#   set -- "${ARGS[@]}"
#
# After that, $BROKER holds the host and the positional parameters hold only
# the script's own arguments, with the host flag removed.

BROKER_DEFAULT="homeassistant.local"

# Line to include in a script's usage text.
BROKER_USAGE="  --host <addr>   MQTT broker hostname or IP (default: \$MQTT_BROKER or $BROKER_DEFAULT)"

parse_broker_args() {
    BROKER="${MQTT_BROKER:-$BROKER_DEFAULT}"
    ARGS=()

    while (( $# > 0 )); do
        case "$1" in
            --host|--broker)
                if [[ -z "$2" ]]; then
                    echo "Error: $1 requires a hostname or IP address" >&2
                    exit 1
                fi
                BROKER="$2"
                shift 2
                ;;
            --host=*|--broker=*)
                BROKER="${1#*=}"
                if [[ -z "$BROKER" ]]; then
                    echo "Error: ${1%%=*} requires a hostname or IP address" >&2
                    exit 1
                fi
                shift
                ;;
            --)
                shift
                ARGS+=("$@")
                break
                ;;
            *)
                ARGS+=("$1")
                shift
                ;;
        esac
    done
}
