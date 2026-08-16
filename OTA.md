# Over-the-Air (OTA) Firmware Updates

This document describes how GlowKitchen devices update their firmware wirelessly
from GitHub Releases, so code changes no longer require a physical USB upload to
each device.

## Summary of decisions

| Decision | Choice |
|----------|--------|
| Firmware host | Public GitHub repo `brennanMKE/GlowKitchen`, Releases |
| Published asset | A single `firmware.bin` (no separate checksum file) |
| Update trigger | Nightly self-check at **03:00 local time** (NTP-synced) |
| Manual trigger | `OTA_UPDATE` MQTT command (for testing / urgent pushes) |
| Release process | Created **manually** on GitHub (no CI) |
| Transport security | Authenticated HTTPS via the **Mozilla CA bundle** (`setCACertBundle`) |
| Time zone | US Central (`America/Chicago`), DST-aware |

## Why only `firmware.bin` (no checksum)

Integrity against corruption is already verified at three layers, so a separately
published checksum would be redundant:

1. **Content-Length** — `httpUpdate` rejects truncated/partial downloads.
2. **Embedded SHA-256** — ESP32 app images carry an appended SHA-256 that the
   **bootloader verifies on every boot**; a corrupted flash will not run.
3. **Image magic-byte (`0xE9`) check** in `Update.end()` before committing the slot.

A checksum file would not add *authenticity* (anyone who can swap the `.bin` can
swap the checksum). Authenticity is instead provided by validating GitHub's TLS
certificate chain against the bundled Mozilla CA store (`setCACertBundle`), which
defeats a man-in-the-middle on the local network without the complexity of signed
OTA. Release downloads redirect to a separate CDN host
(`*.githubusercontent.com` / CDN), so pinning a single GitHub cert is brittle —
the CA bundle validates the whole chain across the redirect.

## Partition note

No repartitioning is required. The board-default partition table already has two
app slots (`app0` + `app1`, 1.25 MB each). OTA flashes the *inactive* slot and
reboots into it. The current image (~792 KB) fits comfortably; OTA adds ~50–100 KB
for TLS + HTTP-update, still well within the ~500 KB of free app space.

> Trade-off: keeping OTA means we cannot switch to a `huge_app`/no-OTA partition
> for more code space. With ~40% headroom today this is fine.

## Publishing a release (manual workflow)

1. Bump `FIRMWARE_VERSION` in `src/main.cpp` (e.g. `"1.1.0"`).
2. Build: `pio run -e esp32c3` → output at `.pio/build/esp32c3/firmware.bin`.
3. On GitHub → **Releases → Draft a new release**:
   - Tag: `v1.1.0` (must match `FIRMWARE_VERSION`, with a leading `v`).
   - Upload `.pio/build/esp32c3/firmware.bin` as an asset named exactly `firmware.bin`.
   - Publish (mark as "latest").

Stable URLs the devices use:

- Latest version (tag): `https://api.github.com/repos/brennanMKE/GlowKitchen/releases/latest` (`tag_name` field)
- Firmware binary: `https://github.com/brennanMKE/GlowKitchen/releases/latest/download/firmware.bin`

> First rollout is still a one-time USB upload of this OTA-capable firmware.
> After that, all future updates are wireless.

## Device-side behavior

### Versioning
- `FIRMWARE_VERSION` string constant compiled into the firmware.
- Reported in the `STATUS` MQTT payload as `"firmwareVersion"`, so each device's
  running version is visible over MQTT.

### Time sync (NTP)
- After WiFi connects, configure time once with `configTzTime()` using the POSIX
  TZ string for US Central with DST rules: `CST6CDT,M3.2.0,M11.1.0/2`.
- NTP servers: `pool.ntp.org`, `time.nist.gov`.
- The nightly check only runs once the clock is valid (`tm_year >= 2020`).

### Nightly scheduler (03:00 local)
- Non-blocking check in `loop()`. When local time is in the 03:00 hour and the
  check has not already run "today", run the OTA check.
- Persist the last-run day in Preferences (`ota_last_yday`) so a post-update
  reboot near 03:00 does not re-trigger an update loop.

### OTA check + apply
1. `WiFiClientSecure` with `setCACertBundle(...)` (Mozilla bundle).
2. HTTPS GET the GitHub API `releases/latest`, sending a `User-Agent` header
   (GitHub rejects requests without one). Parse `tag_name` with a lightweight
   string scan — **no ArduinoJson dependency**.
3. If `tag_name` (sans leading `v`) differs from `FIRMWARE_VERSION`, run
   `httpUpdate.update(client, ".../releases/latest/download/firmware.bin")` with
   redirect-following enabled (`setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS)`).
4. On success the library reboots into the new slot. On failure it logs and
   leaves the running firmware untouched (no auto-rollback in v1 — a bricked
   device is recovered via USB).

### Manual trigger (MQTT)
- `OTA_UPDATE` (or `OTA_CHECK`) published to `lights/<device>/cmd` or
  `lights/all/cmd` forces an immediate check using the same code path. Useful for
  validating the pipeline without waiting until 03:00.

## Local development pushes (no GitHub release)

Cutting a release updates the whole fleet, which is far too coarse for iterating
on one strip. `OTA_URL:` installs a build straight off a machine on the LAN:

```bash
pio run                                                  # build
./scripts/configure_device.sh kitchen ota_auto false     # pin it first (see below)
./scripts/ota_local.sh kitchen                           # serve + push + watch
```

`ota_local.sh` finds the LAN address of whichever interface reaches the broker,
serves `.pio/build/esp32c3/` over plain HTTP, publishes
`OTA_URL:http://<ip>:8000/firmware.bin` to that one device, then tails its state
topic for the new version and shuts the server down.

**Pin the device first.** A local build reports a version that differs from the
current release tag, and the version check is equality-based, not newer-only —
so an unpinned device downgrades itself back to the released build about 15
seconds after its next boot. `OTA_AUTO:false` suspends the startup and nightly
checks; `OTA_UPDATE` and `OTA_URL` still work, so the device stays reachable.
The flag is persisted in NVS and reported as `otaAuto` in the state payload.
Restore normal behavior with `./scripts/configure_device.sh kitchen ota_auto true`.

Two guards apply to `OTA_URL` because it installs an **unsigned** binary with no
version check:

- It is refused on `lights/all/cmd` — single-device topics only.
- The host must be demonstrably local: a `.local` mDNS name, an address on the
  device's own subnet, or an RFC1918 private range. Public addresses and bare DNS
  names are rejected (resolving a bare name to decide would be precisely the
  lookup an attacker controls).

## New dependencies / includes

- `HTTPClient.h`, `HTTPUpdate.h`, `WiFiClientSecure.h`, `time.h`, `esp_crt_bundle.h`
- No new `lib_deps` entries (all part of the Arduino-ESP32 core).

## Out of scope for v1 (possible follow-ups)

- Cryptographically **signed OTA** (secure boot / signed app images).
- Automatic **rollback** validation via `esp_ota` mark-valid on successful boot.
- Staggered/jittered check times across devices (only a few devices, so 03:00 is fine).
