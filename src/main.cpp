#include <Arduino.h>
#include <FastLED.h>
#include <Config.h>
#include <IRremote.h>
#include <esp_log.h>
#include <Preferences.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <esp_wifi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "time_utils.h"
// effects.h relies on CRGB/CHSV/sin8/min/max already being declared -- see
// the include-order contract at the top of that header. <FastLED.h> above
// satisfies it. effects.h itself pulls in themes.h (HueTheme, THEME_NAMES[],
// the six hue arrays) and time_utils.h (already included directly above).
#include "effects.h"
#include "theme_command.h"
// effect_parse.h (issue #0016): the hand-rolled SET_EFFECT payload parser
// and the NVS load decision. Include-order contract already satisfied by
// <FastLED.h> above (see effects.h's own comment).
#include "effect_parse.h"
// effect_timeout.h (issue #0017): live expiry, STATUS remaining-seconds,
// and the reboot resume decision table/clamp. Same include-order contract.
#include "effect_timeout.h"

static const char *TAG = "MAIN";

#define FIRMWARE_VERSION "0.0.5"

// Greppable marker so tooling can read the version straight from firmware.bin
// (see scripts/firmware_info.sh). __attribute__((used)) alone is not enough on
// this toolchain — the linker's --gc-sections still drops it because nothing
// references it. fwVersionMarkerAnchor (assigned in setup()) is a volatile
// reference that pins the marker into the binary at any optimization/log level.
static const char FW_VERSION_MARKER[] __attribute__((used)) =
    "GLOWKITCHEN_FWVER=" FIRMWARE_VERSION;
static const char *volatile fwVersionMarkerAnchor;

// Pinned in place of the full Mozilla root store, which cost 69,876 bytes to
// validate a single vendor (issue #0010). Two roots are required and it is not
// obvious why: api.github.com and github.com chain to Sectigo, but the release
// asset itself is served from objects.githubusercontent.com, which chains to
// ISRG. Pinning only Sectigo makes the version check succeed and the download
// fail -- the same "looks like no update available" failure shape as #0009,
// because nothing on the OTA path reports to MQTT.
//
// Roots, not leaves or intermediates: leaves rotate every ~90 days on the
// Let's Encrypt side and intermediates every few years, while these two run to
// 2045/2046. Both are the self-signed roots taken from a trusted store, not
// the copies GitHub presents -- the served top-of-chain certs are cross-signed
// (Sectigo E46 by USERTrust ECC, ISRG Root YR by ISRG Root X1) and so carry
// different certificate fingerprints for the same key. mbedTLS closes the
// chain on the trusted root by subject + key, so the cross-signed copies in
// the presented chain are simply unused.
//
// If GitHub migrates to an unpinned root, every GitHub OTA path fails at once,
// fleet-wide and silently. scripts/release.sh gates on that. The escape hatch
// is OTA_URL: with an http:// target, which uses a plain WiFiClient and no TLS
// at all -- which is why that branch below must stay.
//
// mbedtls_x509_crt_parse() accepts concatenated PEM blocks, so both roots live
// in one constant and one setCACert() call.
static const char GITHUB_ROOT_CAS[] PROGMEM =
    "-----BEGIN CERTIFICATE-----\n"  // Sectigo Public Server Authentication Root E46, expires 2046-03-21
    "MIICOjCCAcGgAwIBAgIQQvLM2htpN0RfFf51KBC49DAKBggqhkjOPQQDAzBfMQsw\n"
    "CQYDVQQGEwJHQjEYMBYGA1UEChMPU2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQDEy1T\n"
    "ZWN0aWdvIFB1YmxpYyBTZXJ2ZXIgQXV0aGVudGljYXRpb24gUm9vdCBFNDYwHhcN\n"
    "MjEwMzIyMDAwMDAwWhcNNDYwMzIxMjM1OTU5WjBfMQswCQYDVQQGEwJHQjEYMBYG\n"
    "A1UEChMPU2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQDEy1TZWN0aWdvIFB1YmxpYyBT\n"
    "ZXJ2ZXIgQXV0aGVudGljYXRpb24gUm9vdCBFNDYwdjAQBgcqhkjOPQIBBgUrgQQA\n"
    "IgNiAAR2+pmpbiDt+dd34wc7qNs9Xzjoq1WmVk/WSOrsfy2qw7LFeeyZYX8QeccC\n"
    "WvkEN/U0NSt3zn8gj1KjAIns1aeibVvjS5KToID1AZTc8GgHHs3u/iVStSBDHBv+\n"
    "6xnOQ6OjQjBAMB0GA1UdDgQWBBTRItpMWfFLXyY4qp3W7usNw/upYTAOBgNVHQ8B\n"
    "Af8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAwNnADBkAjAn7qRa\n"
    "qCG76UeXlImldCBteU/IvZNeWBj7LRoAasm4PdCkT0RHlAFWovgzJQxC36oCMB3q\n"
    "4S6ILuH5px0CMk7yn2xVdOOurvulGu7t0vzCAxHrRVxgED1cf5kDW21USAGKcw==\n"
    "-----END CERTIFICATE-----\n"
    "-----BEGIN CERTIFICATE-----\n"  // ISRG Root YR (Let's Encrypt), expires 2045-09-02
    "MIIFKTCCAxGgAwIBAgIRAOxGNJNgz0sP+KmC2Tqpyj0wDQYJKoZIhvcNAQELBQAw\n"
    "LjELMAkGA1UEBhMCVVMxDTALBgNVBAoTBElTUkcxEDAOBgNVBAMTB1Jvb3QgWVIw\n"
    "HhcNMjUwOTAzMDAwMDAwWhcNNDUwOTAyMjM1OTU5WjAuMQswCQYDVQQGEwJVUzEN\n"
    "MAsGA1UEChMESVNSRzEQMA4GA1UEAxMHUm9vdCBZUjCCAiIwDQYJKoZIhvcNAQEB\n"
    "BQADggIPADCCAgoCggIBANvGJnN78CTJdWL3+eGfsLN5TrNBJs+VH9hRXqRbwxu9\n"
    "sGNiB0BD1fcOxbSUQCJIM1xE13Db+5Cw1w0s0EBYsvuIP/6joF0w8cuImbgR1OGg\n"
    "YbSQ4OpzI+DG8SGuTlcE873OCS+kh3srlo6vl43M5OJg4Aeo1sfHp6kTJDoIiFBN\n"
    "JAY+OKfX/FUvYKuhjT+no49lmqmupSBI5PkBQiqrEGtWU5uxU/cQWHGu8jSjFBzn\n"
    "ZqvbNPLMXMLFxCb3WTfrJBXXjqvWG+v4bjzxjjeAtOlU7qarRDvNOyAuQYLln904\n"
    "M+faKx8hnLCpJ15ZqaEgcNlY+9MMWcC5yvL2A2j3l9+2buggZX+dOE91zYmIdawT\n"
    "vSZuVvlbRrAlLxIB6pwMBjneXCjYQ8+3BCCjssbSNpZU3hTcBDdhfAlEDlYr6pEa\n"
    "tnMdmDT5BqnKC92bd0EhM1fbLHioLccLCuievT8ZkPhZrq7Mii7gNXAcUEAR8+lz\n"
    "Yal+9zTg7C5DALyVOeG/CqfRAMn1KSHCR0NSA6P8tn/mGRlnCct5rtVCLnVySVpU\n"
    "6H1qGg3DgTOuskf8eahTMiYbI5ezPJmO5ertalskQ1utp74+eDy92PI4ftHKTbq9\n"
    "IWhH4YZKh3WnJEIt+oQvlYZbY8tpEroKrFB6PFGzrJIDRyts4HqvuH52RFj2zv/B\n"
    "AgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNVHRMBAf8EBTADAQH/MB0GA1Ud\n"
    "DgQWBBTe51tg0CJtQCh9Pw0B/qS1UrRRlDANBgkqhkiG9w0BAQsFAAOCAgEAWHnf\n"
    "713Bdkq7t5yN2dNIgQakUb94X9WuyhMEHHkgx4oDpSUlnG0w4g94MoqaEUE31ZjR\n"
    "LU7L5LD1g9ujFHTQu8AD215AHMVQFbm6j8hQxdXHAzDajFNQnOlDJrLjzIx176oy\n"
    "AjvUtejZx2NNmdb5fd0WGVGsCdoAJ3N8ozo7ajE8t6vfxStZb4BQ9WYJGHUDrv2N\n"
    "i5tJF6CNiPnlzs3BUfECRbE4JSk+jvy8+VoGiFE8qsH/j78x2fjgQhAQFV7P7Zxy\n"
    "dBTZ1wEkNpZNW2qnaK1SKBLa+xf6E06YRIq5uaI+HWH8SY1y5VbRgzq40EKg3yxP\n"
    "06fz+uYAUIFJoLNfhwRCc3Q6pQVuMX3yAjHAes4gk4moGcLQ5p7HAh39yeylZc1J\n"
    "41sx/jKwLIkPE6Rr1Nf4pxdsxf9SA4yOEiAkDgq04DVxn8hgYFdUtBCuiuVC2heA\n"
    "EiqVEa+8QZjuw8Gj0EbHXcRd1nInvGqRS1o9Is7YBdQN57X1AYveGBNNqjICSb7c\n"
    "awuw1EawTDrs13VUlJVEsbQ0/O/1aaV73mCdOQ8azqL2KTv1Ewu1xbquE2S+kdQU\n"
    "To9TUwat3wUA6cwXh1EfpS/3fJ0aGah5hdpRyoCLDlsSn8tkrjMfFFX0viC+GxHc\n"
    "sI1ANRYvqSFC2X1VRZfDg+wD6E21BccmifG4yWc=\n"
    "-----END CERTIFICATE-----\n";

// timeReached() (rollover-safe deadline test) now lives in time_utils.h,
// shared with the native test suite in test/test_rollover/ -- see that
// header for the full rationale, including why it must use uint32_t/int32_t
// rather than unsigned long/long (issue #0008).

// Clock-offset test harness (issue #0008). The 2^32 ms rollover is 49.7 days
// away in real time, which cannot be waited out to prove the fix above.
// Shifting the clock lets a debug build sit within seconds of the wrap on
// demand: `SET_CLOCK_OFFSET:4294900000` (see onMqttMessage below) puts
// nowMs() ~67s from wrapping, so the render timers can be watched crossing
// it directly on the bench.
//
// ENABLE_CLOCK_OFFSET is defined ONLY by [env:esp32c3-debug] in
// platformio.ini, never by the release env [env:esp32c3]. That is load-
// bearing: this build is USB-only and does not have to fit the OTA slot
// (see platformio.ini header), while the release build is what
// scripts/release.sh gates on 15,328 spare bytes against the 1,310,720-byte
// slot (issue #0009 -- v0.0.4 shipped over that limit and was uninstallable
// fleet-wide). In a release build nowMs() compiles down to a bare millis()
// call with the offset variable and the MQTT command handler both absent
// from the binary, so this harness costs the release build nothing.
#ifdef ENABLE_CLOCK_OFFSET
static uint32_t clockOffsetMs = 0;
static inline uint32_t nowMs() { return millis() + clockOffsetMs; }
#else
static inline uint32_t nowMs() { return millis(); }
#endif

// Issue #0017 half B: the project's one notion of "the wall clock is
// valid", reused rather than reinvented. Mirrors the threshold loopOta()
// already tests before trusting getLocalTime() -- `now.tm_year + 1900 <
// 2020` -- but loopOta() is left as-is rather than refactored to call this
// (it also needs the full struct tm for hour/day-of-year, not just a
// validity bool; see issues/0017.md section 6.4 for the "optional, weakly
// preferred" refactor this deliberately skips). time(nullptr) returns UTC
// epoch regardless of the TZ configTzTime() installs, which is exactly what
// an absolute end time wants -- no DST arithmetic, no local-time ambiguity
// across a spring-forward.
static const uint32_t CLOCK_VALID_EPOCH = 1577836800UL;  // 2020-01-01T00:00:00Z
static bool clockEpochNow(uint32_t& outEpoch) {
    time_t t = time(nullptr);
    if (t < (time_t)CLOCK_VALID_EPOCH) return false;
    outEpoch = (uint32_t)t;
    return true;
}

