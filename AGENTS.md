# AGENTS.md - GlowKitchen Project

## Build, Lint, and Test Commands

### Build Commands
- `pio run` - Build the project for the ESP32-C3 target (default environment)
- `pio run -e esp32c3` - Explicitly build for ESP32-C3 environment
- `pio build` - Build project (alias for pio run)

### Lint Commands
- `pio lint` - Run linter on the project (if available)
- `cppcheck src/` - Run C++ static analysis on source files
- `clang-tidy src/main.cpp` - Run clang-tidy for code quality checks

### Test Commands
- `pio test` - Run tests (if configured)
- `pio test --environment esp32c3` - Run tests for ESP32-C3 target
- `pio test --verbose` - Run tests with verbose output

### Running Individual Tests
Since this is an ESP32 Arduino project, unit tests may not be configured in the standard way. To run specific test cases:
1. Use `pio test` to run all tests
2. For development, use the platformio.ini configuration to target specific environments
3. Debug with `pio run --verbose` to see compilation details

## Code Style Guidelines

### General Structure
- Use Arduino C++ style with proper header guards and includes
- Follow the ESP32 Arduino framework conventions for hardware control
- All code should be compatible with ESP-IDF and Arduino frameworks

### Imports and Dependencies
- All library includes should be at the top of files in alphabetical order
- Use `#include <LibraryName.h>` for system libraries and `#include "local_file.h"` for local files
- Avoid including unused libraries to reduce memory usage on ESP32
- Use specific library versions in platformio.ini (e.g., FastLED, IRRemote, PubSubClient)

### Formatting
- Use 4-space indentation (no tabs)
- Allman-style braces `{` on new lines
- One statement per line
- Use camelCase for variables and functions (e.g., `ledCount`, `updateTheme`)
- Use UPPER_CASE for constants (e.g., `MAX_LEDS`, `DATA_PIN`)
- All variables must be declared before use
- Leave one blank line between major sections of code

### Naming Conventions
- Functions: `camelCase` with descriptive names (e.g., `updateLeds`, `handleIrCommand`)
- Variables: `camelCase` (e.g., `ledCount`, `themeIndex`)
- Constants: `UPPER_CASE_WITH_UNDERSCORES` (e.g., `MAX_LEDS`, `DATA_PIN`)
- Classes: `PascalCase` (if used)
- Files: `lowercase_with_underscores.cpp` (e.g., `main.cpp`, `led_controller.cpp`)

### Types
- Use explicit types (int, float, bool, etc.) instead of auto when possible
- Prefer `const` for values that won't change
- Use `static` for variables that should persist across function calls
- Use `constexpr` for compile-time constants where appropriate

### Error Handling
- All hardware operations should check return values and handle errors gracefully
- Use ESP_LOG* macros for logging (e.g., `ESP_LOGI(TAG, "Message")`)
- Implement proper error recovery for network operations (WiFi, MQTT)
- Handle memory allocation failures in ESP32 environment
- Use try/catch blocks for exception-safe code when appropriate

### Memory Management
- Avoid dynamic memory allocation where possible on ESP32
- Use fixed-size arrays instead of dynamically allocated arrays when size is known at compile time
- Be mindful of stack size limitations (ESP32 has limited RAM)
- Use `PROGMEM` for string literals that don't change

### ESP32-Specific Guidelines
- Use `ESP32` specific features only where necessary (e.g., RMT peripheral for LED control)
- Prefer hardware timers over software delays when possible
- Use appropriate pin numbers (GPIO 4 for DATA_PIN in this project)
- Handle power management and sleep states appropriately
- Use proper task priorities when using FreeRTOS tasks

### Configuration Management
- Use `#if` guards for conditional compilation (e.g., `#if ENABLE_IR`)
- Store persistent configuration in Preferences or EEPROM
- Use constants for hardware pin numbers and configuration values

### Comments and Documentation
- Follow Doxygen-style comments for public APIs
- Use inline comments to explain complex logic or hardware-specific behavior
- Document all function parameters and return values
- Keep comments up-to-date with code changes

### Code Organization
- Separate concerns into logical modules (LED control, IR handling, WiFi/MQTT)
- Use header files for function declarations and constants
- Group related functions together in logical sections with comments
- Keep functions small and focused on a single task (single responsibility principle)

## Supported Themes

The GlowKitchen LED system supports the following themes:

1. **Green** - Original candle-like greens with flickering effect
2. **Rainbow** - Full spectrum cycling with gradient blend effect  
3. **Pink Pony Club** - Pink, magenta, and pony colors with gradient blend
4. **Ocean Waves** - Deep blues and teal colors that shift like ocean waves with gradient blend
5. **Sunset** - Warm oranges and pinks mimicking a sunset sky with gradient blend
6. **Forest** - Natural greens and earth tones capturing forest colors with gradient blend

All themes integrate with MQTT commands using the following format:
- GREEN, RAINBOW, PINK_PONY, OCEAN_WAVES, SUNSET, FOREST

MQTT commands for theme switching now support all 6 themes including the new Ocean Waves, Sunset, and Forest themes.