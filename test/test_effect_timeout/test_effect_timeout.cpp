// Native (host-compiled) tests for src/effect_timeout.h (issue #0017): the
// live expiry predicate, the STATUS remaining-seconds arithmetic, and (half
// B) the reboot resume decision table and its clamp. Run with
// `pio test -e native`.
//
// Sibling directory to test/test_effect_parse/ and test/test_effects/, not a
// second .cpp inside either: all .cpp files within one PlatformIO test
// subdirectory link into one binary, and src/themes.h's THEME_NAMES[] has
// external linkage -- a second TU beside an existing one is a duplicate-
// symbol link error (issue #0014's Gotchas). A sibling directory sidesteps
// it and keeps a timeout regression from reading as a parser or renderer
// regression.
//
// Include-order contract (transitively inherited via effect_timeout.h ->
// effects.h -> themes.h): fastled_shim.h MUST come first, exactly as
// test_effect_parse.cpp and test_effects.cpp do. Do not move or edit
// fastled_shim.h -- earlier harnesses depend on it and must keep passing
// untouched.
#include <unity.h>
#include <stdint.h>
#include "../test_effects/fastled_shim.h"
#include "effects.h"
#include "effect_timeout.h"

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------
// A. effectTimeoutExpired() -- half A
// ---------------------------------------------------------------------

// 1
void test_case01_non_custom_never_expires(void) {
    TEST_ASSERT_FALSE(effectTimeoutExpired(false, 1000u, 0u, 999999u));
}

// 2
void test_case02_zero_timeout_runs_until_changed(void) {
    TEST_ASSERT_FALSE(effectTimeoutExpired(true, 0u, 0u, 0u));
    TEST_ASSERT_FALSE(effectTimeoutExpired(true, 0u, 0u, 0xFFFFFFFFu));
}

// 3, 4, 5 -- the exact boundary
void test_case03_04_05_boundary(void) {
    const uint32_t activatedAt = 1000u, timeoutMs = 30000u;
    TEST_ASSERT_FALSE_MESSAGE(effectTimeoutExpired(true, timeoutMs, activatedAt, 30999u),
                               "case 3: one ms early");
    TEST_ASSERT_TRUE_MESSAGE(effectTimeoutExpired(true, timeoutMs, activatedAt, 31000u),
                              "case 4: exact boundary");
    TEST_ASSERT_TRUE_MESSAGE(effectTimeoutExpired(true, timeoutMs, activatedAt, 31001u),
                              "case 5: one ms late");
}

// 6, 7 -- rollover, unblocked-style: now sampled right at/just before the
// wrapped deadline.
void test_case06_07_rollover_boundary(void) {
    const uint32_t activatedAt = 0xFFFFF000u, timeoutMs = 30000u;
    // deadline = activatedAt + timeoutMs wraps to 25904 (mod 2^32).
    uint32_t nowJustBefore = (uint32_t)(activatedAt + 29999u); // wraps to 25903
    uint32_t nowAtBoundary = (uint32_t)(activatedAt + 30000u); // wraps to 25904
    TEST_ASSERT_FALSE_MESSAGE(effectTimeoutExpired(true, timeoutMs, activatedAt, nowJustBefore),
                               "case 6: rollover, one ms before the wrapped deadline");
    TEST_ASSERT_TRUE_MESSAGE(effectTimeoutExpired(true, timeoutMs, activatedAt, nowAtBoundary),
                              "case 7: rollover, exact wrapped boundary");
}

// 8 -- the genuine #0008 rollover shape, corrected in round 2 of this ticket's
// review. The ORIGINAL fixture here (activatedAt = 0xFFFFF000, timeoutMs =
// 30000) made `deadline = activatedAt + timeoutMs` itself wrap (to 25904), so
// both `now` and `deadline` ended up small with no straddle between them --
// every naive `deadline < now`/`deadline <= now` substitution agreed with
// timeReached() on that fixture, which means the case could never fail no
// matter how badly effectTimeoutExpired() was broken. It was decorative, not
// a rollover test, and round 1's `## Verification` wrongly counted it as
// having caught something (see issues/0017.md for the correction).
//
// The real #0008 shape needs an UNWRAPPED deadline with a WRAPPED `now`:
// activatedAt is chosen far enough from the 2^32 wrap that adding timeoutMs
// does not cross it, but adding a later, larger elapsed time (a blocked loop
// sampling `now` well after the deadline) does.
//   activatedAt = 0xFFFF0000, timeoutMs = 30000
//     -> deadline = activatedAt + timeoutMs = 4,294,931,760   (does NOT wrap)
//   now = activatedAt + 90000
//     -> now (mod 2^32)                     = 24,464          (DOES wrap)
// timeReached(now, deadline) is TRUE (correct: the effect is overdue), but
// both `deadline < now` and `deadline <= now` read as FALSE, since as plain
// uint32_t values deadline (~4.29e9) is far larger than now (24,464) -- the
// wrap that makes `now` small is exactly what a naive comparison misses.
// This is proven by actual perturbation, not asserted; see issues/0017.md's
// `## Verification` for the pasted output of both naive substitutions.
void test_case08_blocked_loop_rollover(void) {
    const uint32_t activatedAt = 0xFFFF0000u, timeoutMs = 30000u;
    // deadline = 4294931760, does not wrap.
    uint32_t nowAfterBlockedWrap = (uint32_t)(activatedAt + 90000u); // wraps to 24464
    TEST_ASSERT_TRUE_MESSAGE(effectTimeoutExpired(true, timeoutMs, activatedAt, nowAfterBlockedWrap),
                              "case 8: unwrapped deadline, wrapped now -- must still fire (late)");
}