// Issue #0017 half B: how long a device waits after boot for the wall
// clock to sync before giving up and reverting a pending resumed effect
// unconditionally. WiFi association plus an SNTP round trip is seconds in
// the normal case; 120s is generous enough that a slow AP does not cost a
// legitimate 8-hour effect, and short enough that a device with no network
// reaches a definite answer while someone is still standing there. A
// judgement call (issues/0017.md section 6.3), not a derived number.
static const uint32_t RESUME_GRACE_MS = 120000;

// Forward declarations for OTA (called from onMqttMessage before definition)
void checkForOtaUpdate(bool manual);
void performOtaFromUrl(const String& url);
bool isLocalNetworkUrl(const String& url);
// Issue #0017: revertFromCustomEffect() (defined near cancelCustomEffect(),
// ahead of publishState() in file order) needs this forward declaration.
void publishState();

/// DEVELOPMENT OVERRIDE
// Set to a valid theme index (0-5) to force that theme, or -1 to use saved preferences
// 0=Green, 1=Rainbow, 2=Pink Pony Club, 3=Ocean Waves, 4=Sunset, 5=Forest
const int DEV_THEME_OVERRIDE = -1;  // Change this to override theme for development

/// LED

const int BRIGHTNESS = 180;
const int MAX_BRIGHTNESS = 225;
const int MIN_BRIGHTNESS = 75;

// LED Configuration - Default values, will be loaded from Preferences
#define MAX_LEDS 500
int numLeds = 240;
int ledsPerColor = 25;

#define DATA_PIN 4

CRGB leds[MAX_LEDS];
// uint32_t, not unsigned long (issue #0014): identical on the C3, but
// correct (32-bit) on a 64-bit native host too -- same lesson as
// src/time_utils.h (issue #0008).
uint32_t timeouts[MAX_LEDS];
// SPARKLE's per-LED brightness array (issue #0015) -- wired to fx.sparkleVal
// in setupLED(). ~500 bytes SRAM at MAX_LEDS.
uint8_t sparkleValues[MAX_LEDS];

// Theme System -- HueTheme, THEME_NAMES[] and the six hue arrays moved to
// src/themes.h (issue #0014) so the native frame-capture test
// (test/test_effects/) can link against the real tables. EffectMode,
// BUILTIN_EFFECT_MODE[], CustomEffectConfig and the two renderers moved to
// src/effects.h, both included above.

// Theme management
HueTheme currentTheme = THEME_GREEN;
bool colorChangeEnabled = true;
bool ledsEnabled = true;  // Global LED state - true = on, false = off
bool mirrorEnabled = false;  // Mirror mode for bi-directional strips
// Automatic GitHub OTA checks (startup + nightly). Turned off with OTA_AUTO:false
// while a device runs a locally-pushed dev build, which would otherwise be
// replaced by the released tag within ~15s of its next boot.
bool otaAutoEnabled = true;
int hueIndex = 0;
unsigned long logTimeout = 0;
unsigned long logInterval = 10000;

// Effect engine state (issue #0014). Zero-initialized, matching the exact
// prior defaults: blendTimeout/rotateTimeout/hueTimeout/blendOffset all
// started at 0 before this refactor too (only hueTimeout and timeouts[] were
// explicitly seeded in setupLED() -- see there). hueIndex stays a separate
// global rather than living in fx: it has 17 references across preferences,
// MQTT and logging, and is copied into/out of fx around each render call
// instead (see loopLED()).
//
// blendTimeout and rotateTimeout are the two timers the ENABLE_CLOCK_OFFSET
// handler in onMqttMessage() re-bases when the debug clock jumps (issue
// #0008) -- accessed there as fx.blendTimeout / fx.rotateTimeout.
EffectState fx = {};

// Nothing populates this in this phase (issue #0014) -- see CustomEffectConfig
// in src/effects.h. Zero-initialized: mode == EFFECT_BLEND == 0, colorCount
// == 0, which is exactly what the THEME_CUSTOM boot guard in
// loadAllPreferences() checks for.
CustomEffectConfig customEffect = {};

// Issue #0017 half B: wall-clock end time for reboot survival, stored under
// its own NVS key ("fx_end") rather than a CustomEffectConfig field, so
// this stays purely additive to #0016's layout and needs no
// SETTINGS_VERSION bump (issues/0017.md section 6.2). effectResumePending/
// effectResumeGraceUntil are pure in-RAM bookkeeping for the deferred boot
// decision in loopEffectTimeout() -- never persisted, since a second reboot
// must re-derive from the unchanged effectEndEpoch, not from progressively
// adjusted in-RAM state.
static uint32_t effectEndEpoch = 0;        // UTC seconds; 0 = no timeout / not resumable
static bool     effectResumePending = false;
static uint32_t effectResumeGraceUntil = 0;

// Two-line wrapper over Arduino random(lo, hi) so the shipped behavior is
// byte-for-byte what flickerLEDs() did before this refactor -- see the
// EffectState::rng comment in src/effects.h. The native test injects a
// fixed-seed LCG instead.
uint32_t arduinoRandomRange(uint32_t lo, uint32_t hi) {
    return (uint32_t)random((long)lo, (long)hi);
}

// NEC Remote Button Constants (Protocol: NEC, Address: 0x0)
enum GrayRemoteButton {
    NEC_VOL_PLUS = 0x09,         // Vol+ - Increase Brightness
    NEC_VOL_MINUS = 0x15,        // Vol- - Decrease Brightness
    NEC_REWIND = 0x40,           // Rewind - Previous Theme
    NEC_FORWARD = 0x43,          // Forward - Next Theme
    NEC_PLAY_PAUSE = 0x44,       // Play/Pause - Toggle LEDs On/Off
    NEC_MODE = 0x46,             // Mode - Toggle Auto Color Change
    NEC_POWER = 0x45,            // Power - Toggle LEDs On/Off (same as Play/Pause)
    NEC_MUTE = 0x47,             // Mute - (reserved for future use)
    NEC_EQ = 0x07,               // EQ - (reserved for future use)
    NEC_RPT = 0x19,              // RPT - (reserved for future use)
    NEC_USD = 0x0D,              // U/SD - (reserved for future use)
    NEC_0 = 0x16,                // Number 0
    NEC_1 = 0x0C,                // Number 1
    NEC_2 = 0x18,                // Number 2
    NEC_3 = 0x5E,                // Number 3
    NEC_4 = 0x08,                // Number 4
    NEC_5 = 0x1C,                // Number 5
    NEC_6 = 0x5A,                // Number 6
    NEC_7 = 0x42,                // Number 7
    NEC_8 = 0x52,                // Number 8
    NEC_9 = 0x4A,                // Number 9
    NEC_UNKNOWN = 0xFF           // Unknown button
};

// Button debouncing variables
unsigned long lastButtonTime = 0;
unsigned long buttonDebounceDelay = 100;  // 100ms debounce delay
GrayRemoteButton lastButton = NEC_UNKNOWN;

// Preferences for persistent storage
Preferences preferences;

/// IR
const int irReceiverPin = 3;
unsigned long irLogTimeout = 0;
unsigned long irLogInterval = 10000;
const bool IR_DEBUG_MODE = true;  // Set to false to reduce logging

/// MQTT & WiFi

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// Non-blocking reconnect timing
static const uint32_t MQTT_RETRY_MS = 5000;
uint32_t mqttRetryAt = 0;

// Theme switching - now simplified to direct button control

const uint8_t NEC_REMOTE_ADDRESS = 0x0;

// Theme Helper Functions
const char* getThemeMqttCommand(HueTheme theme) {
    switch (theme) {
        case THEME_GREEN: return "GREEN";
        case THEME_RAINBOW: return "RAINBOW";
        case THEME_PINK_PONY: return "PINK_PONY";
        case THEME_OCEAN_WAVES: return "OCEAN_WAVES";
        case THEME_SUNSET: return "SUNSET";
        case THEME_FOREST: return "FOREST";
        default: return "GREEN";
    }
}

// Renamed from getCurrentHueArray()/getCurrentHueCount() (issue #0014) to
// match the spec's naming now that a non-hue (THEME_CUSTOM) source exists.
// Return type stays `const uint8_t*` -- a HUE array -- for this phase, even
// though the spec sketches `const CRGB*`: renderBlend() does hue-space
// arithmetic (shortest path around the 256-hue circle) that has no meaning
// on RGB triples, so converting the built-ins to CRGB here would change what
// BLEND renders. Converting the *custom* colors into hue space is issue
// #0016's problem -- customHues (src/effects.h) is the derived cache that
// bridges the two representations for THEME_CUSTOM.
// Return type widened from `const uint8_t*` to `const PaletteColor*` (issue
// #0015): the six built-in arrays in src/themes.h are now {hue, 255, 200}
// triples rather than bare hue bytes, and customPalette (src/effects.h) is
// the derived-cache analog of the old customHues.
const PaletteColor* getCurrentColorArray() {
    switch (currentTheme) {
        case THEME_GREEN: return GREEN_HUES;
        case THEME_RAINBOW: return RAINBOW_HUES;
        case THEME_PINK_PONY: return PINK_PONY_HUES;
        case THEME_OCEAN_WAVES: return OCEAN_HUES;
        case THEME_SUNSET: return SUNSET_HUES;
        case THEME_FOREST: return FOREST_HUES;
        case THEME_CUSTOM: return customPalette;
        default: return GREEN_HUES;
    }
}

int getCurrentColorCount() {
    switch (currentTheme) {
        case THEME_GREEN: return NUM_GREEN_HUES;
        case THEME_RAINBOW: return NUM_RAINBOW_HUES;
        case THEME_PINK_PONY: return NUM_PINK_PONY_HUES;
        case THEME_OCEAN_WAVES: return NUM_OCEAN_HUES;
        case THEME_SUNSET: return NUM_SUNSET_HUES;
        case THEME_FOREST: return NUM_FOREST_HUES;
        case THEME_CUSTOM: return customEffect.colorCount;
        default: return NUM_GREEN_HUES;
    }
}

void saveThemeToPreferences() {
    preferences.begin("glow_kitchen", false);
    preferences.putUChar("theme", (unsigned char)currentTheme);
    preferences.end();
    ESP_LOGI(TAG, "Theme saved: %s", THEME_NAMES[currentTheme]);
}

void saveBrightnessToPreferences() {
    int currentBrightness = FastLED.getBrightness();
    preferences.begin("glow_kitchen", false);
    preferences.putUChar("brightness", (unsigned char)currentBrightness);
    preferences.end();
    ESP_LOGI(TAG, "Brightness saved: %d", currentBrightness);
}

void saveHueToPreferences() {
    preferences.begin("glow_kitchen", false);
    preferences.putUChar("hue_index", (unsigned char)hueIndex);
    preferences.end();
    ESP_LOGI(TAG, "Hue index saved: %d", hueIndex);
}

void saveLedConfigToPreferences() {
    preferences.begin("glow_kitchen", false);
    preferences.putInt("num_leds", numLeds);
    preferences.putInt("leds_per_color", ledsPerColor);
    preferences.end();
    ESP_LOGI(TAG, "LED Config saved: num_leds=%d, leds_per_color=%d", numLeds, ledsPerColor);
}

void saveDeviceNameToPreferences() {
    preferences.begin("glow_kitchen", false);
    preferences.putString("device_name", DEVICE_NAME);
    preferences.end();
    ESP_LOGI(TAG, "Device name saved: %s", DEVICE_NAME.c_str());
}

void saveIrFlagToPreferences() {
    preferences.begin("glow_kitchen", false);
    preferences.putBool("enable_ir", irEnabled);
    preferences.end();
    ESP_LOGI(TAG, "IR flag saved: %s", irEnabled ? "true" : "false");
}

void saveMirrorToPreferences() {
    preferences.begin("glow_kitchen", false);
    preferences.putBool("mirror_mode", mirrorEnabled);
    preferences.end();
    ESP_LOGI(TAG, "Mirror mode saved: %s", mirrorEnabled ? "true" : "false");
}

void saveOtaAutoToPreferences() {
    preferences.begin("glow_kitchen", false);
    preferences.putBool("ota_auto", otaAutoEnabled);
    preferences.end();
    ESP_LOGI(TAG, "OTA auto-update saved: %s", otaAutoEnabled ? "true" : "false");
}

