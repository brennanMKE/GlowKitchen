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
# The second gate is the CA gate. Since issue #0010 the firmware no longer
# carries the Mozilla root store; it pins the two roots GitHub actually chains
# to. That saves 69,200 bytes and buys a new silent failure mode: if GitHub
# migrates to an unpinned root, every OTA path dies fleet-wide at once and says
# so only to serial. GitHub has already moved once (DigiCert -> Sectigo), so
# this is not hypothetical. The gate turns that outage into a build failure
# here, which is the same bargain the size gate makes.
#
# Usage: ./scripts/release.sh [version]
#   version: e.g. 0.0.5. Defaults to FIRMWARE_VERSION in src/main.cpp.
#
# Exits non-zero if the image will not fit, or if a GitHub host no longer
# chains to a pinned root. It does not tag or publish -- see the instructions
# it prints on success.

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

# ---------------------------------------------------------------------------
# CA gate: do the pinned roots still match what GitHub presents?
# ---------------------------------------------------------------------------

# These hosts do NOT share a root. api.github.com answers the version check and
# github.com issues the /releases/latest/download redirect -- both Sectigo --
# while the firmware.bin bytes come from a githubusercontent.com host that
# chains to ISRG instead. Checking only the API host would pass a build whose
# download step cannot complete, which is the failure this gate exists to stop.
#
# The asset host is a redirect target discovered at runtime, not a constant, and
# it has changed before: /releases/latest/download/ now lands on
# release-assets.githubusercontent.com, where it previously used
# objects.githubusercontent.com. So it must be established by following the
# redirect (see verify_asset_host below) rather than assumed -- a gate watching
# a hostname the firmware never contacts would stay green through exactly the
# migration it is meant to catch. Both are listed because the old host is still
# live and still serves other asset paths.
OTA_HOSTS=(
    api.github.com
    github.com
    release-assets.githubusercontent.com
    objects.githubusercontent.com
)

# Where the firmware actually downloads from, so the gate can confirm the list
# above still covers the real asset host instead of trusting it.
RELEASE_ASSET_URL="https://github.com/brennanMKE/GlowKitchen/releases/latest/download/firmware.bin"

# Warn this far ahead of a pinned root expiring. Both currently run past 2045,
# so this should stay quiet for two decades; it is here for the rotation.
EXPIRY_WARN_DAYS=180

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# The pinned roots are read out of the firmware source rather than duplicated
# here. A second copy of the fingerprints in this script would be one more
# thing that can silently drift out of step with what actually ships.
sed -n '/GITHUB_ROOT_CAS\[\] PROGMEM/,/;$/p' "$SRC" \
    | grep -oE '"[^"]*"' \
    | sed -e 's/^"//' -e 's/\\n"$//' > "$WORK/pinned.pem"

if ! grep -q "BEGIN CERTIFICATE" "$WORK/pinned.pem"; then
    echo "Error: could not extract GITHUB_ROOT_CAS from $SRC"
    echo "       The CA gate cannot verify what it cannot read."
    exit 1
fi

# Split a PEM bundle into $2-1.pem, $2-2.pem, ... and echo the count.
split_pem() {
    awk -v out="$2" 'BEGIN{n=0}
        /BEGIN CERTIFICATE/{n++}
        n>0{print > (out "-" n ".pem")}
        END{print n}' "$1"
}

# The identity that matters is the public key, not the certificate. Roots get
# cross-signed -- GitHub presents Sectigo E46 signed by USERTrust ECC and ISRG
# Root YR signed by ISRG Root X1, both of which have different SHA-256
# fingerprints from the self-signed roots we embed while carrying the same key
# and subject. Comparing certificate fingerprints alone would fail on a chain
# that works perfectly. mbedTLS terminates on subject + key, so that is what is
# compared here; the certificate fingerprint is printed for the record.
#
# Note this is the corroborating check, not the gate. A server is not obliged
# to send its root at all -- most do not -- so "top of chain is not a pinned
# root" is a normal, working configuration and must not fail a release. The
# gate below is openssl verify, which replays the device's actual decision.
spki_of() {
    openssl x509 -in "$1" -noout -pubkey \
        | openssl pkey -pubin -outform der 2>/dev/null \
        | openssl dgst -sha256 | awk '{print $NF}'
}
fp_of() {
    openssl x509 -in "$1" -noout -fingerprint -sha256 | sed 's/.*=//'
}
cn_of() {
    openssl x509 -in "$1" -noout -subject | sed -E 's/.*CN ?= ?//'
}

