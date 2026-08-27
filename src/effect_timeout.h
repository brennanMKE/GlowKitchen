#pragma once

// Issue #0017: pure, native-testable logic for acting on the custom effect
// timeout that issue #0016 parses, clamps and persists -- live expiry
// (section 2), remaining-seconds reporting for STATUS (section 4), and the
// reboot resume decision table plus its clamp (section 6, half B). Same
// header-only rationale as effects.h/effect_parse.h/time_utils.h:
// [env:native] does not build project sources (test_build_src is off), so a
// .cpp here could not be linked from a native test without dragging
// main.cpp/WiFi.h/FastLED.h into the host build. [env:native] already
// carries -I src.
//
// INCLUDE-ORDER CONTRACT (inherited transitively via effects.h): effects.h
// uses CRGB/CHSV/sin8/min/max WITHOUT including <FastLED.h> itself. Any
// translation unit that includes this header must already have those names
// in scope -- main.cpp via `#include <FastLED.h>` at the top of the file, a
// native test via test/test_effects/fastled_shim.h included FIRST.
//
// Forbidden in this file, and why (issue #0008's whole point, restated):
//   - `deadline < now` -- looks correct but only survives the millis() wrap
//     if the loop samples the clock during the interval straddling it. A
//     blocked loop (ensureWifi()'s 30s wait, an OTA check, mqtt.connect())
//     strands the deadline ~49.7 days in the future.
//   - EVERY_N_MILLISECONDS -- a FastLED macro that wraps millis() itself,
//     which would silently opt this path out of the nowMs() clock-offset
//     bench harness.
//   - `unsigned long` anywhere in this path -- 64 bits on a native host, 32
//     on the C3 (see src/time_utils.h's type-width note). Fixed-width
//     uint32_t/int32_t only.

#include <stdint.h>
#include "effects.h"
#include "time_utils.h"

// ---------------------------------------------------------------------
// Half A -- live expiry and STATUS remaining-seconds (issue #0017 sections
// 2 and 4).
// ---------------------------------------------------------------------

// True when an active custom effect's timeout has elapsed. `isCustom` is
// passed in rather than read off the currentTheme global so this stays a
// pure function the native env can call without linking main.cpp.
//
// The spec writes the elapsed form `millis() - activatedAt >= timeoutMs`.
// `timeReached(now, activatedAt + timeoutMs)` is arithmetically identical to
// that for any timeoutMs < 2^31 (both reduce to the same unsigned
// subtraction read as signed) -- timeoutMs is capped at 28,800,000 by issue
// #0016, four orders of magnitude inside that bound. Routing it through
// timeReached() keeps exactly one deadline idiom in this codebase, which is
// the entire point of issue #0008's fix. The unsigned `activatedAt +
// timeoutMs` addition wrapping is the mechanism, not a bug.
inline bool effectTimeoutExpired(bool isCustom, uint32_t timeoutMs,
                                  uint32_t activatedAt, uint32_t now) {
    if (!isCustom || timeoutMs == 0) return false;
    return timeReached(now, activatedAt + timeoutMs);
}

// Seconds remaining on an active custom effect's timeout, for STATUS's
// "custom" block.
//   -1  -> no timeout ("runs until changed") -- the pinned sentinel (issue
//          #0017 section 4.2). 0 is ambiguous with "about to expire" with no
//          way for a client to tell the two apart; null forces a type check
//          on every shell-script consumer; an absent key makes the field's
//          presence itself carry meaning. -1 is a single always-present
//          integer, unambiguously outside the legal 0-28800 range.
//   0..  -> whole seconds remaining, rounded UP (ceil, not floor),
//          deliberately: a freshly-set `timeout:1800` must report 1800 the
//          instant it is set, not 1799 -- floor would look like the #0016
//          clamp silently shaved a second off, which is the exact confusion
//          this field exists to prevent. 0 is reachable in practice only in
//          the sub-loop-iteration window where a STATUS races the revert.
// Computed from the millis()-domain clock (via `now`), never the wall
// clock, even once half B lands -- the wall clock may be unsynced while an
// effect runs perfectly well (a session-started effect on a device that
// never joined WiFi), and this answers "how much longer will this actually
// run", which is what the millis() countdown governs. Half B's epoch only
// seeds how the countdown starts at boot.
inline int32_t effectTimeoutRemainingSeconds(uint32_t timeoutMs,
                                              uint32_t activatedAt, uint32_t now) {
    if (timeoutMs == 0) return -1;
    uint32_t elapsed = now - activatedAt;   // wraps correctly
    if (elapsed >= timeoutMs) return 0;
    return (int32_t)((timeoutMs - elapsed + 999) / 1000);
}

// ---------------------------------------------------------------------
// Half B -- reboot resume semantics (issue #0017 section 6).
// ---------------------------------------------------------------------

enum EffectResumeDecision { RESUME, REVERT_EXPIRED, REVERT_NO_CLOCK, REVERT_NO_END };

// The boot decision table, expressed as a named pure function so the table
// is testable as a table. `clockValid` is tested FIRST and wins outright
// over everything else, including an end time that has not yet passed
// (issue #0013, "Surviving a reboot": a wrong guess about a timed effect
// while the wall clock might be lying is worse than dropping it). `endEpoch
// == 0` means the effect was set while the clock was unsynced (issue #0017
// section 6.3) and is therefore never resumable, independent of the current
// clock state once it does sync.
inline EffectResumeDecision effectResumeDecision(bool clockValid, uint32_t nowEpoch,
                                                  uint32_t endEpoch) {
    if (!clockValid) return REVERT_NO_CLOCK;
    if (endEpoch == 0) return REVERT_NO_END;
    if (nowEpoch >= endEpoch) return REVERT_EXPIRED;
    return RESUME;
}

// Clamped remaining-time-in-ms for a resumed effect. The clamp is
// defensive, not decorative: a corrupt fx_end, or a clock that synced to a
// wrong year and then corrected, can produce an arbitrarily far-future end
// time, and without the clamp `secs * 1000` overflows uint32_t above ~49.7
// days, yielding a short or negative-looking remainder. Clamping to the
// same 28,800s ceiling issue #0016 enforces on input means a resumed effect
// can never outlive a freshly-set one.
inline uint32_t resumeTimeoutMs(uint32_t nowEpoch, uint32_t endEpoch) {
    if (endEpoch <= nowEpoch) return 0;
    uint32_t secs = endEpoch - nowEpoch;
    if (secs > 28800UL) secs = 28800UL;
    return secs * 1000UL;
}