// Issue #0016: the custom effect's NVS record, modelled on the seven helpers
// above -- a per-key Preferences session, not a monolithic "saveSettings()"
// (this codebase has no such function; see effect_parse.h's header comment).
// Both keys are <=15 characters (the NVS key limit). Write the version byte
// BEFORE the blob in the same session: if the blob write somehow fails, the
// version byte is already correct, which is the safe direction -- a correct
// version with a stale/short blob is caught by the length check on the next
// load (applyLoadedCustomEffect(), src/effect_parse.h).
void saveCustomEffectToPreferences() {
    preferences.begin("glow_kitchen", false);
    preferences.putUChar("fx_ver", SETTINGS_VERSION);
    preferences.putBytes("fx_cfg", &customEffect, sizeof(customEffect));
    // Issue #0017 half B: always written in the same session as fx_ver/
    // fx_cfg, so the two can never diverge -- fx_end == 0 is itself a
    // legal, meaningful value ("no timeout"/"not resumable"), never stale.
    preferences.putUInt("fx_end", effectEndEpoch);
    preferences.end();
    ESP_LOGI(TAG, "Custom effect saved: mode=%s colors=%u speed=%u intensity=%u timeoutMs=%lu fx_end=%lu",
             EFFECT_MODE_NAMES[customEffect.mode], customEffect.colorCount,
             customEffect.speed, customEffect.intensity,
             (unsigned long)customEffect.timeoutMs, (unsigned long)effectEndEpoch);
}

// Issue #0016 section 6.3 (widened by issue #0017): the cancel path, called
// from every non-CLEAR_EFFECT exit from a custom effect (explicit theme
// dispatch, switchToNextTheme(), switchToPreviousTheme()) so an abandoned
// custom effect never leaves a stale timeout armed. The caller sets
// currentTheme itself -- this only tears down the timeout bookkeeping.
// No-op (and no needless NVS write) if there was nothing to cancel.
//
// The guard grew two terms in issue #0017: without them, a device sitting
// in the "resume pending" state with timeoutMs == 0 (an untimed effect that
// still has a stale fx_end/effectResumePending from before) would return
// early and never clear fx_end -- a landmine for the next reboot.
void cancelCustomEffect() {
    if (customEffect.timeoutMs == 0 && customEffect.activatedAt == 0
        && effectEndEpoch == 0 && !effectResumePending) return;
    customEffect.timeoutMs  = 0;
    customEffect.activatedAt = 0;
    effectEndEpoch      = 0;      // issue #0017
    effectResumePending = false;  // issue #0017
    saveCustomEffectToPreferences();
}

// Issue #0017 section 2.3: the shared revert path. CLEAR_EFFECT and the
// live timeout expiry (loopEffectTimeout()) both funnel through this
// instead of duplicating the revert logic, so they cannot drift into two
// different notions of "revert" (e.g. one clearing the wall-clock end time,
// the other forgetting to, and resurrecting a ghost effect on the next
// reboot).
void revertFromCustomEffect() {
    if (currentTheme != THEME_CUSTOM) return;          // idempotent
    HueTheme t = customEffect.revertTheme;
    if (t >= CYCLEABLE_THEME_COUNT) t = THEME_GREEN;   // never revert into THEME_CUSTOM
    currentTheme = t;
    hueIndex = 0;
    // Keep mode/colors/colorCount -- colorCount == 0 keeps meaning "never
    // configured" for the THEME_CUSTOM boot guard, not the ambiguous
    // "cleared" (issue #0016 section 6.2).
    customEffect.timeoutMs  = 0;
    customEffect.activatedAt = 0;
    effectEndEpoch      = 0;      // issue #0017 half B
    effectResumePending = false;  // issue #0017 half B
    saveThemeToPreferences();
    saveCustomEffectToPreferences();  // also writes fx_end = 0

    // Force immediate visual update, mirroring the theme-string handler.
    // hueIndex = 0 before reading the palette matters -- the custom effect
    // may have had 1 color and the revert target 8, or vice versa;
    // renderFlicker() indexes colors[hueIndex] unguarded.
    const PaletteColor* currentColors = getCurrentColorArray();
    PaletteColor newColor = currentColors[hueIndex];
    fill_solid(leds, numLeds, CHSV(newColor.h, newColor.s, newColor.v));
    FastLED.show();
    publishState();
}

void loadAllPreferences() {
    ESP_LOGI(TAG, "loadAllPreferences()");
    preferences.begin("glow_kitchen", true);

    // Load device name
    DEVICE_NAME = preferences.getString("device_name", INITIAL_DEVICE_NAME);

    // Load IR flag
    irEnabled = preferences.getBool("enable_ir", true);

    // Load mirror mode
    mirrorEnabled = preferences.getBool("mirror_mode", false);

    // Load OTA auto-update flag
    otaAutoEnabled = preferences.getBool("ota_auto", true);

    // Load LED count - use default if not in preferences
    numLeds = preferences.getInt("num_leds", 240);
    ledsPerColor = preferences.getInt("leds_per_color", 25);

    // Safety check
    if (numLeds > MAX_LEDS) numLeds = MAX_LEDS;
    if (numLeds < 1) numLeds = 1;
    if (ledsPerColor < 1) ledsPerColor = 1;

    // Issue #0016: load the custom effect BEFORE the theme-resolution block
    // below -- the THEME_CUSTOM boot guard a few lines down tests
    // customEffect.colorCount == 0, so it would read garbage if this ran
    // after it. applyLoadedCustomEffect() (src/effect_parse.h) is the pure
    // decision function; this is just the four-line Preferences wrapper
    // around it. A missing "fx_ver"/"fx_cfg" (pre-#0016 firmware, or a
    // device that has never set an effect) reads back version 0 / length 0,
    // which applyLoadedCustomEffect() treats as "never written" rather than
    // "corrupt" -- no log on that path, since it would otherwise fire on
    // every boot of every device on the very first upgrade.
    uint8_t fxVer = preferences.getUChar("fx_ver", 0);
    size_t fxLen = preferences.getBytesLength("fx_cfg");
    uint8_t fxBuf[sizeof(CustomEffectConfig)];
    size_t fxGot = (fxLen == sizeof(fxBuf)) ? preferences.getBytes("fx_cfg", fxBuf, sizeof(fxBuf)) : 0;
    bool fxAccepted = applyLoadedCustomEffect(fxVer, fxGot, fxBuf, customEffect);
    if (!fxAccepted) {
        if (fxVer != 0 || fxLen != 0) {
            ESP_LOGE(TAG, "custom effect: stored v%u/%uB rejected, reset to defaults", fxVer, (unsigned)fxLen);
        }
    }
    // Issue #0017 half B: gated on the config having been accepted -- if
    // applyLoadedCustomEffect() rejected and reset the config, a leftover
    // end time is meaningless; treat it as 0. No need to erase the key --
    // the next save rewrites it (section 6.2).
    effectEndEpoch = fxAccepted ? preferences.getUInt("fx_end", 0) : 0;
    refreshCustomPalette();

    // Check for development theme override first
    if (DEV_THEME_OVERRIDE >= 0 && DEV_THEME_OVERRIDE < CYCLEABLE_THEME_COUNT) {
        currentTheme = (HueTheme)DEV_THEME_OVERRIDE;
        ESP_LOGI(TAG, "*** DEVELOPMENT OVERRIDE ACTIVE ***");
        ESP_LOGI(TAG, "Forcing theme to: %s (index %d)", THEME_NAMES[currentTheme], DEV_THEME_OVERRIDE);
    } else {
        // Load theme from preferences
        unsigned char savedTheme = preferences.getUChar("theme", THEME_GREEN);
        if (savedTheme < THEME_COUNT) {
            currentTheme = (HueTheme)savedTheme;
        } else {
            currentTheme = THEME_GREEN;
            ESP_LOGI(TAG, "Invalid saved theme, defaulting to Green");
        }
    }

    // THEME_CUSTOM boot guard (issue #0014). Nothing populates customEffect
    // in this phase, so this always fires for any path that reaches
    // THEME_CUSTOM (a forced DEV_THEME_OVERRIDE can't -- it's bounded to
    // CYCLEABLE_THEME_COUNT above -- but a stray/corrupt NVS byte of 6 could).
    // Falling back to Green avoids rendering (and dividing/moduloing by) a
    // zero-length color array in renderBlend()/renderFlicker(), i.e. a
    // crash loop.
    if (currentTheme == THEME_CUSTOM && customEffect.colorCount == 0) {
        currentTheme = THEME_GREEN;
        ESP_LOGI(TAG, "THEME_CUSTOM with no effect configured, falling back to Green");
    }

    // Issue #0017 half B: arm the deferred resume decision for a timed
    // custom effect that survived the boot guard above. This cannot be a
    // plain if/else here -- the wall clock is never synced at this point in
    // boot (configTzTime() runs inside loopOta(), only after WiFi joins;
    // issues/0017.md section 6.1) -- so the actual decision (resume /
    // revert-expired / revert-no-clock) is deferred to loopEffectTimeout(),
    // which resolves it once the clock syncs or a grace window elapses.
    //
    // customEffect.activatedAt is already 0 here (applyLoadedCustomEffect()
    // forces it). customEffect.timeoutMs is deliberately left INTACT rather
    // than zeroed -- it is both the "was this actually a timed effect"
    // signal tested below and the original duration resumeTimeoutMs()
    // clamps against on resume. This is safe because loopEffectTimeout()
    // unconditionally returns early while effectResumePending is true, so
    // effectTimeoutExpired() never evaluates this half-seeded state (the
    // alternative considered was zeroing timeoutMs and stashing the
    // original value separately -- issues/0017.md section 6.3 flags that
    // approach as easy to get wrong by simply forgetting the stash).
    if (currentTheme == THEME_CUSTOM && customEffect.timeoutMs > 0) {
        effectResumePending = true;
        effectResumeGraceUntil = nowMs() + RESUME_GRACE_MS;
        ESP_LOGI(TAG, "Custom effect timeout pending resume decision (grace %lu ms)",
                 (unsigned long)RESUME_GRACE_MS);
    }

    // Load brightness
    unsigned char savedBrightness = preferences.getUChar("brightness", BRIGHTNESS);
    int brightnessToSet = min(MAX_BRIGHTNESS, max(10, (int)savedBrightness));

    // Load hue index
    unsigned char savedHueIndex = preferences.getUChar("hue_index", 0);
    int maxHueIndex = getCurrentColorCount() - 1;
    hueIndex = min(maxHueIndex, (int)savedHueIndex);
    
    preferences.end();
    
    // Apply loaded brightness
    FastLED.setBrightness(brightnessToSet);
    
    // Log all loaded preferences
    ESP_LOGI(TAG, "=== LOADED PREFERENCES ===");
    if (DEV_THEME_OVERRIDE >= 0 && DEV_THEME_OVERRIDE < CYCLEABLE_THEME_COUNT) {
        ESP_LOGI(TAG, "Theme: %s (DEV OVERRIDE)", THEME_NAMES[currentTheme]);
    } else {
        ESP_LOGI(TAG, "Theme: %s", THEME_NAMES[currentTheme]);
    }
    ESP_LOGI(TAG, "Brightness: %d (max: %d)", brightnessToSet, MAX_BRIGHTNESS);
    ESP_LOGI(TAG, "Hue Index: %d (max: %d)", hueIndex, maxHueIndex);
    ESP_LOGI(TAG, "Color Change: %s", colorChangeEnabled ? "enabled" : "disabled");
    ESP_LOGI(TAG, "Mirror Mode: %s", mirrorEnabled ? "enabled" : "disabled");
    ESP_LOGI(TAG, "IR Enabled: %s", irEnabled ? "true" : "false");
    ESP_LOGI(TAG, "OTA Auto: %s", otaAutoEnabled ? "true" : "false");
    ESP_LOGI(TAG, "========================");
}