# openssl s_client has no connect timeout, and a black-holed route would hang
# the release for ~75s per host. Cap it so a bad network is a fast warning.
run_with_timeout() {
    local secs=$1; shift
    "$@" &
    local pid=$! waited=0
    while (( waited < secs * 10 )); do
        kill -0 $pid 2>/dev/null || break
        sleep 0.1
        (( waited += 1 ))
    done
    if kill -0 $pid 2>/dev/null; then
        kill -9 $pid 2>/dev/null
        wait $pid 2>/dev/null || true
        return 124
    fi
    wait $pid
}

PINNED_COUNT=$(split_pem "$WORK/pinned.pem" "$WORK/pin")
echo "Pinned roots in $SRC ($PINNED_COUNT):"

PINNED_SPKI=()
CA_WARN=0
for i in {1..$PINNED_COUNT}; do
    P="$WORK/pin-$i.pem"
    PINNED_SPKI+=("$(spki_of "$P")")
    EXPIRES=$(openssl x509 -in "$P" -noout -enddate | sed 's/notAfter=//')
    echo "  $(cn_of "$P")"
    echo "    expires  $EXPIRES"
    echo "    sha256   $(fp_of "$P")"
    if ! openssl x509 -in "$P" -noout -checkend $(( EXPIRY_WARN_DAYS * 86400 )) >/dev/null 2>&1; then
        echo ""
        echo "WARNING: this root expires within ${EXPIRY_WARN_DAYS} days."
        echo "         Devices that miss the replacement firmware lose OTA permanently"
        echo "         and can only be recovered by a LAN push (scripts/ota_local.sh)."
        CA_WARN=1
    fi
done
echo ""

CA_FAIL=0
CA_CHECKED=0
CA_UNREACHABLE=0

# Establish the asset host by following the redirect the firmware follows,
# rather than trusting the list above to still be accurate. If GitHub moves the
# asset to a host that is not listed, add it to the check rather than skipping
# it -- an unlisted host is precisely the one worth verifying.
ASSET_HOST=$(curl -sS -o /dev/null -L --max-redirs 5 -w '%{url_effective}' \
    "$RELEASE_ASSET_URL" 2>/dev/null | sed -E 's#^https?://([^/]+)/.*#\1#') || ASSET_HOST=""
if [[ -n "$ASSET_HOST" && "$ASSET_HOST" != *" "* ]]; then
    if (( ! ${OTA_HOSTS[(I)$ASSET_HOST]} )); then
        echo "NOTE: firmware.bin now downloads from $ASSET_HOST, which is not in"
        echo "      OTA_HOSTS. Checking it anyway and adding it to the list is"
        echo "      overdue -- the asset host has moved before."
        echo ""
        OTA_HOSTS+=("$ASSET_HOST")
        CA_WARN=1
    fi
fi