// 9 -- the max legal timeout (issue #0016's 28,800,000 ms clamp).
void test_case09_max_legal_timeout_boundary(void) {
    const uint32_t timeoutMs = 28800000u;
    TEST_ASSERT_FALSE(effectTimeoutExpired(true, timeoutMs, 0u, 28799999u));
    TEST_ASSERT_TRUE(effectTimeoutExpired(true, timeoutMs, 0u, 28800000u));
}

// ---------------------------------------------------------------------
// B. effectTimeoutRemainingSeconds() -- half A
// ---------------------------------------------------------------------

// 10
void test_case10_no_timeout_sentinel(void) {
    TEST_ASSERT_EQUAL_INT32(-1, effectTimeoutRemainingSeconds(0u, 0u, 0u));
}

// 11, 12 -- ceil, not floor.
void test_case11_12_ceil_rounding(void) {
    const uint32_t timeoutMs = 1800000u, activatedAt = 1000u;
    TEST_ASSERT_EQUAL_INT32_MESSAGE(1800, effectTimeoutRemainingSeconds(timeoutMs, activatedAt, 1000u),
                                     "case 11: full value reported immediately");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(1800, effectTimeoutRemainingSeconds(timeoutMs, activatedAt, 1500u),
                                     "case 12: ceil(1799.5) == 1800");
}

// 13, 14, 15
void test_case13_14_15_tail(void) {
    const uint32_t timeoutMs = 1800000u, activatedAt = 1000u;
    TEST_ASSERT_EQUAL_INT32(1, effectTimeoutRemainingSeconds(timeoutMs, activatedAt, activatedAt + 1799001u));
    TEST_ASSERT_EQUAL_INT32(0, effectTimeoutRemainingSeconds(timeoutMs, activatedAt, activatedAt + 1800000u));
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, effectTimeoutRemainingSeconds(timeoutMs, activatedAt, activatedAt + 5000000u),
                                     "case 15: well past the deadline, never negative");
}

// 16 -- rollover.
void test_case16_rollover_remaining(void) {
    const uint32_t activatedAt = 0xFFFFF000u, timeoutMs = 30000u;
    uint32_t now = (uint32_t)(activatedAt + 10000u); // wraps
    TEST_ASSERT_EQUAL_INT32(20, effectTimeoutRemainingSeconds(timeoutMs, activatedAt, now));
}

// 17 -- the #0016 clamp ceiling, visible in STATUS.
void test_case17_max_timeout_visible_in_status(void) {
    TEST_ASSERT_EQUAL_INT32(28800, effectTimeoutRemainingSeconds(28800000u, 0u, 0u));
}

// ---------------------------------------------------------------------
// C. effectResumeDecision() -- half B, boot decision table
// ---------------------------------------------------------------------

// 18
void test_case18_resume(void) {
    TEST_ASSERT_EQUAL(RESUME, effectResumeDecision(true, 1000u, 2000u));
}

// 19, 20 -- expired, at and past the boundary.
void test_case19_20_revert_expired(void) {
    TEST_ASSERT_EQUAL_MESSAGE(REVERT_EXPIRED, effectResumeDecision(true, 2000u, 2000u),
                               "case 19: exact boundary counts as expired");
    TEST_ASSERT_EQUAL_MESSAGE(REVERT_EXPIRED, effectResumeDecision(true, 2001u, 2000u),
                               "case 20: past the boundary");
}