void switchToNextTheme() {
    HueTheme oldTheme = currentTheme;
    // Issue #0016: NEXT_THEME (MQTT and the IR remote, via
    // handleGrayRemoteButton()) must tear down an active custom effect's
    // timeout, or an effect abandoned by cycling away from it would still be
    // sitting there armed to revert later, onto whatever theme cycling has
    // since landed on.
    cancelCustomEffect();
    // % CYCLEABLE_THEME_COUNT, not THEME_COUNT (issue #0014): NEXT_THEME must
    // keep cycling only the original six -- THEME_CUSTOM stays unreachable
    // by cycling.
    currentTheme = (HueTheme)((currentTheme + 1) % CYCLEABLE_THEME_COUNT);
    hueIndex = 0; // Reset to first hue in new theme
    saveThemeToPreferences();
    ESP_LOGI(TAG, "Switched from %s to %s theme", THEME_NAMES[oldTheme], THEME_NAMES[currentTheme]);

    // Force immediate visual update
    const PaletteColor* currentColors = getCurrentColorArray();
    PaletteColor newColor = currentColors[hueIndex];
    fill_solid(leds, numLeds, CHSV(newColor.h, newColor.s, newColor.v));
    FastLED.show();
}

void switchToPreviousTheme() {
    HueTheme oldTheme = currentTheme;
    // Issue #0016: same reasoning as switchToNextTheme() above.
    cancelCustomEffect();
    // Same CYCLEABLE_THEME_COUNT reasoning as switchToNextTheme() above.
    currentTheme = (HueTheme)((currentTheme - 1 + CYCLEABLE_THEME_COUNT) % CYCLEABLE_THEME_COUNT);
    hueIndex = 0; // Reset to first hue in new theme
    saveThemeToPreferences();
    ESP_LOGI(TAG, "Switched from %s to %s theme", THEME_NAMES[oldTheme], THEME_NAMES[currentTheme]);

    // Force immediate visual update
    const PaletteColor* currentColors = getCurrentColorArray();
    PaletteColor newColor = currentColors[hueIndex];
    fill_solid(leds, numLeds, CHSV(newColor.h, newColor.s, newColor.v));
    FastLED.show();
}

// LED Control Functions
void increaseBrightness() {
    int currentBrightness = FastLED.getBrightness();
    int newBrightness = min(MAX_BRIGHTNESS, currentBrightness + 10);
    FastLED.setBrightness(newBrightness);
    FastLED.show();
    saveBrightnessToPreferences();
    ESP_LOGI(TAG, "Brightness increased to %d (max: %d)", newBrightness, MAX_BRIGHTNESS);
}

void decreaseBrightness() {
    int currentBrightness = FastLED.getBrightness();
    int newBrightness = max(MIN_BRIGHTNESS, currentBrightness - 10);
    FastLED.setBrightness(newBrightness);
    FastLED.show();
    saveBrightnessToPreferences();
    ESP_LOGI(TAG, "Brightness decreased to %d (min: 10)", newBrightness);
}

// Removed previousHueSet() and nextHueSet() - themes now change directly with Left/Right

void toggleColorChange() {
    colorChangeEnabled = !colorChangeEnabled;
    ESP_LOGI(TAG, "Color change %s", colorChangeEnabled ? "enabled" : "disabled");
}

void toggleAllLEDs() {
    ledsEnabled = !ledsEnabled;
    
    if (!ledsEnabled) {
        // Turn off all LEDs - be aggressive about it
        ESP_LOGI(TAG, "Turning OFF all LEDs - setting to black");
        fill_solid(leds, numLeds, CRGB::Black);
        FastLED.show();
        delay(10); // Small delay to ensure the command is processed
        fill_solid(leds, numLeds, CRGB::Black);
        FastLED.show();
        ESP_LOGI(TAG, "All LEDs turned OFF");
    } else {
        // Turn on all LEDs with current hue from current theme
        ESP_LOGI(TAG, "Turning ON all LEDs");
        const PaletteColor* currentColors = getCurrentColorArray();
        PaletteColor color = currentColors[hueIndex];
        fill_solid(leds, numLeds, CHSV(color.h, color.s, color.v));
        FastLED.show();
        ESP_LOGI(TAG, "All LEDs turned ON (Theme: %s)", THEME_NAMES[currentTheme]);
    }
}

// MQTT Helpers
void publishState() {
    if (!mqtt.connected()) return;

    String topic = "lights/" + String(getDeviceName()) + "/state";
    String payload = "{\"theme\":\"" + String(THEME_NAMES[currentTheme]) + "\",";
    payload += "\"numLeds\":" + String(numLeds) + ",";
    payload += "\"ledsPerColor\":" + String(ledsPerColor) + ",";
    payload += "\"brightness\":" + String(FastLED.getBrightness()) + ",";
    payload += "\"ledsEnabled\":" + String(ledsEnabled ? "true" : "false") + ",";
    payload += "\"mirrorEnabled\":" + String(mirrorEnabled ? "true" : "false") + ",";
    payload += "\"irEnabled\":" + String(irEnabled ? "true" : "false") + ",";
    payload += "\"otaAuto\":" + String(otaAutoEnabled ? "true" : "false") + ",";
    payload += "\"firmwareVersion\":\"" + String(FIRMWARE_VERSION) + "\"";

    // Issue #0017 section 4.3: emitted ONLY when a custom effect is active,
    // so old clients see byte-identical STATUS payloads otherwise.
    // Deliberately omits "colors" even though the parent spec's example
    // includes them -- SET_EFFECT is the only thing that sets them, the
    // sender already knows them, and re-serializing up to eight RGB
    // triples into every retained state publish spends flash and wire for
    // no consumer; see docs/mqtt_commands.md. timeoutRemaining is computed
    // from the millis()-domain clock (nowMs()), same as the live expiry
    // check -- see effect_timeout.h's comment on why that's right even
    // once half B's wall-clock epoch exists.
    if (currentTheme == THEME_CUSTOM) {
        // Issue #0017 half B: while a boot-time resume decision is still
        // pending, activatedAt is 0 but timeoutMs still holds the full
        // original duration (see the boot-arm comment in
        // loadAllPreferences()) -- feeding those straight into
        // effectTimeoutRemainingSeconds() would read as "elapsed since
        // boot" and report a bogus near-zero remainder during the grace
        // window. Report the original duration instead until the decision
        // resolves one way or the other.
        int32_t remaining = effectResumePending
            ? (int32_t)(customEffect.timeoutMs / 1000UL)
            : effectTimeoutRemainingSeconds(customEffect.timeoutMs, customEffect.activatedAt, nowMs());
        payload += ",\"custom\":{\"mode\":\"" + String(EFFECT_MODE_NAMES[customEffect.mode]) +
                   "\",\"timeoutRemaining\":" + String(remaining) + "}";
    }
    payload += "}";

    // Issue #0016: PubSubClient's publish() drops an oversized packet and
    // returns false, silently, with no other symptom -- the default
    // MQTT_MAX_PACKET_SIZE is 256 bytes for the WHOLE packet (topic +
    // payload + overhead), and this payload has been growing (SET_EFFECT
    // added a "theme":"Custom" possibility, and every future field here
    // narrows the same margin). ESP_LOGE, not LOGW/LOGI, for the same
    // release-visibility reason as SET_EFFECT's rejection log above.
    if (!mqtt.publish(topic.c_str(), payload.c_str(), true)) {
        ESP_LOGE(TAG, "publishState: publish to %s FAILED (payload %u bytes) -- "
                 "likely exceeds the MQTT buffer; see mqtt.setBufferSize() in ensureMqtt()",
                 topic.c_str(), (unsigned)payload.length());
        return;
    }
    ESP_LOGI(TAG, "Published state to %s", topic.c_str());
}

void broadcastCommand(const char* cmd) {
    if (mqtt.connected()) {
        mqtt.publish("lights/all/cmd", cmd);
        ESP_LOGI(TAG, "Broadcasted command to all: %s", cmd);
    }
}

// Button debouncing function
bool isButtonDebounced(GrayRemoteButton button) {
    unsigned long now = millis();
    
    // Check if enough time has passed since last button press
    if ((now - lastButtonTime) < buttonDebounceDelay) {
        // If it's the same button within debounce period, ignore it
        if (button == lastButton) {
            ESP_LOGI(TAG, "Button debounced");
            return false;
        }
    }
    
    // Update debounce tracking
    lastButtonTime = now;
    lastButton = button;
    return true;
}

// Button Handler Function
void handleGrayRemoteButton(GrayRemoteButton button) {
    unsigned long now = millis();
    
    // Apply debouncing
    if (!isButtonDebounced(button)) {
        return; // Button press ignored due to debouncing
    }
    
    switch (button) {
        case NEC_VOL_PLUS:
            increaseBrightness();
            {
                String cmd = "SET_BRIGHTNESS:" + String(FastLED.getBrightness());
                broadcastCommand(cmd.c_str());
            }
            break;
        case NEC_VOL_MINUS:
            decreaseBrightness();
            {
                String cmd = "SET_BRIGHTNESS:" + String(FastLED.getBrightness());
                broadcastCommand(cmd.c_str());
            }
            break;
        case NEC_REWIND:
            // Rewind = Previous Theme
            switchToPreviousTheme();
            broadcastCommand(getThemeMqttCommand(currentTheme));
            break;
        case NEC_FORWARD:
            // Forward = Next Theme
            switchToNextTheme();
            broadcastCommand(getThemeMqttCommand(currentTheme));
            break;
        case NEC_PLAY_PAUSE:
        case NEC_POWER:
            // Both Play/Pause and Power toggle LEDs
            toggleAllLEDs();
            broadcastCommand(ledsEnabled ? "ON" : "OFF");
            break;
        case NEC_MODE:
            // Mode = Toggle auto color change
            toggleColorChange();
            broadcastCommand(colorChangeEnabled ? "COLOR_CHANGE_ON" : "COLOR_CHANGE_OFF");
            break;
        default:
            ESP_LOGI(TAG, "Unknown or unmapped button pressed (Command: 0x%X)", button);
            break;
    }
}

