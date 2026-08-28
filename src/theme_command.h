#pragma once

#include <string.h>

// Issue #0021: whether an incoming theme command should be ignored because a
// custom effect owns the strip right now.
//
// A retained `FOREST` on lights/all/cmd is redelivered by the broker to every
// subscriber on every reconnect, so a running effect was cancelled seconds
// after it was applied by a message nobody sent. The effect and its expiry are
// device-owned state (persisted to NVS, resumed across reboots with a
// wall-clock end time -- see issue #0017), and a broadcast aimed at the whole
// installation is not a reason to discard them.
//
// Scoped to broadcasts on purpose. A theme sent to THIS device's own topic is
// someone deliberately taking control of this strip and still wins, which --
// along with CLEAR_EFFECT -- is how a person overrides a running effect. Same
// split as the OTA_URL broadcast refusal in main.cpp: /all/ is for the fleet,
// the device topic is for the device.
//
// Lives in its own header, free of Arduino and FastLED, so the native suite
// can reach it. The predicate was originally written inline in
// onMqttMessage(), where no host test could see it.
inline bool shouldIgnoreBroadcastTheme(const char* topic, bool customEffectActive) {
    if (!customEffectActive || topic == nullptr) return false;
    return strstr(topic, "/all/") != nullptr;
}
