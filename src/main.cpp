#include <Arduino.h>
#include <FastLED.h>
#include <Config.h>
#if ENABLE_IR
#include <IRremote.h>
#endif
#include <esp_log.h>
#include <Preferences.h>
#include <WiFi.h>
#include <PubSubClient.h>

static const char *TAG = "MAIN";

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
unsigned long timeouts[MAX_LEDS];

// Theme System
enum HueTheme {
    THEME_GREEN = 0,
    THEME_RAINBOW = 1,
    THEME_PINK_PONY = 2,
    THEME_OCEAN = 3,
    THEME_SUNSET = 4,
    THEME_FOREST = 5,
    THEME_COUNT = 6
};

const char* THEME_NAMES[] = {
    "Green",
    "Rainbow", 
    "Pink Pony Club",
    "Ocean Waves",
    "Sunset",
    "Forest"
};

// Green Theme - Original candle-like greens
const uint8_t GREEN_HUES[] = {
  85,  // dark green
  90,  // darker medium green
  95,  // medium green
  100, // medium-light green
  105, // light green
  100, // medium-light green (back down)
  95,  // medium green
  90   // darker medium green (back to start)
};

// Rainbow Theme - Full spectrum cycling
const uint8_t RAINBOW_HUES[] = {
  0,   // red
  32,  // orange
  64,  // yellow
  96,  // green
  128, // cyan
  160, // blue
  192, // purple
  224  // magenta
};

// Pink Pony Club Theme - Pink, magenta, and pony colors
const uint8_t PINK_PONY_HUES[] = {
  200, // deep magenta
  210, // bright magenta
  220, // hot pink
  230, // light pink
  240, // pink-red
  245, // cotton candy pink
  250, // bubble gum pink
  255  // soft pink
};

// Halloween Theme - Orange and purple
const uint8_t HALLOWEEN_HUES[] = {
  16,  // deep orange
  20,  // orange
  24,  // bright orange
  28,  // yellow-orange
  200, // deep purple
  208, // purple
  216, // bright purple
  224  // magenta-purple
};

// Christmas Theme - Red and green shades only
 const uint8_t CHRISTMAS_HUES[] = {
   0,   // pure red
   3,   // red-red
   8,   // red-orange (still red)
   15,  // darker red-orange
   80,  // dark green
   85,  // medium-dark green
   90,  // medium green
   96   // light green (max before blue shift)
 };

 const int NUM_GREEN_HUES = sizeof(GREEN_HUES) / sizeof(GREEN_HUES[0]);
 const int NUM_RAINBOW_HUES = sizeof(RAINBOW_HUES) / sizeof(RAINBOW_HUES[0]);
 const int NUM_HALLOWEEN_HUES = sizeof(HALLOWEEN_HUES) / sizeof(HALLOWEEN_HUES[0]);
 const int NUM_CHRISTMAS_HUES = sizeof(CHRISTMAS_HUES) / sizeof(CHRISTMAS_HUES[0]);
 const int NUM_PINK_PONY_HUES = sizeof(PINK_PONY_HUES) / sizeof(PINK_PONY_HUES[0]);
 const int NUM_OCEAN_HUES = sizeof(OCEAN_HUES) / sizeof(OCEAN_HUES[0]);
 const int NUM_SUNSET_HUES = sizeof(SUNSET_HUES) / sizeof(SUNSET_HUES[0]);
 const int NUM_FOREST_HUES = sizeof(FOREST_HUES) / sizeof(FOREST_HUES[0]);

// Theme management
HueTheme currentTheme = THEME_GREEN;
bool colorChangeEnabled = true;
bool ledsEnabled = true;  // Global LED state - true = on, false = off
int hueIndex = 0;
unsigned long hueTimeout = 0;
unsigned long logTimeout = 0;
unsigned long logInterval = 10000;

// slowBlend variables for gradient effects
unsigned long blendTimeout = 0;
unsigned long blendInterval = 100;  // Update blend every 100ms for slower, smoother transitions
uint8_t blendOffset = 0;           // Offset for rotating the gradient
uint8_t blendBrightness = MAX_BRIGHTNESS;     // Base brightness for blend mode

// NEC Remote Button Constants (Protocol: NEC, Address: 0x0)
#if ENABLE_IR
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
#endif

// Preferences for persistent storage
Preferences preferences;