void onMqttMessage(char* topic, byte* payload, unsigned int len) {
    String msg;
    msg.reserve(len);
    for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
    msg.trim(); // Remove any hidden whitespace/newlines
    
    String msgUpper = msg;
    msgUpper.toUpperCase(); // Ensure case-insensitive matching for commands

    ESP_LOGI(TAG, "MQTT Topic: %s, Payload: '%s'", topic, msg.c_str());

    if (msgUpper == "NEXT_THEME") {
        switchToNextTheme();
        publishState();
    } else if (msgUpper == "PREV_THEME") {
        switchToPreviousTheme();
        publishState();
    } else if (msgUpper == "STATUS") {
        publishState();
    } else if (msgUpper.startsWith("SET_NUM_LEDS:")) {
        int val = msgUpper.substring(13).toInt();
        if (val > 0 && val <= MAX_LEDS) {
            numLeds = val;
            saveLedConfigToPreferences();
            fill_solid(leds, MAX_LEDS, CRGB::Black);
            FastLED.show();
            publishState();
        }
    } else if (msgUpper.startsWith("SET_LEDS_PER_COLOR:")) {
        int val = msgUpper.substring(19).toInt();
        if (val > 0) {
            ledsPerColor = val;
            saveLedConfigToPreferences();
            publishState();
        }
    } else if (msgUpper.startsWith("SET_BRIGHTNESS:")) {
        int val = msgUpper.substring(15).toInt();
        if (val >= 0 && val <= 255) {
            if (FastLED.getBrightness() != val) {
                FastLED.setBrightness(val);
                FastLED.show();
                saveBrightnessToPreferences();
                publishState();
            }
        }
    } else if (msgUpper.startsWith("SET_DEVICE_NAME:")) {
        String newName = msg.substring(16); // Use original msg to preserve case
        newName.trim();
        if (newName.length() > 0 && newName != DEVICE_NAME) {
            // The address this device has been publishing under so far: its old
            // name, or the hardware id while it was still unnamed.
            String oldName = getDeviceName();

            // Retained state under the old address would otherwise linger on the
            // broker forever. Nothing ever publishes there again, so tools that
            // read retained state keep reporting a device that no longer exists
            // — which is exactly how a freshly-named board leaves a ghost behind
            // under its hardware id. A zero-length retained publish deletes it.
            String oldStateTopic = "lights/" + oldName + "/state";
            mqtt.publish(oldStateTopic.c_str(), "", true);

            // Stop answering on the previous name too, but never drop the
            // hardware-id topic: that is the permanent address for reaching a
            // device whose name has been forgotten or mistyped.
            if (oldName != getDeviceId()) {
                String oldCmdTopic = "lights/" + oldName + "/cmd";
                mqtt.unsubscribe(oldCmdTopic.c_str());
            }

            DEVICE_NAME = newName;
            saveDeviceNameToPreferences();

            // Subscribe to the new name topic immediately
            String newTopic = "lights/" + DEVICE_NAME + "/cmd";
            mqtt.subscribe(newTopic.c_str());
            ESP_LOGI(TAG, "Device name set to: %s (was %s), subscribed to %s",
                     DEVICE_NAME.c_str(), oldName.c_str(), newTopic.c_str());

            publishState();
        }
    } else if (msgUpper.startsWith("SET_IR_FLAG:")) {
        String val = msgUpper.substring(12);
        val.toLowerCase();
        irEnabled = (val == "true" || val == "1");
        saveIrFlagToPreferences();
        ESP_LOGI(TAG, "IR Flag set to: %s", irEnabled ? "true" : "false");
        publishState();
    } else if (msgUpper.startsWith("SET_MIRROR:")) {
        String val = msgUpper.substring(11);
        val.toLowerCase();
        mirrorEnabled = (val == "true" || val == "1");
        saveMirrorToPreferences();
        ESP_LOGI(TAG, "Mirror mode set to: %s", mirrorEnabled ? "true" : "false");
        publishState();
#ifdef ENABLE_CLOCK_OFFSET
    } else if (msgUpper.startsWith(SET_CLOCK_OFFSET_PREFIX)) {
        // Debug-only harness for issue #0008: shift nowMs() near the 2^32 ms
        // rollover so it can be exercised on the bench in seconds instead of
        // waited out over 49.7 days. Compiled out entirely in release builds
        // -- see the ENABLE_CLOCK_OFFSET block above nowMs(). Not part of
        // publishState()'s schema; it's a bench tool, not device state.
        //
        // Round 1 hard-coded this offset as msg.substring(18), which silently
        // ate the first digit of every documented bench command
        // (SET_CLOCK_OFFSET:4294900000 parsed as 294900000, landing ~46.3
        // days from the wrap instead of ~67s) with no error, no log, and no
        // visible symptom. Round 2 "fixed" it by hand-typing substring(17)
        // instead -- still just as capable of drifting from the literal the
        // next time this prefix is edited, papered over by a test that only
        // pinned strlen("SET_CLOCK_OFFSET:") against a copy of the same
        // literal declared in the test file (a tautology, not a guarantee).
        // This round makes the offset structurally impossible to get wrong:
        // it is derived from SET_CLOCK_OFFSET_PREFIX (shared with the native
        // test suite via time_utils.h) rather than typed as a number at all.
        // test_set_clock_offset_payload_parses_at_correct_offset in
        // test/test_rollover/test_rollover.cpp now pins the arithmetic
        // (sizeof(prefix) - 1 recovers the full value), not the handler
        // itself, which native tests cannot link against.
        String val = msg.substring(sizeof(SET_CLOCK_OFFSET_PREFIX) - 1);
        val.trim();

        bool validDigits = val.length() > 0;
        for (unsigned int i = 0; validDigits && i < val.length(); i++) {
            if (!isDigit(val[i])) validDigits = false;
        }

        if (!validDigits) {
            // strtoul() maps garbage to 0, which would silently reset the
            // offset with no other sign anything went wrong. Bench-tool
            // leniency (no rejection of a technically-out-of-range number)
            // is fine, but a non-numeric payload is a typo, not an offset,
            // so it's rejected and logged loudly rather than applied as 0.
            ESP_LOGW(TAG, "SET_CLOCK_OFFSET: ignoring non-numeric payload '%s'", val.c_str());
        } else {
            clockOffsetMs = (uint32_t)strtoul(val.c_str(), nullptr, 10);

            // Re-base the shifted deadlines onto the new clock. fx.blendTimeout/
            // fx.rotateTimeout were computed against the OLD nowMs(); jumping
            // clockOffsetMs by ~4.2949e9ms wraps them, in the signed
            // arithmetic timeReached() uses, into a deadline that reads as
            // ~67s in the future -- stranding the blend render for that whole
            // window and hiding the very rollover this harness exists to let
            // an operator watch happen live (the wrap itself occurs partway
            // through that frozen window, ~37s in). Resetting both to the
            // new nowMs() makes them fire on the very next loop iteration
            // instead of stalling.
            //
            // blendTimeout/rotateTimeout moved into EffectState (issue
            // #0014) -- this handler is compiled only under
            // ENABLE_CLOCK_OFFSET ([env:esp32c3-debug]), which is exactly
            // the by-name rebase the #0014 plan flagged as needing an
            // update here.
            fx.blendTimeout = nowMs();
            fx.rotateTimeout = nowMs();
            // issue #0015: the seven new renderers tick off one shared
            // effectTimeout deadline via nowMs() -- it needs the same
            // rebase as blendTimeout/rotateTimeout above, or a chase/strobe
            // in flight strands here exactly like #0008's round-2 defect.
            fx.effectTimeout = nowMs();
            // issue #0017: customEffect.activatedAt is another millis()-
            // domain deadline base and needs the identical rebase, or a
            // live custom-effect timeout strands exactly like the two
            // above -- this is #0008's round-2 defect, verbatim, applied to
            // a field #0016 added after that defect was fixed. Rebasing
            // restarts the countdown from full duration, which is correct
            // and intentional here: an 8-hour timeout restarting when the
            // bench clock jumps is a property of this debug-only bench
            // harness, not a bug -- ENABLE_CLOCK_OFFSET never compiles into
            // the release build (see the harness comment above nowMs()).
            customEffect.activatedAt = nowMs();
            // issue #0017 half B: a pending resume decision's grace window
            // is also millis()-domain and needs the same treatment, or the
            // jump can make the grace deadline appear to have already
            // passed (or be absurdly far away), corrupting the boot resume
            // decision on this bench build.
            if (effectResumePending) effectResumeGraceUntil = nowMs() + RESUME_GRACE_MS;

            // The ~67s bench runway documented for this harness (issue
            // #0008) only holds if the command lands within seconds of
            // boot. In practice (flash -> reboot -> WiFi -> MQTT connect ->
            // operator types the command), millis() at command time is
            // often well past that, and this same
            // SET_CLOCK_OFFSET:4294900000 then leaves nowMs() ~49.7 days
            // from the wrap instead of ~67s -- the wrap gets jumped over,
            // never crossed, and everything downstream (strip animates, log
            // line looks sane, parsed offset matches what was sent) still
            // LOOKS like a pass. Logging the actual distance to the wrap
            // directly, rather than trusting the ~67s assumption, is the
            // only way an operator running the bench procedure can tell
            // those two cases apart: this prints ~62000 in the good case
            // and ~4294900000 in the bad one.
            uint32_t wrapRunwayMs = 0u - nowMs();
            ESP_LOGI(TAG, "SET_CLOCK_OFFSET: payload='%s' -> clockOffsetMs=%lu ms "
                     "(nowMs() now reads %lu, millis() reads %lu, wrap in %lu ms)",
                     val.c_str(), (unsigned long)clockOffsetMs, (unsigned long)nowMs(),
                     (unsigned long)millis(), (unsigned long)wrapRunwayMs);
        }
#endif
    } else if (msgUpper.startsWith("OTA_URL:")) {
        // Use the original-case msg — URLs are case-sensitive and msgUpper is not.
        String url = msg.substring(8);
        url.trim();
        // Deliberately refused on lights/all/cmd: a stray broadcast here would
        // re-flash the entire fleet from one unverified URL at once.
        if (String(topic).indexOf("/all/") >= 0) {
            ESP_LOGW(TAG, "OTA: refusing OTA_URL on a broadcast topic (%s)", topic);
        } else if (!url.startsWith("http://") && !url.startsWith("https://")) {
            ESP_LOGW(TAG, "OTA: ignoring OTA_URL with unsupported scheme: '%s'", url.c_str());
        } else if (!isLocalNetworkUrl(url)) {
            ESP_LOGW(TAG, "OTA: refusing OTA_URL outside the local network: %s", url.c_str());
        } else {
            ESP_LOGI(TAG, "OTA: manual URL update requested: %s", url.c_str());
            performOtaFromUrl(url);
        }
    } else if (msgUpper.startsWith("OTA_AUTO:")) {
        String val = msgUpper.substring(9);
        val.toLowerCase();
        otaAutoEnabled = (val == "true" || val == "1");
        saveOtaAutoToPreferences();
        ESP_LOGI(TAG, "OTA auto-update set to: %s", otaAutoEnabled ? "true" : "false");
        publishState();
    } else if (msgUpper.startsWith(SET_EFFECT_PREFIX)) {
        // Issue #0016. No topic restriction -- unlike OTA_URL/RESTART, an
        // ad hoc effect on lights/all/cmd is a normal thing to want and
        // trivially undone with CLEAR_EFFECT.
        //
        // The parser writes into a scratch struct, never the live
        // customEffect, so a rejected parse structurally cannot leave a
        // half-applied config -- see effect_parse.h's header comment. The
        // prefix offset is derived (sizeof(...) - 1), never hand-typed --
        // the exact defect that cost issue #0008 two rounds. Original-case
        // msg is parsed and logged; only the startsWith() match above uses
        // msgUpper, so a normalization step the native tests don't exercise
        // never reaches the device-only path.
        CustomEffectConfig parsed;
        const char* body = msg.c_str() + (sizeof(SET_EFFECT_PREFIX) - 1);
        EffectParseResult r = parseEffectPayload(body, parsed);
        if (r != EFFECT_PARSE_OK) {
            // ESP_LOGE, not LOGW/LOGI -- [env:esp32c3] builds at
            // CORE_DEBUG_LEVEL=1, where only ESP_LOGE survives. A rejection
            // logged at a lower level would be invisible on every device
            // that ever runs this in the field. Truncated to 160 chars: a
            // full legal payload is ~166 bytes, so a rejected one is bounded
            // and readable without a pathological payload flooding the log.
            // Nothing else happens: no publishState(), no NVS write, no
            // currentTheme change -- the device stays exactly where it was.
            ESP_LOGE(TAG, "SET_EFFECT rejected (%s): '%.160s'", effectParseResultName(r), msg.c_str());
        } else {
            // The prevRevert ternary is the whole point of issue #0016's
            // cancel-path item 4: two back-to-back SET_EFFECTs must not make
            // the second one's revert target the first effect. Captured
            // BEFORE `customEffect = parsed` overwrites revertTheme (with
            // resetCustomEffect()'s THEME_GREEN via parseEffectPayload()).
            HueTheme prevRevert = customEffect.revertTheme;
            customEffect = parsed;
            customEffect.revertTheme = (currentTheme != THEME_CUSTOM) ? currentTheme : prevRevert;
            customEffect.activatedAt = nowMs(); // nowMs(), not millis() -- see the ENABLE_CLOCK_OFFSET harness above
            currentTheme = THEME_CUSTOM;
            hueIndex = 0; // the previous theme may have had 8 hues, the new effect fewer
            refreshCustomPalette();
            if (!ledsEnabled) ledsEnabled = true; // mirrors the theme-string handler below

            // Issue #0017 half B: seed the wall-clock end time so a reboot
            // can resolve this effect correctly. Set while the clock is
            // unsynced -> effectEndEpoch stays 0, which is deliberately
            // NOT resumable: without a wall clock there is no honest
            // remainder to compute, so it reverts on boot instead (falls
            // out of effectResumeDecision()'s REVERT_NO_END row for free).
            // A fresh SET_EFFECT also self-cancels any pending boot-resume
            // decision -- this is a brand new effect, not a continuation of
            // whatever was pending.
            effectResumePending = false;
            uint32_t epochNow = 0;
            effectEndEpoch = (customEffect.timeoutMs && clockEpochNow(epochNow))
                             ? epochNow + customEffect.timeoutMs / 1000UL : 0;
            if (customEffect.timeoutMs && effectEndEpoch == 0) {
                ESP_LOGI(TAG, "SET_EFFECT: clock not synced, timeout will not survive a reboot");
            }

            saveThemeToPreferences();
            saveCustomEffectToPreferences();

            // Force immediate visual update -- parity with the theme-switch
            // paths (switchToNextTheme(), the theme-string dispatch below).
            const PaletteColor* currentColors = getCurrentColorArray();
            PaletteColor newColor = currentColors[hueIndex];
            fill_solid(leds, numLeds, CHSV(newColor.h, newColor.s, newColor.v));
            FastLED.show();

            ESP_LOGI(TAG, "SET_EFFECT applied: mode=%s colors=%u speed=%u intensity=%u timeoutSec=%lu",
                     EFFECT_MODE_NAMES[customEffect.mode], customEffect.colorCount,
                     customEffect.speed, customEffect.intensity,
                     (unsigned long)(customEffect.timeoutMs / 1000UL));
            publishState();
        }
    } else if (msgUpper == "CLEAR_EFFECT") {
        // Issue #0016. Exact match, not a prefix -- placed here (rather
        // than the trailing exact-match block below) purely because it's
        // adjacent to SET_EFFECT; it behaves like the other exact-match
        // commands otherwise. Idempotent no-op when not currently on
        // THEME_CUSTOM. Deliberately NO /all/ check, unlike OTA_URL/RESTART:
        // the worst case is the whole installation returning to its normal
        // theme, which is the recovery action anyway (issue #0013).
        //
        // Issue #0017: shares revertFromCustomEffect() with the live
        // timeout expiry (loopEffectTimeout()) rather than duplicating the
        // revert logic -- see that function's comment for why.
        if (currentTheme == THEME_CUSTOM) {
            revertFromCustomEffect();
            ESP_LOGI(TAG, "CLEAR_EFFECT: reverted to %s", THEME_NAMES[currentTheme]);
        }
    } else {
        HueTheme newTheme = currentTheme;
        bool themeIdentified = false;

        if (msgUpper == "GREEN" || msgUpper == "THEME_GREEN" || msgUpper == "SCENE_1") {
            newTheme = THEME_GREEN;
            themeIdentified = true;
        } else if (msgUpper == "RAINBOW" || msgUpper == "THEME_RAINBOW" || msgUpper == "SCENE_2") {
            newTheme = THEME_RAINBOW;
            themeIdentified = true;
        } else if (msgUpper == "PINK_PONY" || msgUpper == "THEME_PINK_PONY" || msgUpper == "PINK_PONY_CLUB") {
            newTheme = THEME_PINK_PONY;
            themeIdentified = true;
        } else if (msgUpper == "OCEAN_WAVES" || msgUpper == "THEME_OCEAN_WAVES" || msgUpper == "OCEAN") {
            newTheme = THEME_OCEAN_WAVES;
            themeIdentified = true;
        } else if (msgUpper == "SUNSET" || msgUpper == "THEME_SUNSET") {
            newTheme = THEME_SUNSET;
            themeIdentified = true;
        } else if (msgUpper == "FOREST" || msgUpper == "THEME_FOREST") {
            newTheme = THEME_FOREST;
            themeIdentified = true;
        }

        // Issue #0021: an active custom effect outranks a theme command that
        // arrives on the broadcast topic. The fleet's broker carries a RETAINED
        // theme on lights/all/cmd (a `FOREST` put there by a Home Assistant
        // automation), and MQTT redelivers a retained message on every
        // reconnect -- so any custom effect was silently cancelled seconds
        // later by a message nobody sent, and the device could never hold an
        // effect across a WiFi blip. The effect and its expiry are device-owned
        // state; a broadcast aimed at the whole fleet is not a reason to
        // discard them.
        //
        // Scoped to broadcasts on purpose. A theme sent to THIS device's own
        // topic is someone deliberately taking control of this strip and still
        // wins immediately -- that, and CLEAR_EFFECT, are how a person overrides
        // a running effect. Same reasoning as the OTA_URL broadcast refusal
        // above: /all/ is for the fleet, the device topic is for the device.
        if (themeIdentified &&
            shouldIgnoreBroadcastTheme(topic, currentTheme == THEME_CUSTOM)) {
            ESP_LOGI(TAG, "Ignoring broadcast theme '%s' on %s: custom effect active "
                          "(mode=%s, %ld s remaining)",
                     msgUpper.c_str(), topic, EFFECT_MODE_NAMES[customEffect.mode],
                     (long)effectTimeoutRemainingSeconds(customEffect.timeoutMs,
                                                         customEffect.activatedAt, nowMs()));
            themeIdentified = false;
        }

        if (themeIdentified) {
            if (newTheme != currentTheme || !ledsEnabled) {
                // Issue #0016: an explicit theme command must cancel an
                // active custom effect's timeout, not leave it queued to
                // revert (to a theme the user has since moved away from)
                // later. newTheme != THEME_CUSTOM always holds here (no
                // theme string maps to THEME_CUSTOM), so this `if` is the
                // only thing gating the visual change already below --
                // what was missing before this ticket was only the timeout
                // teardown.
                if (currentTheme == THEME_CUSTOM) cancelCustomEffect();
                currentTheme = newTheme;
                hueIndex = 0;
                saveThemeToPreferences();
                if (!ledsEnabled) ledsEnabled = true; // Use direct flag to avoid toggle logic
                
                // Force immediate visual update
                const PaletteColor* currentColors = getCurrentColorArray();
                PaletteColor newColor = currentColors[hueIndex];
                fill_solid(leds, numLeds, CHSV(newColor.h, newColor.s, newColor.v));
                FastLED.show();
                publishState();
            }
            return;
        }

        if (msgUpper == "TOGGLE") {
            toggleAllLEDs();
            publishState();
        } else if (msgUpper == "ON") {
            if (!ledsEnabled) {
                toggleAllLEDs();
                publishState();
            }
        } else if (msgUpper == "OFF") {
            if (ledsEnabled) {
                toggleAllLEDs();
                publishState();
            }
        } else if (msgUpper == "BRIGHT_UP") {
            increaseBrightness();
            publishState();
        } else if (msgUpper == "BRIGHT_DOWN") {
            decreaseBrightness();
            publishState();
        } else if (msgUpper == "COLOR_CHANGE_ON") {
            if (!colorChangeEnabled) {
                colorChangeEnabled = true;
                publishState();
            }
        } else if (msgUpper == "COLOR_CHANGE_OFF") {
            if (colorChangeEnabled) {
                colorChangeEnabled = false;
                publishState();
            }
        } else if (msgUpper == "MIRROR_ON") {
            if (!mirrorEnabled) {
                mirrorEnabled = true;
                saveMirrorToPreferences();
                publishState();
            }
        } else if (msgUpper == "MIRROR_OFF") {
            if (mirrorEnabled) {
                mirrorEnabled = false;
                saveMirrorToPreferences();
                publishState();
            }
        } else if (msgUpper == "TOGGLE_MIRROR") {
            mirrorEnabled = !mirrorEnabled;
            saveMirrorToPreferences();
            publishState();
        } else if (msgUpper == "OTA_UPDATE" || msgUpper == "OTA_CHECK") {
            checkForOtaUpdate(true);
        }
    }
}

