#include <Arduino.h>
#include <FastLED.h>
#include <IRremote.h>
#include <esp_log.h>
#include <Preferences.h>

static const char *TAG = "MAIN";

/// DEVELOPMENT OVERRIDE
// Set to a valid theme index (0-4) to force that theme, or -1 to use saved preferences
// 0=Green, 1=Rainbow, 2=Halloween, 3=Christmas, 4=Pink Pony Club
const int DEV_THEME_OVERRIDE = 0;  // Change this to override theme for development

/// LED

const int BRIGHTNESS = 120;
const int MAX_BRIGHTNESS = 180;
const int MIN_BRIGHTNESS = 50;

#define NUM_LEDS 10
#define DATA_PIN 4

CRGB leds[NUM_LEDS];
unsigned long timeouts[NUM_LEDS];

// Theme System
enum HueTheme {
    THEME_GREEN = 0,
    THEME_RAINBOW = 1,
    THEME_PINK_PONY = 2,
    THEME_HALLOWEEN = 3,
    THEME_CHRISTMAS = 4,
    THEME_COUNT = 5
};

const char* THEME_NAMES[] = {
    "Green",
    "Rainbow", 
    "Pink Pony Club",
    "Halloween",
    "Christmas"
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
unsigned long blendInterval = 50;  // Update blend every 50ms for smooth transitions
uint8_t blendOffset = 0;           // Offset for rotating the gradient
uint8_t blendBrightness = MAX_BRIGHTNESS;     // Base brightness for blend mode

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

// Theme switching - now simplified to direct button control

const uint8_t NEC_REMOTE_ADDRESS = 0x0;

// Theme Helper Functions
const uint8_t* getCurrentHueArray() {
    switch (currentTheme) {
        case THEME_GREEN: return GREEN_HUES;
        case THEME_RAINBOW: return RAINBOW_HUES;
        case THEME_HALLOWEEN: return HALLOWEEN_HUES;
        case THEME_CHRISTMAS: return CHRISTMAS_HUES;
        case THEME_PINK_PONY: return PINK_PONY_HUES;
        default: return GREEN_HUES;
    }
}

int getCurrentHueCount() {
    switch (currentTheme) {
        case THEME_GREEN: return NUM_GREEN_HUES;
        case THEME_RAINBOW: return NUM_RAINBOW_HUES;
        case THEME_HALLOWEEN: return NUM_HALLOWEEN_HUES;
        case THEME_CHRISTMAS: return NUM_CHRISTMAS_HUES;
        case THEME_PINK_PONY: return NUM_PINK_PONY_HUES;
        default: return NUM_GREEN_HUES;
    }
}

void saveThemeToPreferences() {
    preferences.begin("led_cabinet", false);
    preferences.putUChar("theme", (unsigned char)currentTheme);
    preferences.end();
    ESP_LOGI(TAG, "Theme saved: %s", THEME_NAMES[currentTheme]);
}

void saveBrightnessToPreferences() {
    int currentBrightness = FastLED.getBrightness();
    preferences.begin("led_cabinet", false);
    preferences.putUChar("brightness", (unsigned char)currentBrightness);
    preferences.end();
    ESP_LOGI(TAG, "Brightness saved: %d", currentBrightness);
}

void saveHueToPreferences() {
    preferences.begin("led_cabinet", false);
    preferences.putUChar("hue_index", (unsigned char)hueIndex);
    preferences.end();
    ESP_LOGI(TAG, "Hue index saved: %d", hueIndex);
}

void loadAllPreferences() {
    preferences.begin("led_cabinet", true);
    
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
    fill_solid(leds, NUM_LEDS, CHSV(newHue, 255, 200));
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
    fill_solid(leds, NUM_LEDS, CHSV(newHue, 255, 200));
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
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
        delay(10); // Small delay to ensure the command is processed
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
        ESP_LOGI(TAG, "All LEDs turned OFF");
    } else {
        // Turn on all LEDs with current hue from current theme
        ESP_LOGI(TAG, "Turning ON all LEDs");
        const uint8_t* currentHues = getCurrentHueArray();
        uint8_t hue = currentHues[hueIndex];
        fill_solid(leds, NUM_LEDS, CHSV(hue, 255, 200));
        FastLED.show();
        ESP_LOGI(TAG, "All LEDs turned ON (Theme: %s)", THEME_NAMES[currentTheme]);
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
            break;
        case NEC_VOL_MINUS:
            decreaseBrightness();
            break;
        case NEC_REWIND:
            // Rewind = Previous Theme
            switchToPreviousTheme();
            break;
        case NEC_FORWARD:
            // Forward = Next Theme
            switchToNextTheme();
            break;
        case NEC_PLAY_PAUSE:
        case NEC_POWER:
            // Both Play/Pause and Power toggle LEDs
            toggleAllLEDs();
            break;
        case NEC_MODE:
            // Mode = Toggle auto color change
            toggleColorChange();
            break;
        default:
            ESP_LOGI(TAG, "Unknown or unmapped button pressed (Command: 0x%X)", button);
            break;
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

void flickerLEDs() {
    bool didChange = false;

    const uint8_t* currentHues = getCurrentHueArray();
    int currentHueCount = getCurrentHueCount();

    unsigned long now = millis();
    
    // Update individual LED flickering
    for (int i = 0; i < NUM_LEDS; i++) {
      if (timeouts[i] < now) {
          didChange = true;
          unsigned long delay = random(25, 100);
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
        
        // Create color groups - each color spans 3 LEDs
        const int LEDS_PER_COLOR = 3;
        
        for (int i = 0; i < NUM_LEDS; i++) {
            // Calculate which color group this LED belongs to, with offset applied per LED
            int effectiveLedPosition = (i + blendOffset) % (NUM_LEDS * currentHueCount);
            int groupIndex = (effectiveLedPosition / LEDS_PER_COLOR) % currentHueCount;
            
            // Get the color for this group
            uint8_t hue = currentHues[groupIndex];
            
            // Add subtle brightness variation for visual interest
            // Use the LED position within the group for slight variation
            int baseBrightness = FastLED.getBrightness();
            int ledInGroup = effectiveLedPosition % LEDS_PER_COLOR;
            uint8_t brightness = min(MAX_BRIGHTNESS, baseBrightness + sin8(now/15 + ledInGroup*40)/12);
            
            leds[i] = CHSV(hue, 255, brightness);
        }
        
        FastLED.show();
        blendTimeout = now + blendInterval;
        
        // Slowly rotate the color groups if color change is enabled
        if (colorChangeEnabled) {
            static unsigned long rotateTimeout = 0;
            if (rotateTimeout < now) {
                blendOffset = (blendOffset + 1) % (NUM_LEDS * currentHueCount); // Move one LED at a time
                rotateTimeout = now + 200; // Move every 200ms for smooth scrolling
            }
        }
    }
}

void setupLED() {
    FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 500); // volts, mA
    
    // Load all saved preferences (theme, brightness, hue)
    loadAllPreferences();
    
    ESP_LOGI(TAG, "LED setup complete");

    unsigned long now = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      timeouts[i] = now;
    }
    hueTimeout = now + 2000;
    logTimeout = now + logInterval;
}

void setupIR() {    
    // Initialize IR receiver
    IrReceiver.begin(irReceiverPin, DISABLE_LED_FEEDBACK);
    
    ESP_LOGI(TAG, "IR receiver initialized on pin %d", irReceiverPin);
    ESP_LOGI(TAG, "IR receiver ready: %s", IrReceiver.isIdle() ? "true" : "false");
}

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
            fill_solid(leds, NUM_LEDS, CRGB::Black);
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

void setup() {
    Serial.begin(115200);

    setupLED();
    setupIR();
}

void loop() {
    loopLED();
    loopIR();
}