for HOST in $OTA_HOSTS; do
    CHAIN="$WORK/$HOST.chain"
    if ! run_with_timeout 15 openssl s_client -connect "$HOST:443" -servername "$HOST" \
            -showcerts </dev/null >"$CHAIN" 2>/dev/null || ! grep -q "BEGIN CERTIFICATE" "$CHAIN"; then
        echo "  $HOST  UNREACHABLE -- chain not checked"
        CA_UNREACHABLE=1
        continue
    fi

    N=$(split_pem "$CHAIN" "$WORK/$HOST-c")
    TOP="$WORK/$HOST-c-$N.pem"
    TOP_SPKI=$(spki_of "$TOP")

    MATCH=0
    for S in $PINNED_SPKI; do
        [[ "$S" == "$TOP_SPKI" ]] && MATCH=1
    done

    # The gate proper: replay the device's own decision. Verifying the leaf
    # against the pinned roots as the entire trust store, with the presented
    # chain as untrusted intermediates, is exactly what mbedTLS will do on the
    # C3. A pass here means the handshake closes on a pinned root; a failure
    # means this build cannot fetch from this host, full stop.
    # Everything below the leaf is an untrusted intermediate. Guarded because a
    # host may present the leaf alone, and zsh's {2..1} counts downward.
    cat /dev/null > "$WORK/$HOST.untrusted"
    if (( N > 1 )); then
        for i in {2..$N}; do cat "$WORK/$HOST-c-$i.pem" >> "$WORK/$HOST.untrusted"; done
    fi
    VERIFY_OUT=$(openssl verify -CAfile "$WORK/pinned.pem" -untrusted "$WORK/$HOST.untrusted" \
        "$WORK/$HOST-c-1.pem" 2>&1) && VERIFY_OK=1 || VERIFY_OK=0

    CA_CHECKED=$(( CA_CHECKED + 1 ))
    if (( ! VERIFY_OK )); then
        echo "  $HOST  MISMATCH -- chain does not verify against the pinned roots"
        echo "      presented top-of-chain: $(cn_of "$TOP")"
        echo "      sha256                : $(fp_of "$TOP")"
        echo "      public key sha256     : $TOP_SPKI"
        echo "      openssl: $(echo "$VERIFY_OUT" | tr '\n' ' ')"
        CA_FAIL=1
    elif (( MATCH )); then
        echo "  $HOST  OK -- chains to $(cn_of "$TOP")"
    else
        # Verified, but the host stopped presenting a pinned root at the top.
        # Still working, still worth saying out loud: it usually means a
        # cross-signature changed or the server trimmed its chain, and it is
        # the early signal that a root migration is under way.
        echo "  $HOST  OK -- verifies, but top of chain is not a pinned root"
        echo "      presented top-of-chain: $(cn_of "$TOP")"
        echo "      public key sha256     : $TOP_SPKI"
        CA_WARN=1
    fi
done
echo ""

if (( CA_FAIL )); then
    echo "REFUSING TO RELEASE: a GitHub host no longer chains to a pinned root."
    echo ""
    echo "This firmware carries only the roots listed above. Publishing it would"
    echo "break OTA for the whole fleet at once, and the devices would report it"
    echo "only to serial -- from MQTT they would look like they were ignoring the"
    echo "update, which is exactly the shape that cost hours in issue #0009."
    echo ""
    echo "Add the new root to GITHUB_ROOT_CAS in $SRC (mbedTLS accepts"
    echo "concatenated PEM blocks, so keep the old one alongside it during the"
    echo "migration), rebuild, and push to any already-broken device over the LAN:"
    echo "  ./scripts/ota_local.sh <device>     # plain http, no TLS, no pin"
    exit 1
fi

if (( CA_CHECKED == 0 )); then
    echo "WARNING: could not reach any GitHub host, so the CA gate DID NOT RUN."
    echo "         The pinned roots above are unverified against reality."
    echo ""
    echo "         This is a warning rather than a failure because unreachability"
    echo "         is absence of evidence, not evidence of a mismatch -- and a"
    echo "         machine that cannot reach GitHub cannot publish the release"
    echo "         either, so nothing can ship past this point regardless."
    echo "         Re-run on a connected machine before publishing."
    echo ""
elif (( CA_UNREACHABLE )); then
    echo "WARNING: at least one host was unreachable and went unchecked."
    echo "         The hosts that did answer matched the pinned roots."
    echo ""
else
    echo "CA gate passed. All $CA_CHECKED hosts chain to a pinned root."
    echo ""
    echo "One honest limit: chains can vary by CDN edge and geography, so this is"
    echo "evidence from one vantage point, not proof. It still catches the case"
    echo "that matters -- a vendor-wide root migration."
    echo ""
fi

if (( CA_WARN )); then
    echo "Review the certificate warnings above before publishing."
    echo ""
fi

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
