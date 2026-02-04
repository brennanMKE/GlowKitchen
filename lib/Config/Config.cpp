#include "Config.h"

// Define the variables that were declared as extern in Config.h
// The initial values come from macros in Config.h which is ignored by git
String DEVICE_NAME = INITIAL_DEVICE_NAME;
bool irEnabled = INITIAL_IR_ENABLED;

// Function to generate unique device ID from MAC address
String deviceIdFromMac() {
  // Typically returns like "7C:DF:A1:12:34:56"
  String mac = WiFi.macAddress();
  mac.toLowerCase();
  return mac;
}

// If you prefer topic-safe IDs without colons:
String deviceIdFromMacCompact() {
  String mac = deviceIdFromMac();
  mac.replace(":", "");
  return mac; // "7cdfa1123456"
}

// Function to get device ID for MQTT topics (immutable)
String getDeviceId() {
  // This returns the immutable hardware-based device ID
  return deviceIdFromMacCompact();
}

// Function to get device name (configurable)
String getDeviceName() {
  if (DEVICE_NAME.length() > 0) {
    return DEVICE_NAME;
  } else {
    // Fallback to device ID if no name set
    return getDeviceId();
  }
}
