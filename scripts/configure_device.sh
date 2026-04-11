#!/bin/bash

# Configuration
BROKER="homeassistant.local"

# Check arguments
if [ "$#" -lt 2 ]; then
    echo "Usage: $0 [location] [numLeds] [optional: ledsPerColor]"
    echo "Locations: kitchen, tv, desk, all"
    echo "Example: $0 tv 120 15"
    exit 1
fi

LOCATION=$1
NUM_LEDS=$2
LEDS_PER_COLOR=$3

TOPIC="lights/$LOCATION/cmd"

# Set Number of LEDs
echo "Sending SET_NUM_LEDS:$NUM_LEDS to $TOPIC..."
/opt/homebrew/bin/mosquitto_pub -h "$BROKER" -p 1883 -u mqtt -P "$MQTT_PASSWORD" \
  -t "$TOPIC" -m "SET_NUM_LEDS:$NUM_LEDS"

# Set LEDs Per Color if provided
if [ -n "$LEDS_PER_COLOR" ]; then
    echo "Sending SET_LEDS_PER_COLOR:$LEDS_PER_COLOR to $TOPIC..."
    /opt/homebrew/bin/mosquitto_pub -h "$BROKER" -p 1883 -u mqtt -P "$MQTT_PASSWORD" \
      -t "$TOPIC" -m "SET_LEDS_PER_COLOR:$LEDS_PER_COLOR"
fi

echo "Configuration sent."
