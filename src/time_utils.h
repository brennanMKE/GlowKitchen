#pragma once

#include <stdint.h>

// Rollover-safe deadline test, shared between the firmware (src/main.cpp)
// and the native test suite (test/test_rollover/test_rollover.cpp). It lives
// in its own header, rather than inline in main.cpp, specifically so the
// test build links against the REAL implementation instead of a hand-copied
// stand-in that could silently drift from what ships.
//
// `deadline < now` looks correct and usually is, but it survives the
// millis() wrap only if the loop samples the clock during the interval
// straddling it. Block across that instant -- ensureWifi() waits up to 30s,
// an OTA check takes seconds -- and the deadline is stranded ~49.7 days in
// the future. That is what froze slowBlend() on four strips (issue #0008).
//
// Unsigned subtraction is well-defined on overflow, so the difference is the
// true elapsed interval across the wrap; reading it as signed answers "has
// the deadline passed?" correctly for any deadline within ~24.8 days of now.
// A blocked loop then costs one late frame instead of a permanent stall.
//
// Fixed-width types are not optional here (issue #0008 follow-up). The
// signed-cast trick only works when the signed and unsigned types are the
// SAME width, because it depends on a negative int32_t and the wrapped
// uint32_t value sharing a bit pattern. On xtensa/riscv32, `unsigned long`
// happens to be 32 bits, so a `(long)(now - deadline)` version built and
// "worked" on the ESP32-C3 -- by coincidence, not by design. On a PlatformIO
// `native` build on macOS/Linux, `unsigned long` is 64 bits: `now + interval`
// never actually wraps at 2^32, the cast becomes a 64-bit comparison, and a
// rollover test written against that signature would pass while exercising
// nothing -- the bug it's meant to catch couldn't even be expressed. Using
// uint32_t/int32_t explicitly makes the width (and therefore the wrap point)
// portable across the device build and the native test build.
static inline bool timeReached(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

// Shared with the SET_CLOCK_OFFSET:<ms> MQTT handler in main.cpp (guarded by
// ENABLE_CLOCK_OFFSET) and with the native test suite in
// test/test_rollover/test_rollover.cpp. Living here, rather than as a
// literal typed separately in each place, means the substring() offset the
// handler uses to skip past this prefix is derived with
// `sizeof(SET_CLOCK_OFFSET_PREFIX) - 1` rather than a hand-typed number that
// could silently drift from the string itself -- this is what actually
// closed the round-2/round-3 off-by-one (issue #0008), not a test asserting
// the number is right.
static const char SET_CLOCK_OFFSET_PREFIX[] = "SET_CLOCK_OFFSET:";
