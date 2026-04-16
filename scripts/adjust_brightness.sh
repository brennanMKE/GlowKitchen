#!/bin/bash

# Configuration
BROKER="homeassistant.local"

# Check arguments
if [ "$#" -lt 2 ]; then
    echo "Usage: $0 [location] [UP|DOWN]"
    echo "Locations: kitchen, tv, desk, all"
    echo "Example: $0 all UP"
    exit 1
fi

LOCATION=$1
ACTION=$2

# Standardize action to uppercase
ACTION=$(echo "$ACTION" | tr '[:lower:]' '[:upper:]')

if [ "$ACTION" == "UP" ]; then
    PAYLOAD="BRIGHT_UP"
elif [ "$ACTION" == "DOWN" ]; then
    PAYLOAD="BRIGHT_DOWN"
else
    echo "Error: Action must be UP or DOWN"
    exit 1
fi

TOPIC="lights/$LOCATION/cmd"

echo "Sending $PAYLOAD to $TOPIC..."
/opt/homebrew/bin/mosquitto_pub -h "$BROKER" -p 1883 -u mqtt -P "$MQTT_PASSWORD" \
  -t "$TOPIC" -m "$PAYLOAD"

echo "Brightness adjustment sent."