void ensureWifi() {
    if (WiFi.status() == WL_CONNECTED) return;

    ESP_LOGI(TAG, "WiFi connecting to %s...", WIFI_SSID);

    // eero mesh network compatibility fixes
    WiFi.mode(WIFI_STA);  // Initialize mode FIRST before other settings

    // Lower transmit power to prevent overloading eero receivers
    if (WiFi.setTxPower(WIFI_POWER_8_5dBm)) {
        ESP_LOGI(TAG, "Set transmit power to 8.5dBm (eero compatibility)");
    }

    // Disable 802.11n to avoid mesh interference - use b/g only
    esp_err_t result = esp_wifi_set_protocol(WIFI_IF_STA,
                                              WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G);
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Set WiFi protocol to 802.11b/g (disabled 802.11n)");
    }

    WiFi.begin(WIFI_SSID, WIFI_PASS);

    // Quick connect attempt (non-blocking in loop, but here we wait a bit in setup or first run)
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) {  // 30 second timeout
        delay(100);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        ESP_LOGI(TAG, "WiFi connected, IP: %s", WiFi.localIP().toString().c_str());
    } else {
        ESP_LOGW(TAG, "WiFi connection failed");
    }
}

void ensureMqtt() {
    if (mqtt.connected()) {
        mqtt.loop();
        return;
    }

    if (WiFi.status() != WL_CONNECTED) return;

    uint32_t now = millis();
    if (!timeReached(now, mqttRetryAt)) return;
    mqttRetryAt = now + MQTT_RETRY_MS;

    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setCallback(onMqttMessage);
    // Issue #0016: PubSubClient defaults to a 256-byte MQTT_MAX_PACKET_SIZE
    // for the WHOLE packet, and silently drops (never invokes the callback,
    // never logs) anything over that -- an 8-color SET_EFFECT with a
    // realistic topic name is ~189 of those 256 bytes already (see
    // effect_parse.h's wire-size test), and STATUS/SET_DEVICE_NAME payloads
    // grow over time too. setBufferSize() must be called every time this
    // function actually (re)connects -- it does not persist across a
    // PubSubClient reconnect the way setServer()/setCallback() are already
    // re-applied here on each attempt.
    mqtt.setBufferSize(512);

    String clientId = "esp32c3-glowkitchen-" + String(getDeviceName()) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);

    ESP_LOGI(TAG, "MQTT connecting to %s...", MQTT_HOST);
    if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
        ESP_LOGI(TAG, "MQTT connected; subscribing...");
        mqtt.subscribe("lights/all/cmd");
        
        // Always subscribe to hardware-based ID topic
        String idTopic = "lights/" + getDeviceId() + "/cmd";
        mqtt.subscribe(idTopic.c_str());
        ESP_LOGI(TAG, "Subscribed to %s", idTopic.c_str());
        
        // Also subscribe to name-based topic if a name is set and it's different from the ID
        if (DEVICE_NAME.length() > 0 && DEVICE_NAME != getDeviceId()) {
            String nameTopic = "lights/" + DEVICE_NAME + "/cmd";
            mqtt.subscribe(nameTopic.c_str());
            ESP_LOGI(TAG, "Subscribed to name-based topic: %s", nameTopic.c_str());
        }

        // Report initial state
        publishState();
    } else {
        ESP_LOGW(TAG, "MQTT connect failed, rc=%d", mqtt.state());
    }
}

// Function to identify button from IR data
GrayRemoteButton identifyGrayRemoteButton(uint8_t address, uint8_t command) {
    if (address != NEC_REMOTE_ADDRESS) {
        return NEC_UNKNOWN;
    }
    
    switch (command) {
        case 0x09: return NEC_VOL_PLUS;
        case 0x15: return NEC_VOL_MINUS;
        case 0x40: return NEC_REWIND;
        case 0x43: return NEC_FORWARD;
        case 0x44: return NEC_PLAY_PAUSE;
        case 0x46: return NEC_MODE;
        case 0x45: return NEC_POWER;
        case 0x47: return NEC_MUTE;
        case 0x07: return NEC_EQ;
        case 0x19: return NEC_RPT;
        case 0x0D: return NEC_USD;
        case 0x16: return NEC_0;
        case 0x0C: return NEC_1;
        case 0x18: return NEC_2;
        case 0x5E: return NEC_3;
        case 0x08: return NEC_4;
        case 0x1C: return NEC_5;
        case 0x5A: return NEC_6;
        case 0x42: return NEC_7;
        case 0x52: return NEC_8;
        case 0x4A: return NEC_9;
        default: return NEC_UNKNOWN;
    }
}

// flickerLEDs()/slowBlend() (issue #0014): the render bodies moved to
// renderFlicker()/renderBlend() in src/effects.h, so the native frame-diff
// test (test/test_effects/) can drive them against a stub leds[] buffer.
// loopLED() below now dispatches on EffectMode via getCurrentEffectMode()
// instead of `if (currentTheme == THEME_GREEN)`.

// Mirror the first half of the strip onto the second half in reverse (palindrome effect)
void applyMirror() {
    int half = numLeds / 2;
    for (int i = 0; i < half; i++) {
        leds[numLeds - 1 - i] = leds[i];
    }
    FastLED.show();
}

