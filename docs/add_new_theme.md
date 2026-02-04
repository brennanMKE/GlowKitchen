# Adding New Themes to GlowKitchen

This document provides instructions for adding new themes to the GlowKitchen LED project.

## Theme Types

There are two types of themes in the GlowKitchen system:

1. **Green Theme** - Uses flickering effect for candle-like appearance
2. **Other Themes** - Use gradient blending with multiple hue values

## Adding a New Theme

### Step 1: Define Theme Constants

Add your theme to the `HueTheme` enum in `src/main.cpp`:

```cpp
enum HueTheme {
    THEME_GREEN = 0,
    THEME_RAINBOW = 1,
    THEME_PINK_PONY = 2,
    THEME_OCEAN_WAVES = 3,
    THEME_SUNSET = 4,
    THEME_FOREST = 5,
    THEME_YOUR_THEME = 6,  // Add your theme here
    THEME_COUNT = 7        // Update this count
};
```

### Step 2: Add Theme Name String

Add your theme name to the `THEME_NAMES` array:

```cpp
const char* THEME_NAMES[] = {
    "Green",
    "Rainbow", 
    "Pink Pony Club",
    "Ocean Waves",
    "Sunset",
    "Forest",
    "Your Theme Name"  // Add your theme name here
};
```

### Step 3: Define Hue Values

Depending on your theme type, add the appropriate hue array:

#### For Non-Green Themes (with multiple hues):

Add the hue values for your theme (similar to existing themes like Rainbow, Ocean Waves, Sunset, Forest):

```cpp
// Your Theme - Description of your theme
const uint8_t YOUR_THEME_HUES[] = {
    0,   // First color
    32,  // Second color
    64,  // Third color
    // ... more colors as needed
};

const int NUM_YOUR_THEME_HUES = sizeof(YOUR_THEME_HUES) / sizeof(YOUR_THEME_HUES[0]);
```

#### For Green Theme (flickering effect):

The Green theme uses a specific flickering effect and doesn't need hue arrays. It's handled separately in the `flickerLEDs()` function.

### Step 4: Update Theme Helper Functions

Update the `getCurrentHueArray()` function to return your theme's hue array:

```cpp
const uint8_t* getCurrentHueArray() {
    switch (currentTheme) {
        case THEME_GREEN: return GREEN_HUES;
        case THEME_RAINBOW: return RAINBOW_HUES;
        case THEME_PINK_PONY: return PINK_PONY_HUES;
        case THEME_OCEAN_WAVES: return OCEAN_HUES;
        case THEME_SUNSET: return SUNSET_HUES;
        case THEME_FOREST: return FOREST_HUES;
        case THEME_YOUR_THEME: return YOUR_THEME_HUES;  // Add your theme here
        default: return GREEN_HUES;
    }
}
```

Update `getCurrentHueCount()` function:

```cpp
int getCurrentHueCount() {
    switch (currentTheme) {
        case THEME_GREEN: return NUM_GREEN_HUES;
        case THEME_RAINBOW: return NUM_RAINBOW_HUES;
        case THEME_PINK_PONY: return NUM_PINK_PONY_HUES;
        case THEME_OCEAN_WAVES: return NUM_OCEAN_HUES;
        case THEME_SUNSET: return NUM_SUNSET_HUES;
        case THEME_FOREST: return NUM_FOREST_HUES;
        case THEME_YOUR_THEME: return NUM_YOUR_THEME_HUES;  // Add your theme here
        default: return NUM_GREEN_HUES;
    }
}
```

### Step 5: Add MQTT Command Support

Update the theme identification in `onMqttMessage()` function to recognize your new theme:

```cpp
if (msg == "YOUR_THEME" || msg == "THEME_YOUR_THEME") {
    newTheme = THEME_YOUR_THEME;
    themeIdentified = true;
}
```

### Step 6: Update MQTT Commands

Update the `getThemeMqttCommand()` function to return the correct MQTT command:

```cpp
const char* getThemeMqttCommand(HueTheme theme) {
    switch (theme) {
        case THEME_GREEN: return "GREEN";
        case THEME_RAINBOW: return "RAINBOW";
        case THEME_PINK_PONY: return "PINK_PONY";
        case THEME_OCEAN_WAVES: return "OCEAN_WAVES";
        case THEME_SUNSET: return "SUNSET";
        case THEME_FOREST: return "FOREST";
        case THEME_YOUR_THEME: return "YOUR_THEME";  // Add your theme command here
        default: return "GREEN";
    }
}
```

### Theme Type Considerations

**Important**: When implementing new themes, consider their behavior:
- **Green Theme** (THEME_GREEN) uses special flickering effects and will not blend colors
- **Other Themes** (all others) use gradient blending and will smoothly transition between defined hues

The system uses different LED effects based on theme type:
- Green themes use `flickerLEDs()` function for candle-like flickering
- Other themes use `slowBlend()` function for gradient color blending

This ensures proper visual effects for each theme type.
