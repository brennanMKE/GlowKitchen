#!/bin/zsh

# Build a release firmware and refuse to produce one that cannot be installed.
#
# The gate exists because v0.0.4 shipped as a GitHub release that no device
# could ever apply: firmware.bin was 1,340,544 bytes against a 1,310,720-byte
# OTA slot, so httpUpdate rejected it with "Not Enough Space" on every device,
# every time, and reported the failure only to serial. Nothing in the build
# caught it -- PlatformIO's "99.1% used" is the sum of loadable ELF sections and
# understates the real .bin by ~42 KB of image header, segment padding and the
# appended SHA-256. Only the artifact on disk tells the truth, so that is what
# this script measures.
#
# Usage: ./scripts/release.sh [version]
#   version: e.g. 0.0.5. Defaults to FIRMWARE_VERSION in src/main.cpp.
#
# Exits non-zero if the image will not fit. It does not tag or publish -- see
# the instructions it prints on success.

set -e

# Always the release environment. The esp32c3-debug env builds ~45 KB larger and
# does not fit the OTA slot at all -- it is a USB-only build.
ENVIRONMENT="esp32c3"
BIN=".pio/build/$ENVIRONMENT/firmware.bin"
SRC="src/main.cpp"

# ota_0 / ota_1 from the default 4MB partition table (0x140000 each). If
# board_build.partitions is ever set in platformio.ini, update this to match --
# a partition change cannot be delivered by OTA, so it is a deliberate decision.
OTA_PARTITION_SIZE=$((0x140000))

cd "$(dirname "$0")/.."

SRC_VERSION=$(grep -E '^#define FIRMWARE_VERSION' "$SRC" | sed -E 's/.*"([^"]+)".*/\1/')
VERSION="${1:-$SRC_VERSION}"

if [[ "$VERSION" != "$SRC_VERSION" ]]; then
    echo "Error: requested version $VERSION but $SRC says $SRC_VERSION"
    echo "       Bump FIRMWARE_VERSION first so the running firmware reports the truth."
    exit 1
fi

echo "Building GlowKitchen $VERSION ($ENVIRONMENT)..."
~/.platformio/penv/bin/pio run -e "$ENVIRONMENT"

if [[ ! -f "$BIN" ]]; then
    echo "Error: $BIN not produced"
    exit 1
fi

SIZE=$(stat -f%z "$BIN")
HEADROOM=$((OTA_PARTITION_SIZE - SIZE))
PERCENT=$(( SIZE * 100 / OTA_PARTITION_SIZE ))

echo ""
echo "  firmware.bin   $(printf "%'d" $SIZE) bytes"
echo "  OTA partition  $(printf "%'d" $OTA_PARTITION_SIZE) bytes"
echo "  headroom       $(printf "%'d" $HEADROOM) bytes (${PERCENT}% used)"
echo ""

if (( SIZE > OTA_PARTITION_SIZE )); then
    echo "REFUSING TO RELEASE: image is $(printf "%'d" $((-HEADROOM))) bytes too large."
    echo ""
    echo "Every OTA path would fail with \"Not Enough Space\" and report it only"
    echo "to serial, so the fleet would silently stay on the old firmware."
    echo ""
    echo "Reclaim space by lowering CORE_DEBUG_LEVEL in platformio.ini"
    echo "(measured: level 5 -> 1,340,544  3 -> 1,319,088  2 -> 1,304,208  1 -> 1,295,360),"
    echo "or move to a larger partition scheme -- but note a partition table cannot"
    echo "be delivered over OTA and needs a USB flash on every device."
    exit 1
fi

# Not fatal, but a release this tight will not survive the next feature.
if (( HEADROOM < 20480 )); then
    echo "WARNING: only $(printf "%'d" $HEADROOM) bytes spare (<20 KB)."
    echo "         The next change of any size will breach the limit."
    echo ""
fi

echo "Size gate passed."
echo ""
echo "Verify the version is readable from the image:"
echo "  ./scripts/firmware_info.sh $BIN"
echo ""
echo "Then publish. The asset MUST be named firmware.bin (the download URL is"
echo "hardcoded) and the release must NOT be marked pre-release, because GitHub"
echo "excludes pre-releases from /releases/latest and the fleet would ignore it:"
echo "  git tag -a v$VERSION -m \"v$VERSION\" && git push origin v$VERSION"
echo "  gh release create v$VERSION $BIN --title \"v$VERSION\" --notes-from-tag"
echo ""
echo "Confirm the fleet sees it:"
echo "  curl -s https://api.github.com/repos/brennanMKE/GlowKitchen/releases/latest | grep tag_name"
