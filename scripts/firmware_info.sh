#!/bin/zsh

# Print identifying details of a compiled firmware image:
#   FIRMWARE_VERSION, file size, modification time, SHA-256, MD5, and (if
#   esptool is available) the ESP32 image info. Use this before publishing a
#   GitHub release to confirm the .bin was built with the intended version.
#
# Usage: ./scripts/firmware_info.sh [path/to/firmware.bin]
#   Defaults to .pio/build/esp32c3/firmware.bin

BIN="${1:-.pio/build/esp32c3/firmware.bin}"

if [[ ! -f "$BIN" ]]; then
    echo "Error: firmware not found: $BIN" >&2
    echo "Build it first with: pio run -e esp32c3" >&2
    exit 1
fi

# --- FIRMWARE_VERSION ---
# Preferred: the marker embedded by the firmware (GLOWKITCHEN_FWVER=x.y.z).
# Fallback: the lone semver-looking string (works on older builds without the
# marker, but is heuristic and may be wrong if a library embeds a version too).
VERSION=$(strings -n 4 "$BIN" | grep -m1 '^GLOWKITCHEN_FWVER=' | cut -d'=' -f2)
if [[ -z "$VERSION" ]]; then
    VERSION=$(strings -n 4 "$BIN" | grep -Em1 '^[0-9]+\.[0-9]+\.[0-9]+$')
    if [[ -n "$VERSION" ]]; then
        VERSION="$VERSION  (heuristic — no marker; rebuild for reliable detection)"
    else
        VERSION="(not found)"
    fi
fi

# --- size / time / checksums (macOS coreutils) ---
BYTES=$(stat -f%z "$BIN")
HUMAN=$(du -h "$BIN" | cut -f1)
MTIME=$(stat -f '%Sm' -t '%Y-%m-%d %H:%M:%S' "$BIN")
SHA256=$(shasum -a 256 "$BIN" | awk '{print $1}')
MD5=$(md5 -q "$BIN")

echo "Firmware image:   $BIN"
echo "FIRMWARE_VERSION: $VERSION"
echo "Size:             $BYTES bytes ($HUMAN)"
echo "Modified:         $MTIME"
echo "SHA-256:          $SHA256"
echo "MD5:              $MD5"

# --- optional: ESP32 image details via esptool, if present on PATH ---
ESPTOOL=""
command -v esptool.py >/dev/null 2>&1 && ESPTOOL="esptool.py"
[[ -z "$ESPTOOL" ]] && command -v esptool >/dev/null 2>&1 && ESPTOOL="esptool"
if [[ -n "$ESPTOOL" ]]; then
    echo
    echo "ESP32 image info ($ESPTOOL):"
    "$ESPTOOL" --chip esp32c3 image_info "$BIN" 2>/dev/null | sed 's/^/  /' \
        || echo "  (image_info unavailable for this esptool version)"
fi