void setupLED() {
    // Load all saved preferences (theme, brightness, hue, led config)
    loadAllPreferences();

    // Issue #0017: the DEV_FORCE_EFFECT bench hook (issue #0015 section 11)
    // used to run here, forcing THEME_CUSTOM with a hardcoded CHASE effect
    // at every boot. It existed only because nothing could set THEME_CUSTOM
    // over MQTT until issue #0016 landed SET_EFFECT, which supersedes it.
    // Removed here because it was actively harmful: it ran AFTER
    // loadAllPreferences() and unconditionally overwrote whatever effect
    // had just been restored from NVS, so every esp32c3-debug build
    // restored the WRONG effect after a power cycle -- confirmed on
    // hardware during issue #0016. See platformio.ini for the matching
    // -DDEV_FORCE_EFFECT removal.

    // Initialize with MAX_LEDS so we can change numLeds at runtime without re-initializing
    FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, MAX_LEDS);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 500); // volts, mA

    ESP_LOGI(TAG, "LED setup complete with %d LEDs (max %d)", numLeds, MAX_LEDS);

    unsigned long now = millis();
    for (int i = 0; i < MAX_LEDS; i++) {
      timeouts[i] = now;
    }
    // fx.blendTimeout/fx.rotateTimeout/fx.blendOffset are left at their
    // zero-initialized defaults here, matching the exact pre-#0014 behavior
    // (blendTimeout/rotateTimeout/blendOffset were never touched in
    // setupLED() before this refactor either -- only hueTimeout and
    // timeouts[] were seeded).
    fx.timeouts = timeouts;
    fx.sparkleVal = sparkleValues;  // issue #0015: SPARKLE's per-LED brightness array
    fx.hueTimeout = now + 2000;
    fx.rng = arduinoRandomRange;
    logTimeout = now + logInterval;
}

void setupIR() {    
    // Initialize IR receiver
    IrReceiver.begin(irReceiverPin, DISABLE_LED_FEEDBACK);
    
    ESP_LOGI(TAG, "IR receiver initialized on pin %d", irReceiverPin);
    ESP_LOGI(TAG, "IR receiver ready: %s", IrReceiver.isIdle() ? "true" : "false");
}

// Issue #0017: acts on the timeout issue #0016 parses, clamps and persists.
// Called from loop(), BEFORE loopLED() (so a revert takes visual effect on
// the same iteration it fires, not one frame later) and NOT from inside
// loopLED() (which returns early when !ledsEnabled -- issue #0016 section
// 6.3 deliberately makes OFF/TOGGLE NOT cancel a timed effect, so a timed
// effect must still be able to expire while the strip is off, or turning
// the lights off parks a live timeout indefinitely and it fires late
// whenever they come back on). Running from loop() also means this keeps
// working with WiFi down and MQTT disconnected -- loopOta() would not,
// since it returns early on WiFi.status() != WL_CONNECTED.
void loopEffectTimeout() {
    // Half B: resolve the deferred boot-time resume decision first, before
    // any live-expiry check. See the boot-arm comment in
    // loadAllPreferences() for why this can't just run once at boot.
    if (effectResumePending) {
        uint32_t epoch;
        if (clockEpochNow(epoch)) {
            effectResumePending = false;
            if (effectEndEpoch == 0 || epoch >= effectEndEpoch) {
                ESP_LOGI(TAG, "Custom effect expired while powered down; reverting");
                revertFromCustomEffect();
            } else {
                customEffect.timeoutMs   = resumeTimeoutMs(epoch, effectEndEpoch);
                customEffect.activatedAt = nowMs();
                ESP_LOGI(TAG, "Custom effect resumed, %lu s remaining",
                         (unsigned long)(customEffect.timeoutMs / 1000UL));
            }
        } else if (timeReached(nowMs(), effectResumeGraceUntil)) {
            effectResumePending = false;
            ESP_LOGI(TAG, "Clock never synced within grace window; reverting custom effect");
            revertFromCustomEffect();
        }
        // The resume decision writes no NVS -- it adjusts the in-RAM
        // activatedAt/timeoutMs only. A second reboot must re-derive from
        // the unchanged fx_end, not from progressively adjusted in-RAM
        // state, and this also avoids a flash write on every boot
        // (issues/0017.md section 6.3). Unconditional return: the live
        // expiry check below must never run against this half-seeded state
        // in the same iteration a resume decision resolves.
        return;
    }

    if (!effectTimeoutExpired(currentTheme == THEME_CUSTOM, customEffect.timeoutMs,
                               customEffect.activatedAt, nowMs())) return;
    ESP_LOGI(TAG, "Custom effect timed out after %lu s, reverting to %s",
             (unsigned long)(customEffect.timeoutMs / 1000UL), THEME_NAMES[customEffect.revertTheme]);
    revertFromCustomEffect();
}

void loopLED() {
    unsigned long now = millis();

    if (timeReached(now, logTimeout)) {
        ESP_LOGI(TAG, "Heartbeat - uptime: %lu ms, theme: %s, hue index: %d, LEDs: %s", 
                 now, THEME_NAMES[currentTheme], hueIndex, ledsEnabled ? "ON" : "OFF");
        logTimeout = now + logInterval;
    }
    
    // If LEDs are disabled, ensure they stay black and exit early
    if (!ledsEnabled) {
        // Force all LEDs to black and show immediately
        static unsigned long lastBlackUpdate = 0;
        if (now - lastBlackUpdate > 100) { // Update every 100ms to ensure they stay black
            fill_solid(leds, numLeds, CRGB::Black);
            FastLED.show();
            lastBlackUpdate = now;
        }
        return; // Exit early, don't do any other LED processing
    }

    // Dispatch on EffectMode rather than theme (issue #0014). `default ->
    // renderBlend` is load-bearing for any future mode value this switch
    // doesn't otherwise name -- the strip blends rather than going dark or
    // falling off the end of a jump table.
    EffectMode mode = getCurrentEffectMode();

    // Per-frame speed/intensity inputs (issue #0015). Built-ins (BLEND/
    // FLICKER) ignore both -- passing 128/128 is a definiteness choice, not
    // a behavior one, and it means a future built-in mapped onto one of the
    // seven new modes gets sane defaults for free.
    const uint8_t EFFECT_DEFAULT_PARAM = 128;
    fx.speed     = (currentTheme == THEME_CUSTOM) ? customEffect.speed     : EFFECT_DEFAULT_PARAM;
    fx.intensity = (currentTheme == THEME_CUSTOM) ? customEffect.intensity : EFFECT_DEFAULT_PARAM;

    // Mode-change reset (issue #0015): only the #0015 fields, never
    // blendTimeout/rotateTimeout/hueTimeout/blendOffset/timeouts[] -- see
    // resetEffectState()'s comment in src/effects.h. fx.lastMode
    // zero-initializes to EFFECT_BLEND, so a device booting into Green
    // (FLICKER) fires exactly one reset here that touches nothing FLICKER
    // reads.
    if (mode != fx.lastMode) {
        resetEffectState(fx, nowMs());
        fx.lastMode = mode;
    }

    fx.leds = leds;
    fx.numLeds = numLeds;
    fx.ledsPerColor = ledsPerColor;
    fx.colors = getCurrentColorArray();
    fx.colorCount = getCurrentColorCount();
    fx.hueIndex = hueIndex;
    fx.colorChangeEnabled = colorChangeEnabled;
    fx.brightness = FastLED.getBrightness();
    fx.maxBrightness = MAX_BRIGHTNESS;

    bool drew;
    switch (mode) {
        case EFFECT_FLICKER:
            // Driven by millis(), not nowMs() -- as before this refactor.
            // Unifying it with the blend path would strand flicker's
            // timeouts[] under the SET_CLOCK_OFFSET bench harness, which
            // only rebases the two blend timers (issue #0008). Out of scope.
            drew = renderFlicker(fx, millis());
            break;
        case EFFECT_CHASE:
            drew = renderChase(fx, nowMs());
            break;
        case EFFECT_WIPE:
            drew = renderWipe(fx, nowMs());
            break;
        case EFFECT_SCAN:
            drew = renderScan(fx, nowMs());
            break;
        case EFFECT_SPARKLE:
            drew = renderSparkle(fx, nowMs());
            break;
        case EFFECT_PULSE:
            drew = renderPulse(fx, nowMs());
            break;
        case EFFECT_STROBE:
            drew = renderStrobe(fx, nowMs());
            break;
        case EFFECT_COLORLOOP:
            drew = renderColorloop(fx, nowMs());
            break;
        case EFFECT_BLEND:
        default:
            // Routed through nowMs(): blendTimeout/rotateTimeout are the two
            // timers that actually froze in issue #0008, so they're the ones
            // the clock-offset harness needs to move. In a release build
            // nowMs() is just millis() -- see its definition above.
            drew = renderBlend(fx, nowMs());
            break;
    }
    hueIndex = fx.hueIndex;

#ifdef EFFECT_TRACE
    // Render telemetry for bench verification (debug builds only).
    //
    // Exists because eyeballing a strip is a poor feedback loop: over one
    // session it produced three wrong diagnoses -- a "frozen" SPARKLE that was
    // really stale firmware, a "stuck" SCAN that was a frame caught during a
    // flash, and a colour conversion that reviewed clean numerically and
    // rendered white on actual LEDs. A log line carrying the renderer's own
    // state plus the pixels it just wrote makes all three checkable from a
    // serial capture instead of a person's eyes.
    //
    // Rate-limited rather than per-frame: at ~30 fps an unthrottled trace
    // floods the link and changes the timing it is meant to measure.
    // TRACE_LEDS caps the dump so a 240-LED strip cannot overrun the line.
    {
        static uint32_t traceDeadline = 0;
        const uint32_t traceNow = millis();
        if (drew && timeReached(traceNow, traceDeadline)) {
            traceDeadline = traceNow + 250;
            const int TRACE_LEDS = numLeds < 10 ? numLeds : 10;
            char px[128];
            int n = 0;
            for (int i = 0; i < TRACE_LEDS && n < (int)sizeof(px) - 12; i++) {
                n += snprintf(px + n, sizeof(px) - n, "%02x%02x%02x ",
                              leds[i].r, leds[i].g, leds[i].b);
            }
            ESP_LOGI(TAG, "[TRACE] mode=%d phase=%u dir=%d sub=%u leds=%d speed=%u int=%u bright=%u px=%s",
                     (int)mode, (unsigned)fx.effectPhase, (int)fx.effectDir,
                     (unsigned)fx.effectSub, numLeds, (unsigned)fx.speed,
                     (unsigned)fx.intensity, FastLED.getBrightness(), px);
        }
    }
#endif

    if (drew) {
        if (mirrorEnabled && mode != EFFECT_FLICKER) {
            applyMirror();
        } else {
            FastLED.show();
        }
    }
}