/// IR
#if ENABLE_IR
const int irReceiverPin = 3;
unsigned long irLogTimeout = 0;
unsigned long irLogInterval = 10000;
const bool IR_DEBUG_MODE = true;  // Set to false to reduce logging
#endif

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
        case THEME_OCEAN: return "OCEAN_WAVES";
        case THEME_SUNSET: return "SUNSET";
        case THEME_FOREST: return "FOREST";
        default: return "GREEN";
    }
}

const uint8_t* getCurrentHueArray() {
    switch (currentTheme) {
        case THEME_GREEN: return GREEN_HUES;
        case THEME_RAINBOW: return RAINBOW_HUES;
        case THEME_PINK_PONY: return PINK_PONY_HUES;
        case THEME_OCEAN: return OCEAN_HUES;
        case THEME_SUNSET: return SUNSET_HUES;
        case THEME_FOREST: return FOREST_HUES;
        default: return GREEN_HUES;
    }
}

int getCurrentHueCount() {
    switch (currentTheme) {
        case THEME_GREEN: return NUM_GREEN_HUES;
        case THEME_RAINBOW: return NUM_RAINBOW_HUES;
        case THEME_PINK_PONY: return NUM_PINK_PONY_HUES;
        case THEME_OCEAN: return NUM_OCEAN_HUES;
        case THEME_SUNSET: return NUM_SUNSET_HUES;
        case THEME_FOREST: return NUM_FOREST_HUES;
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

void loadAllPreferences() {
    preferences.begin("glow_kitchen", true);
    
    // Load LED count and color group size
    numLeds = preferences.getInt("num_leds", 240);
    ledsPerColor = preferences.getInt("leds_per_color", 25);
    
    // Safety check
    if (numLeds > MAX_LEDS) numLeds = MAX_LEDS;
    if (numLeds < 1) numLeds = 1;
    if (ledsPerColor < 1) ledsPerColor = 1;

    // Check for development theme override first
    if (DEV_THEME_OVERRIDE >= 0 && DEV_THEME_OVERRIDE < THEME_COUNT) {
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
    
    // Load brightness
    unsigned char savedBrightness = preferences.getUChar("brightness", BRIGHTNESS);
    int brightnessToSet = min(MAX_BRIGHTNESS, max(10, (int)savedBrightness));
    
    // Load hue index
    unsigned char savedHueIndex = preferences.getUChar("hue_index", 0);
    int maxHueIndex = getCurrentHueCount() - 1;
    hueIndex = min(maxHueIndex, (int)savedHueIndex);
    
    preferences.end();
    
    // Apply loaded brightness
    FastLED.setBrightness(brightnessToSet);
    
    // Log all loaded preferences
    ESP_LOGI(TAG, "=== LOADED PREFERENCES ===");
    if (DEV_THEME_OVERRIDE >= 0 && DEV_THEME_OVERRIDE < THEME_COUNT) {
        ESP_LOGI(TAG, "Theme: %s (DEV OVERRIDE)", THEME_NAMES[currentTheme]);
    } else {
        ESP_LOGI(TAG, "Theme: %s", THEME_NAMES[currentTheme]);
    }
    ESP_LOGI(TAG, "Brightness: %d (max: %d)", brightnessToSet, MAX_BRIGHTNESS);
    ESP_LOGI(TAG, "Hue Index: %d (max: %d)", hueIndex, maxHueIndex);
    ESP_LOGI(TAG, "Color Change: %s", colorChangeEnabled ? "enabled" : "disabled");
    ESP_LOGI(TAG, "========================");
}

void switchToNextTheme() {
    HueTheme oldTheme = currentTheme;
    currentTheme = (HueTheme)((currentTheme + 1) % THEME_COUNT);
    hueIndex = 0; // Reset to first hue in new theme
    saveThemeToPreferences();
    ESP_LOGI(TAG, "Switched from %s to %s theme", THEME_NAMES[oldTheme], THEME_NAMES[currentTheme]);
    
    // Force immediate visual update
    const uint8_t* currentHues = getCurrentHueArray();
    uint8_t newHue = currentHues[hueIndex];
    fill_solid(leds, numLeds, CHSV(newHue, 255, 200));
    FastLED.show();
}

void switchToPreviousTheme() {
    HueTheme oldTheme = currentTheme;
    currentTheme = (HueTheme)((currentTheme - 1 + THEME_COUNT) % THEME_COUNT);
    hueIndex = 0; // Reset to first hue in new theme
    saveThemeToPreferences();
    ESP_LOGI(TAG, "Switched from %s to %s theme", THEME_NAMES[oldTheme], THEME_NAMES[currentTheme]);
    
    // Force immediate visual update
    const uint8_t* currentHues = getCurrentHueArray();
    uint8_t newHue = currentHues[hueIndex];
    fill_solid(leds, numLeds, CHSV(newHue, 255, 200));
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
        const uint8_t* currentHues = getCurrentHueArray();
        uint8_t hue = currentHues[hueIndex];
        fill_solid(leds, numLeds, CHSV(hue, 255, 200));
        FastLED.show();
        ESP_LOGI(TAG, "All LEDs turned ON (Theme: %s)", THEME_NAMES[currentTheme]);
    }
}

// MQTT Helpers
void publishState() {
    if (!mqtt.connected()) return;

    String topic = "lights/" + String(DEVICE_LOCATION) + "/state";
    String payload = "{\"theme\":\"" + String(THEME_NAMES[currentTheme]) + "\",";
    payload += "\"numLeds\":" + String(numLeds) + ",";
    payload += "\"ledsPerColor\":" + String(ledsPerColor) + ",";
    payload += "\"brightness\":" + String(FastLED.getBrightness()) + ",";
    payload += "\"ledsEnabled\":" + String(ledsEnabled ? "true" : "false") + "}";

    mqtt.publish(topic.c_str(), payload.c_str(), true);
    ESP_LOGI(TAG, "Published state to %s", topic.c_str());
}

void broadcastCommand(const char* cmd) {
    if (mqtt.connected()) {
        mqtt.publish("lights/all/cmd", cmd);
        ESP_LOGI(TAG, "Broadcasted command to all: %s", cmd);
    }
}

// Button debouncing function
#if ENABLE_IR
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
#endif

void onMqttMessage(char* topic, byte* payload, unsigned int len) {
    String msg;
    msg.reserve(len);
    for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
    msg.trim(); // Remove any hidden whitespace/newlines

    ESP_LOGI(TAG, "MQTT Topic: %s, Payload: '%s'", topic, msg.c_str());

    if (msg == "NEXT_THEME") {
        switchToNextTheme();
        publishState();
    } else if (msg == "PREV_THEME") {
        switchToPreviousTheme();
        publishState();
    } else if (msg == "STATUS") {
        publishState();
    } else if (msg.startsWith("SET_NUM_LEDS:")) {
        int val = msg.substring(13).toInt();
        if (val > 0 && val <= MAX_LEDS) {
            numLeds = val;
            saveLedConfigToPreferences();
            fill_solid(leds, MAX_LEDS, CRGB::Black);
            FastLED.show();
            publishState();
        }
    } else if (msg.startsWith("SET_LEDS_PER_COLOR:")) {
        int val = msg.substring(19).toInt();
        if (val > 0) {
            ledsPerColor = val;
            saveLedConfigToPreferences();
            publishState();
        }
    } else if (msg.startsWith("SET_BRIGHTNESS:")) {
        int val = msg.substring(15).toInt();
        if (val >= 0 && val <= 255) {
            if (FastLED.getBrightness() != val) {
                FastLED.setBrightness(val);
                FastLED.show();
                saveBrightnessToPreferences();
                publishState();
            }
        }
    } else {
        HueTheme newTheme = currentTheme;
        bool themeIdentified = false;

        if (msg == "GREEN" || msg == "THEME_GREEN" || msg == "SCENE_1") {
            newTheme = THEME_GREEN;
            themeIdentified = true;
        } else if (msg == "RAINBOW" || msg == "THEME_RAINBOW" || msg == "SCENE_2") {
            newTheme = THEME_RAINBOW;
            themeIdentified = true;
        } else if (msg == "PINK_PONY" || msg == "THEME_PINK_PONY") {
            newTheme = THEME_PINK_PONY;
            themeIdentified = true;
        } else if (msg == "OCEAN_WAVES" || msg == "THEME_OCEAN") {
            newTheme = THEME_OCEAN;
            themeIdentified = true;
        } else if (msg == "SUNSET" || msg == "THEME_SUNSET") {
            newTheme = THEME_SUNSET;
            themeIdentified = true;
        } else if (msg == "FOREST" || msg == "THEME_FOREST") {
            newTheme = THEME_FOREST;
            themeIdentified = true;
        }

        if (themeIdentified) {
            if (newTheme != currentTheme || !ledsEnabled) {
                currentTheme = newTheme;
                hueIndex = 0;
                saveThemeToPreferences();
                if (!ledsEnabled) ledsEnabled = true; // Use direct flag to avoid toggle logic
                
                // Force immediate visual update
                const uint8_t* currentHues = getCurrentHueArray();
                uint8_t newHue = currentHues[hueIndex];
                fill_solid(leds, numLeds, CHSV(newHue, 255, 200));
                FastLED.show();
                publishState();
            }
            return;
        }

        if (msg == "TOGGLE") {
            toggleAllLEDs();
            publishState();
        } else if (msg == "ON") {
            if (!ledsEnabled) {
                toggleAllLEDs();
                publishState();
            }
        } else if (msg == "OFF") {
            if (ledsEnabled) {
                toggleAllLEDs();
                publishState();
            }
        } else if (msg == "BRIGHT_UP") {
            increaseBrightness();
            publishState();
        } else if (msg == "BRIGHT_DOWN") {
            decreaseBrightness();
            publishState();
        } else if (msg == "COLOR_CHANGE_ON") {
            if (!colorChangeEnabled) {
                colorChangeEnabled = true;
                publishState();
            }
        } else if (msg == "COLOR_CHANGE_OFF") {
            if (colorChangeEnabled) {
                colorChangeEnabled = false;
                publishState();
            }
        }
    }
}

void ensureWifi() {
    if (WiFi.status() == WL_CONNECTED) return;

    ESP_LOGI(TAG, "WiFi connecting to %s...", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    // Quick connect attempt (non-blocking in loop, but here we wait a bit in setup or first run)
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 5000) {
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
    if (now < mqttRetryAt) return;
    mqttRetryAt = now + MQTT_RETRY_MS;

    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setCallback(onMqttMessage);

    String clientId = "esp32c3-glowkitchen-" + String(DEVICE_LOCATION) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);

    ESP_LOGI(TAG, "MQTT connecting to %s...", MQTT_HOST);
    if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
        ESP_LOGI(TAG, "MQTT connected; subscribing...");
        mqtt.subscribe("lights/all/cmd");
        
        String locationTopic = "lights/" + String(DEVICE_LOCATION) + "/cmd";
        mqtt.subscribe(locationTopic.c_str());
        ESP_LOGI(TAG, "Subscribed to %s", locationTopic.c_str());

        // Report initial state
        publishState();
    } else {
        ESP_LOGW(TAG, "MQTT connect failed, rc=%d", mqtt.state());
    }
}

// Function to identify button from IR data
#if ENABLE_IR
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
#endif

void flickerLEDs() {
    bool didChange = false;

    const uint8_t* currentHues = getCurrentHueArray();
    int currentHueCount = getCurrentHueCount();

    unsigned long now = millis();
    
    // Update individual LED flickering
    for (int i = 0; i < numLeds; i++) {
      if (timeouts[i] < now) {
          didChange = true;
          unsigned long delay = random(500, 750);  // Slower, more candle-like flicker
          timeouts[i] = now + delay;
          uint8_t flicker = random(120, 255);
          uint8_t hue = currentHues[hueIndex];
          leds[i] = CHSV(hue, 255, flicker);
      }
    }
    
    if (didChange) {
        FastLED.show();
    }

    // Auto color change within current theme
    if (colorChangeEnabled && hueTimeout < now) {
        hueIndex++;
        hueTimeout = now + 2000;
        if (hueIndex >= currentHueCount) {
            hueIndex = 0;
        }
        //saveHueToPreferences();
    }
}

void slowBlend() {
    unsigned long now = millis();
    
    // Update blend animation
    if (blendTimeout < now) {
        const uint8_t* currentHues = getCurrentHueArray();
        int currentHueCount = getCurrentHueCount();
        
        for (int i = 0; i < numLeds; i++) {
            // Calculate which color group this LED belongs to, with offset applied per LED
            int effectiveLedPosition = (i + blendOffset) % (numLeds * currentHueCount);
            int groupIndex = (effectiveLedPosition / ledsPerColor) % currentHueCount;
            int nextGroupIndex = (groupIndex + 1) % currentHueCount;
            
            // Get the current and next colors
            uint8_t currentHue = currentHues[groupIndex];
            uint8_t nextHue = currentHues[nextGroupIndex];
            
            // Calculate position within the color group (0 to ledsPerColor-1)
            int ledInGroup = effectiveLedPosition % ledsPerColor;
            
            // Calculate blend fraction (0.0 at start of group, 1.0 at end of group)
            // This creates a smooth transition across the ledsPerColor range
            float blendFraction = (float)ledInGroup / (float)ledsPerColor;
            
            // Interpolate between current and next hue using the SHORTEST path around the hue circle
            uint8_t blendedHue;
            
            // Calculate distances in both directions around the hue circle
            int forwardDistance = (nextHue - currentHue + 256) % 256;
            int backwardDistance = (currentHue - nextHue + 256) % 256;
            int shortestDistance = min(forwardDistance, backwardDistance);
            
            // Only blend if colors are close together (within 60 hue units)
            // This allows Pink Pony Club (55 unit span) to blend smoothly
            // while preventing blending across larger gaps in Halloween (84 units) and Christmas (65 units)
            const int BLEND_THRESHOLD = 60;
            
            if (shortestDistance <= BLEND_THRESHOLD) {
                // Colors are close - do smooth blending
                if (forwardDistance <= backwardDistance) {
                    // Go forward (clockwise around hue circle)
                    blendedHue = (currentHue + (uint8_t)(forwardDistance * blendFraction)) % 256;
                } else {
                    // Go backward (counter-clockwise around hue circle)
                    blendedHue = (currentHue - (uint8_t)(backwardDistance * blendFraction) + 256) % 256;
                }
            } else {
                // Colors are too far apart - don't blend, just use current color
                // This keeps Halloween oranges separate from purples, Christmas reds separate from greens
                blendedHue = currentHue;
            }
            
            // Add subtle brightness variation for visual interest
            int baseBrightness = FastLED.getBrightness();
            uint8_t brightness = min(MAX_BRIGHTNESS, baseBrightness + sin8(now/15 + ledInGroup*40)/12);
            
            leds[i] = CHSV(blendedHue, 255, brightness);
        }
        
        FastLED.show();
        blendTimeout = now + blendInterval;
        
        // Slowly rotate the color groups if color change is enabled
        if (colorChangeEnabled) {
            static unsigned long rotateTimeout = 0;
            if (rotateTimeout < now) {
                blendOffset = (blendOffset + 1) % (numLeds * currentHueCount); // Move one LED at a time
                rotateTimeout = now + 500; // Move every 500ms for slower, more relaxed scrolling
            }
        }
    }
}

void setupLED() {
    // Load all saved preferences (theme, brightness, hue, led config)
    loadAllPreferences();

    // Initialize with MAX_LEDS so we can change numLeds at runtime without re-initializing
    FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, MAX_LEDS);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 500); // volts, mA
    
    ESP_LOGI(TAG, "LED setup complete with %d LEDs (max %d)", numLeds, MAX_LEDS);

    unsigned long now = millis();
    for (int i = 0; i < MAX_LEDS; i++) {
      timeouts[i] = now;
    }
    hueTimeout = now + 2000;
    logTimeout = now + logInterval;
}

#if ENABLE_IR
void setupIR() {    
    // Initialize IR receiver
    IrReceiver.begin(irReceiverPin, DISABLE_LED_FEEDBACK);
    
    ESP_LOGI(TAG, "IR receiver initialized on pin %d", irReceiverPin);
    ESP_LOGI(TAG, "IR receiver ready: %s", IrReceiver.isIdle() ? "true" : "false");
}
#endif

void loopLED() {    
    unsigned long now = millis();

    if (logTimeout < now) {
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

    // Use different effects based on theme
    if (currentTheme == THEME_GREEN) {
        flickerLEDs();  // Candle flicker effect for green theme
    } else {
        slowBlend();    // Gradient blend effect for rainbow, halloween, christmas
    }
}

#if ENABLE_IR
void loopIR() {
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
    if (irLogTimeout < now) {
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
#endif

void setup() {
    Serial.begin(115200);

    setupLED();
#if ENABLE_IR
    setupIR();
#endif
    
    ensureWifi();
}

void loop() {
    ensureWifi();
    ensureMqtt();
    
    loopLED();
#if ENABLE_IR
    loopIR();
#endif
}
