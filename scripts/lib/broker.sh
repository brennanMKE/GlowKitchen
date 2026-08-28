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

# Credentials and the broker address live in the repo .env (see .env.example).
# Load it HERE rather than in each script: every broker-using script sources
# this file, and it has to happen before parse_broker_args resolves $MQTT_BROKER
# -- sourcing afterwards leaves BROKER stuck on the homeassistant.local default,
# which does not resolve on every network the devices live on. Already-exported
# shell variables win, so `MQTT_BROKER=... script.sh` still overrides the file,
# and an explicit --host still overrides both.
#
# ${(%):-%x} is this file's own path even while sourced; $0 is the *sourcing*
# script under some zsh option combinations, which would resolve the wrong root.
GK_REPO_ROOT="${${(%):-%x}:A:h:h:h}"
if [[ -f "$GK_REPO_ROOT/.env" ]]; then
    # A bare `set -a; source .env` is a plain assignment per line, so the file
    # CLOBBERS anything the caller already exported -- the opposite of the
    # precedence above, and it silently ignores `MQTT_BROKER=other-host script`.
    # So snapshot the caller's values for the file's own keys and put them back.
    typeset -A _gk_env_preset
    typeset _gk_env_key
    for _gk_env_key in ${(f)"$(sed -n 's/^[[:space:]]*\([A-Za-z_][A-Za-z0-9_]*\)[[:space:]]*=.*/\1/p' "$GK_REPO_ROOT/.env")"}; do
        if (( ${+parameters[$_gk_env_key]} )); then
            _gk_env_preset[$_gk_env_key]="${(P)_gk_env_key}"
        fi
    done

    set -a
    source "$GK_REPO_ROOT/.env"
    set +a

    for _gk_env_key in ${(k)_gk_env_preset}; do
        typeset -gx "$_gk_env_key"="${_gk_env_preset[$_gk_env_key]}"
    done
    unset _gk_env_preset _gk_env_key
fi

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

# Every script publishes with these, so resolve them once from the .env loaded
# above rather than hardcoding `-u mqtt -p 1883` at each call site.
MQTT_USER="${MQTT_USERNAME:-mqtt}"
MQTT_PORT="${MQTT_PORT:-1883}"

# Fail loudly and early with the fix, instead of letting mosquitto_pub report a
# bare "Connection Refused: not authorised" ten lines later -- or worse, succeed
# at publishing and silently return nothing.
require_mqtt_password() {
    if [[ -z "$MQTT_PASSWORD" ]]; then
        echo "Error: MQTT_PASSWORD is not set." >&2
        echo "  Copy .env.example to $GK_REPO_ROOT/.env and fill in the broker credentials." >&2
        exit 1
    fi
}