void loopIR() {
    if (!irEnabled) return;
    
    unsigned long now = millis();

    // Check if an IR signal is received
    if (IrReceiver.decode()) {
        if (IR_DEBUG_MODE) {
            ESP_LOGI(TAG, "*** IR Signal Detected! ***");
        }
        
        // Check if it's the NEC remote signal
        if (IrReceiver.decodedIRData.protocol == NEC && 
            IrReceiver.decodedIRData.address == NEC_REMOTE_ADDRESS) {
            
            // Identify and handle the button press
            GrayRemoteButton button = identifyGrayRemoteButton(
                IrReceiver.decodedIRData.address, 
                IrReceiver.decodedIRData.command
            );
            
            if (button != NEC_UNKNOWN) {
                // Add button name to the log for clarity
                const char* buttonName = "Unknown";
                switch (button) {
                    case NEC_VOL_PLUS: buttonName = "Vol+"; break;
                    case NEC_VOL_MINUS: buttonName = "Vol-"; break;
                    case NEC_REWIND: buttonName = "Rewind"; break;
                    case NEC_FORWARD: buttonName = "Forward"; break;
                    case NEC_PLAY_PAUSE: buttonName = "Play/Pause"; break;
                    case NEC_MODE: buttonName = "Mode"; break;
                    case NEC_POWER: buttonName = "Power"; break;
                    case NEC_MUTE: buttonName = "Mute"; break;
                    case NEC_EQ: buttonName = "EQ"; break;
                    case NEC_RPT: buttonName = "RPT"; break;
                    case NEC_USD: buttonName = "U/SD"; break;
                    case NEC_0: buttonName = "0"; break;
                    case NEC_1: buttonName = "1"; break;
                    case NEC_2: buttonName = "2"; break;
                    case NEC_3: buttonName = "3"; break;
                    case NEC_4: buttonName = "4"; break;
                    case NEC_5: buttonName = "5"; break;
                    case NEC_6: buttonName = "6"; break;
                    case NEC_7: buttonName = "7"; break;
                    case NEC_8: buttonName = "8"; break;
                    case NEC_9: buttonName = "9"; break;
                    default: buttonName = "Unknown"; break;
                }
                ESP_LOGI(TAG, "NEC Remote Button pressed: %s (Command 0x%X)", buttonName, IrReceiver.decodedIRData.command);
                handleGrayRemoteButton(button);
            } else {
                ESP_LOGI(TAG, "Unknown NEC button: Command 0x%X", IrReceiver.decodedIRData.command);
            }
        } else {
            // Log detailed information for non-NEC remote signals
            ESP_LOGI(TAG, "=== Non-NEC Remote IR Signal ===");
            
            // Check if protocol was successfully decoded
            if (IrReceiver.decodedIRData.protocol == UNKNOWN) {
                ESP_LOGI(TAG, "Protocol: UNKNOWN (library couldn't decode)");
                ESP_LOGI(TAG, "This might be an unsupported protocol");
            } else {
                ESP_LOGI(TAG, "Protocol: %s", getProtocolString(IrReceiver.decodedIRData.protocol));
            }
            
            ESP_LOGI(TAG, "Raw Data: 0x%lX", IrReceiver.decodedIRData.decodedRawData);
            ESP_LOGI(TAG, "Address: 0x%X", IrReceiver.decodedIRData.address);
            ESP_LOGI(TAG, "Command: 0x%X", IrReceiver.decodedIRData.command);
            ESP_LOGI(TAG, "Flags: 0x%X", IrReceiver.decodedIRData.flags);
            ESP_LOGI(TAG, "=============================");
        }
        
        // Resume receiver to listen for next signal
        IrReceiver.resume();
    }

    // Periodic heartbeat with IR receiver status
    if (timeReached(now, irLogTimeout)) {
        if (IR_DEBUG_MODE) {
            ESP_LOGI(TAG, "IR Heartbeat - uptime: %lu ms, receiver idle: %s, checking for signals...", 
                     now, IrReceiver.isIdle() ? "true" : "false");
        } else {
            ESP_LOGI(TAG, "IR Heartbeat - uptime: %lu ms, receiver idle: %s", 
                     now, IrReceiver.isIdle() ? "true" : "false");
        }
        irLogTimeout = now + irLogInterval;
    }
}

// ---- OTA ----

// OTA_URL applies an unsigned binary, so the blast radius has to stop at the LAN:
// anything that can reach the broker could otherwise point a strip at an internet
// URL and flash arbitrary firmware onto it. Accept only hosts we can show are
// local — an mDNS name, an address on this device's own subnet, or a private
// range for a dev machine on another VLAN. Bare DNS names are refused because
// resolving them to decide is exactly the lookup an attacker would control.
bool isLocalNetworkUrl(const String& url) {
    int schemeEnd = url.indexOf("://");
    if (schemeEnd < 0) return false;

    int hostStart = schemeEnd + 3;
    int hostEnd = url.length();
    for (int i = hostStart; i < (int)url.length(); i++) {
        char c = url[i];
        if (c == '/' || c == ':') { hostEnd = i; break; }
    }
    String host = url.substring(hostStart, hostEnd);
    if (host.length() == 0) return false;

    // mDNS names are link-local by definition (e.g. mymac.local)
    String lower = host;
    lower.toLowerCase();
    if (lower.endsWith(".local")) return true;

    IPAddress ip;
    if (!ip.fromString(host)) return false;   // not an IP literal, and not .local

    // Same subnet as this device is the tightest definition of "local network".
    // An all-zero mask (no DHCP lease yet) would match every address, so treat
    // it as unusable rather than as a wildcard that accepts the whole internet.
    IPAddress self = WiFi.localIP();
    IPAddress mask = WiFi.subnetMask();
    if (mask[0] | mask[1] | mask[2] | mask[3]) {
        bool sameSubnet = true;
        for (int i = 0; i < 4; i++) {
            if ((ip[i] & mask[i]) != (self[i] & mask[i])) { sameSubnet = false; break; }
        }
        if (sameSubnet) return true;
    }

    // Otherwise accept the private / loopback / link-local ranges, so a build
    // server on another VLAN still works without opening this to the internet.
    if (ip[0] == 10) return true;
    if (ip[0] == 192 && ip[1] == 168) return true;
    if (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) return true;
    if (ip[0] == 127) return true;
    if (ip[0] == 169 && ip[1] == 254) return true;
    return false;
}

// Download and flash whatever is at `url`, unconditionally — the caller decides
// whether an update is warranted. Shared by the GitHub release path and the
// operator-issued OTA_URL command so both get the same progress logging and the
// same controlled reboot. Returns only on failure; success reboots.
void performOtaFromUrl(const String& url) {
    ESP_LOGI(TAG, "OTA: downloading firmware from %s", url.c_str());

    // A LAN dev server is plain http, which needs no TLS stack at all. Picking
    // the client by scheme means a local push does no certificate validation --
    // deliberately, because this is the recovery path if a pinned root above
    // ever stops matching what GitHub presents.
    bool secure = url.startsWith("https://");
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    if (secure) {
        secureClient.setCACert(GITHUB_ROOT_CAS);
    }
    WiFiClient& client = secure ? (WiFiClient&)secureClient : plainClient;

    // Progress visibility during the ~1 MB write (otherwise a silent 10-30s gap)
    httpUpdate.onStart([]() {
        ESP_LOGI(TAG, "OTA: download/flash started");
    });
    httpUpdate.onProgress([](int done, int total) {
        ESP_LOGI(TAG, "OTA: progress %d%% (%d/%d bytes)",
                 total ? (done * 100 / total) : 0, done, total);
    });
    httpUpdate.onError([](int err) {
        ESP_LOGE(TAG, "OTA: error %d: %s", err, httpUpdate.getLastErrorString().c_str());
    });

    // Take control of the reboot so we can log + flush UART before resetting
    httpUpdate.rebootOnUpdate(false);
    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    t_httpUpdate_return ret = httpUpdate.update(client, url);

    switch (ret) {
        case HTTP_UPDATE_OK:
            ESP_LOGI(TAG, "OTA: update written, rebooting into new firmware now");
            Serial.flush();      // ensure the line is sent over UART before reset
            delay(100);
            ESP.restart();
            break;
        case HTTP_UPDATE_NO_UPDATES:
            ESP_LOGI(TAG, "OTA: server says no update needed");
            break;
        case HTTP_UPDATE_FAILED:
            ESP_LOGE(TAG, "OTA: update failed, error=%d: %s",
                     httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
            break;
    }
}

void checkForOtaUpdate(bool manual) {
    ESP_LOGI(TAG, "OTA: checking for update (manual=%s, current=%s)", manual ? "true" : "false", FIRMWARE_VERSION);

    WiFiClientSecure client;
    client.setCACert(GITHUB_ROOT_CAS);

    // Fetch latest release tag from GitHub API
    HTTPClient http;
    http.begin(client, "https://api.github.com/repos/brennanMKE/GlowKitchen/releases/latest");
    http.addHeader("User-Agent", "GlowKitchen-OTA");
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        ESP_LOGW(TAG, "OTA: GitHub API returned HTTP %d", code);
        http.end();
        return;
    }

    // Lightweight tag_name extraction — no ArduinoJson dependency
    String body = http.getString();
    http.end();

    int idx = body.indexOf("\"tag_name\"");
    if (idx < 0) {
        ESP_LOGW(TAG, "OTA: tag_name not found in response");
        return;
    }
    int q1 = body.indexOf('"', idx + 10);  // opening quote of value
    int q2 = body.indexOf('"', q1 + 1);    // closing quote
    if (q1 < 0 || q2 < 0) {
        ESP_LOGW(TAG, "OTA: could not parse tag_name value");
        return;
    }
    String tagName = body.substring(q1 + 1, q2);
    // Strip leading 'v' if present
    if (tagName.length() > 0 && tagName[0] == 'v') {
        tagName = tagName.substring(1);
    }
    ESP_LOGI(TAG, "OTA: latest tag=%s, running=%s", tagName.c_str(), FIRMWARE_VERSION);

    if (tagName == String(FIRMWARE_VERSION)) {
        ESP_LOGI(TAG, "OTA: already up to date");
        return;
    }

    ESP_LOGI(TAG, "OTA: update available, downloading firmware...");

    performOtaFromUrl("https://github.com/brennanMKE/GlowKitchen/releases/latest/download/firmware.bin");
}

void loopOta() {
    if (WiFi.status() != WL_CONNECTED) return;

    // Configure NTP once after first WiFi connect
    static bool timeConfigured = false;
    static uint32_t wifiConnectedAt = 0;
    if (!timeConfigured) {
        // America/Los_Angeles. The DST rule is the same US one either way; only
        // the base offset differs, which is why the previous CST6CDT ran the
        // "03:00" check at 01:00 local — see issue #0005.
        configTzTime("PST8PDT,M3.2.0,M11.1.0/2", "pool.ntp.org", "time.nist.gov");
        timeConfigured = true;
        wifiConnectedAt = millis();
        ESP_LOGI(TAG, "OTA: NTP time sync configured");
    }

    // One-time startup check, a short delay after joining WiFi. Handy for local
    // testing — does not depend on NTP (the check queries the GitHub API directly).
    static const uint32_t STARTUP_OTA_DELAY_MS = 15000;  // adjust as needed
    static bool startupCheckDone = false;
    if (!startupCheckDone && (millis() - wifiConnectedAt) >= STARTUP_OTA_DELAY_MS) {
        startupCheckDone = true;
        if (!otaAutoEnabled) {
            ESP_LOGI(TAG, "OTA: startup check skipped (auto-update disabled)");
        } else {
            ESP_LOGI(TAG, "OTA: startup check (%us after WiFi join)", STARTUP_OTA_DELAY_MS / 1000);
            checkForOtaUpdate(true);
        }
        return;
    }

    // A device pinned to a local dev build takes no automatic updates at all;
    // OTA_UPDATE and OTA_URL still work, so it stays reachable.
    if (!otaAutoEnabled) return;

    // Daily scheduler. Deliberately mid-afternoon rather than overnight: an
    // update that goes wrong reboots the strips, and 15:00 is when someone is
    // awake to notice and recover. There is no canary or rollback yet (#0001,
    // #0006), so the time of day is the only safety margin there is.
    static const int OTA_CHECK_HOUR = 15;

    struct tm now;
    if (!getLocalTime(&now, 0)) return;            // not synced yet
    if (now.tm_year + 1900 < 2020) return;         // clock not valid

    if (now.tm_hour != OTA_CHECK_HOUR) return;     // only in that one hour

    // Load last-run day from Preferences and skip if already ran today
    preferences.begin("glow_kitchen", true);
    int lastYday = preferences.getInt("ota_last_yday", -1);
    preferences.end();
    if (lastYday == now.tm_yday) return;           // already ran today

    // Persist the day BEFORE checking: a successful update reboots from inside
    // checkForOtaUpdate() and never returns, so marking it first keeps the
    // once-per-day guard intact across the post-update reboot near 03:00.
    preferences.begin("glow_kitchen", false);
    preferences.putInt("ota_last_yday", now.tm_yday);
    preferences.end();

    ESP_LOGI(TAG, "OTA: nightly check triggered (yday=%d)", now.tm_yday);
    checkForOtaUpdate(false);
}

// ---- Setup / Loop ----

void setup() {
    Serial.begin(115200);
    // Issue #0020: the console is the C3's native USB CDC (ARDUINO_USB_CDC_ON_BOOT),
    // and HWCDC::write() blocks up to tx_timeout_ms whenever isPlugged() is true --
    // which reports whether the CABLE is enumerated, not whether anything is reading
    // the port. A plugged-in board with no monitor attached therefore blocks on every
    // log write, stalling loop() and with it MQTT and the effect timeout. Zero makes
    // logging lossy instead of blocking, which is the right trade in both directions:
    // nobody listening means nobody misses the output.
    Serial.setTxTimeoutMs(0);

    fwVersionMarkerAnchor = FW_VERSION_MARKER;  // pin the version marker into the binary
    ESP_LOGI(TAG, "Booted firmware %s", FIRMWARE_VERSION);

    setupLED();
    setupIR();

    ensureWifi();
}

void loop() {
    ensureWifi();
    ensureMqtt();

    loopEffectTimeout();  // issue #0017 -- see the placement rationale above the function
    loopLED();
    loopIR();
    loopOta();
}