// 21 -- clock invalid wins even over an end time that has not passed.
void test_case21_no_clock_wins_over_unexpired_end(void) {
    TEST_ASSERT_EQUAL(REVERT_NO_CLOCK, effectResumeDecision(false, 500u, 2000u));
}

// 22 -- set while unsynced (fx_end == 0).
void test_case22_no_end_when_clock_valid(void) {
    TEST_ASSERT_EQUAL(REVERT_NO_END, effectResumeDecision(true, 1000u, 0u));
}

// 23 -- both conditions; clock-invalid is pinned to win.
void test_case23_no_clock_and_no_end(void) {
    TEST_ASSERT_EQUAL(REVERT_NO_CLOCK, effectResumeDecision(false, 1000u, 0u));
}

// ---------------------------------------------------------------------
// D. resumeTimeoutMs() -- half B, resume clamp
// ---------------------------------------------------------------------

// 24
void test_case24_basic_resume(void) {
    TEST_ASSERT_EQUAL_UINT32(30000u, resumeTimeoutMs(1000u, 1030u));
}

// 25 -- at and past the end time.
void test_case25_at_or_past_end(void) {
    TEST_ASSERT_EQUAL_UINT32(0u, resumeTimeoutMs(1000u, 1000u));
    TEST_ASSERT_EQUAL_UINT32(0u, resumeTimeoutMs(1000u, 999u));
}

// 26, 27 -- the clamp boundary.
void test_case26_27_clamp_boundary(void) {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(28800000u, resumeTimeoutMs(1000u, 1000u + 28800u),
                                      "case 26: exactly at the ceiling, unclamped");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(28800000u, resumeTimeoutMs(1000u, 1000u + 28801u),
                                      "case 27: one second over, clamped");
}

// 28 -- no uint32_t overflow. Without the clamp, secs*1000 wraps.
void test_case28_no_overflow(void) {
    TEST_ASSERT_EQUAL_UINT32(28800000u, resumeTimeoutMs(0u, 0xFFFFFFFFu));
}

// ---------------------------------------------------------------------
// E. Realistic scenarios, end to end through the pure functions.
// ---------------------------------------------------------------------

// 29 -- a short effect that fully expired while the device was down.
void test_case29_short_effect_expires_across_reboot(void) {
    const uint32_t startEpoch = 1000u, endEpoch = startEpoch + 30u; // 30s effect
    const uint32_t nowEpoch = startEpoch + 90u; // device down long enough to pass it
    TEST_ASSERT_EQUAL(REVERT_EXPIRED, effectResumeDecision(true, nowEpoch, endEpoch));
}

// 30 -- a long effect that should resume with the correct remainder.
void test_case30_long_effect_resumes_with_correct_remainder(void) {
    const uint32_t startEpoch = 1000u;
    const uint32_t endEpoch = startEpoch + 28800u;   // 8h effect
    const uint32_t nowEpoch = startEpoch + 60u;      // ~60s of real time elapsed
    TEST_ASSERT_EQUAL(RESUME, effectResumeDecision(true, nowEpoch, endEpoch));
    uint32_t resumed = resumeTimeoutMs(nowEpoch, endEpoch);
    TEST_ASSERT_UINT32_WITHIN(1000u, 28740000u, resumed); // +-1s
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_case01_non_custom_never_expires);
    RUN_TEST(test_case02_zero_timeout_runs_until_changed);
    RUN_TEST(test_case03_04_05_boundary);
    RUN_TEST(test_case06_07_rollover_boundary);
    RUN_TEST(test_case08_blocked_loop_rollover);
    RUN_TEST(test_case09_max_legal_timeout_boundary);
    RUN_TEST(test_case10_no_timeout_sentinel);
    RUN_TEST(test_case11_12_ceil_rounding);
    RUN_TEST(test_case13_14_15_tail);
    RUN_TEST(test_case16_rollover_remaining);
    RUN_TEST(test_case17_max_timeout_visible_in_status);
    RUN_TEST(test_case18_resume);
    RUN_TEST(test_case19_20_revert_expired);
    RUN_TEST(test_case21_no_clock_wins_over_unexpired_end);
    RUN_TEST(test_case22_no_end_when_clock_valid);
    RUN_TEST(test_case23_no_clock_and_no_end);
    RUN_TEST(test_case24_basic_resume);
    RUN_TEST(test_case25_at_or_past_end);
    RUN_TEST(test_case26_27_clamp_boundary);
    RUN_TEST(test_case28_no_overflow);
    RUN_TEST(test_case29_short_effect_expires_across_reboot);
    RUN_TEST(test_case30_long_effect_resumes_with_correct_remainder);
    return UNITY_END();
}
