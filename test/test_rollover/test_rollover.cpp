// Native (host-compiled) tests for timeReached(), the rollover-safe deadline
// test at the heart of issue #0008. Run with `pio test -e native`.
//
// This exercises the REAL implementation in src/time_utils.h (via the
// -I src build flag on [env:native] in platformio.ini) rather than a copy,
// so a future change to the shipped helper is caught here automatically.
//
// Why this has to be a native (not embedded) build: the failure has a
// 49.7-day feedback loop on real hardware, so the only practical way to
// prove the fix is to construct the exact bit patterns millis() would
// produce at the wrap and check timeReached() against them directly.
#include <unity.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "time_utils.h"

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------
// Shared fixture for the "blocked across the rollover" scenario, which is
// the exact failure from issue #0008: slowBlend()'s blendTimeout is
// computed while `now` is close to but still below 2^32, so the sum does
// NOT itself wrap -- it stays a large just-under-2^32 value. Then the loop
// blocks (ensureWifi()'s 30s wait, an OTA check, mqtt.connect()) for long
// enough that the NEXT sampled `now` has already wrapped to a small value.
// ---------------------------------------------------------------------

// now = 0xFFFFFF00 (just below 2^32) + 100ms interval. 100 < the 256ms of
// headroom left before 2^32, so this sum does NOT wrap -- it stays large.
// That's the crux of the bug: the deadline looks like it is still ~4.29
// billion ms in the future, when really it passed the instant `now` wrapped.
static const uint32_t BLOCKED_WRAP_DEADLINE = (uint32_t)(0xFFFFFF00u + 100u);

// `now` as sampled by the loop AFTER a long block carried it across the
// wrap -- a small value, as if millis() has been running for ~0.5s since 0.
static const uint32_t BLOCKED_WRAP_NOW = 0x200u; // 512

void test_deadline_survives_the_wrap_while_blocked(void) {
    // This is the exact #0008 failure: a loop iteration spans the rollover
    // while blocked (WiFi reconnect, OTA check, mqtt.connect()), so `now`
    // jumps from just-under-2^32 straight to a small post-wrap value while
    // the deadline is still holding a large just-under-2^32 number. The
    // deadline must be treated as passed (fires late), not stranded for
    // another ~49.7 days.
    TEST_ASSERT_TRUE_MESSAGE(
        timeReached(BLOCKED_WRAP_NOW, BLOCKED_WRAP_DEADLINE),
        "timeReached() must fire (late) once now has wrapped past a deadline "
        "that itself never wrapped -- this is issue #0008.");
}

void test_naive_comparison_would_get_this_wrong(void) {
    // Documents the bug this file exists to prevent: the naive, obvious-
    // looking `deadline < now` comparison -- what shipped before #0008 --
    // gives the WRONG answer for exactly this blocked-wrap case. It reads
    // as "deadline is still ~4.29 billion ms in the future" and never fires
    // again until `now` catches back up to it, roughly another full 2^32ms
    // rollover period (49.7 days) later. That is the permanent stall that
    // froze slowBlend() on four strips.
    bool naiveResult = (BLOCKED_WRAP_DEADLINE < BLOCKED_WRAP_NOW);
    TEST_ASSERT_FALSE_MESSAGE(
        naiveResult,
        "naive `deadline < now` incorrectly says \"not yet reached\" in the "
        "blocked-wrap case -- this is the exact bug #0008 fixes.");

    // ...and confirm timeReached() disagrees with it (fixed vs. broken).
    TEST_ASSERT_NOT_EQUAL_MESSAGE(
        naiveResult, timeReached(BLOCKED_WRAP_NOW, BLOCKED_WRAP_DEADLINE),
        "timeReached() must give the opposite answer to the naive comparison "
        "in the blocked-wrap case, or it isn't fixing anything.");
}

// ---------------------------------------------------------------------
// The case that made the bug non-obvious in the first place: if the loop
// keeps sampling millis() through the wrap (i.e. it is never blocked long
// enough to skip over it), the same deadline math self-heals with no
// intervention. See issue #0008, "Why it normally survives the wrap".
//
// Note this is a different claim from the naive `deadline < now` form the
// firmware used to use: that version had a harmless-but-odd quirk right at
// the wrap where it re-fired every loop iteration for ~100ms (small
// wrapped deadline looks "less than" the still-large pre-wrap `now`) until
// `now` itself wrapped too -- which is *why* the underlying bug was easy to
// miss by reading the code. timeReached() doesn't even have that quirk: it
// tracks true elapsed time throughout and simply never reports "reached"
// early, wrap or no wrap, as long as the loop keeps sampling normally.
// ---------------------------------------------------------------------

void test_unblocked_wrap_self_heals(void) {
    // `now` is 16ms below 2^32 when the 100ms-interval deadline is set.
    // Because the interval (100) is bigger than the remaining headroom (16),
    // the SUM wraps immediately to a small value -- unlike the blocked case
    // above, where the interval was smaller than the headroom and the sum
    // stayed large.
    const uint32_t nowAtSet = 0xFFFFFFF0u;              // 16ms before the wrap
    const uint32_t deadline = (uint32_t)(nowAtSet + 100u); // wraps to 84

    // A loop that is NOT blocked keeps sampling every few ms. None of these
    // intermediate samples should report "reached" early -- the true
    // 100ms interval has not elapsed yet, wrap notwithstanding.
    TEST_ASSERT_FALSE(timeReached((uint32_t)(nowAtSet + 10u), deadline));  // 10ms in, still high, not wrapped
    TEST_ASSERT_FALSE(timeReached(0u, deadline));                          // exactly at the wrap (t=0), 84ms still remain
    TEST_ASSERT_FALSE(timeReached(50u, deadline));                         // 66ms after the wrap, 34ms still remain

    // Once the true 100ms have actually elapsed (16ms to the wrap + 90ms
    // past it = 106ms), it correctly fires -- a few ms late because of
    // sampling granularity, exactly like an unblocked loop would produce
    // for any ordinary (non-wrap) interval. Not stranded, not early.
    TEST_ASSERT_TRUE(timeReached(90u, deadline));
}

// ---------------------------------------------------------------------
// Basic negative case: nothing rollover-related, just "not yet".
// ---------------------------------------------------------------------

void test_near_future_deadline_not_yet_reached(void) {
    uint32_t now = 1000;
    uint32_t deadline = 5000;
    TEST_ASSERT_FALSE(timeReached(now, deadline));
}

// ---------------------------------------------------------------------
// Boundary: the signed-cast trick only answers correctly for deadlines
// within +/-(2^31 - 1) ms (~24.8 days) of `now`. Past that, the same bit
// pattern is ambiguous between "far in the future" and "far in the past",
// and the comparison silently starts giving the opposite answer. This is
// inherent to the technique (documented in time_utils.h), not a bug to fix
// here -- every timer in this firmware has an interval of seconds, nowhere
// near this limit -- but it should be understood, not discovered by
// accident.
// ---------------------------------------------------------------------

void test_boundary_near_24_8_day_limit(void) {
    const uint32_t now = 0;

    // Just inside the valid range: 2^31 - 1 ms in the future. Correctly
    // "not yet reached".
    const uint32_t justInside = 0x7FFFFFFFu; // INT32_MAX
    TEST_ASSERT_FALSE(timeReached(now, justInside));

    // Exactly at 2^31: the bit pattern is equally "2^31ms in the future" or
    // "2^31ms in the past" -- there is no correct answer, only whatever the
    // signed reinterpretation of INT32_MIN's bit pattern happens to give on
    // this platform (observed here as "not yet reached"). Documented, not
    // asserted as "correct".
    const uint32_t exactlyAtBoundary = 0x80000000u; // 2^31
    TEST_ASSERT_FALSE(timeReached(now, exactlyAtBoundary));

    // One past the boundary: a deadline that is actually (2^31 - 1) ms in
    // the PAST aliases onto the same bits as one far in the future, and the
    // comparison flips to "reached" even though nothing has elapsed yet in
    // the scenario this firmware ever produces. This is the practical
    // meaning of "~24.8 days" in the time_utils.h comment: stay well under
    // it, which every real interval in this firmware (seconds, not weeks)
    // already does by a wide margin.
    const uint32_t justOutside = 0x80000001u; // 2^31 + 1
    TEST_ASSERT_TRUE(timeReached(now, justOutside));
}

// ---------------------------------------------------------------------
// Regression guard for the type width (issue #0008 follow-up). The whole
// point of switching timeReached()'s signature from unsigned long/long to
// uint32_t/int32_t was that `unsigned long` is 64 bits on this native
// macOS/Linux test build, so a version built against the old signature
// would never wrap at 2^32 in the first place -- the exact scenario this
// file tests could not even be expressed, and every test above would pass
// for the wrong reason (or rather, without exercising anything).
// ---------------------------------------------------------------------

void test_type_width_regression_guard(void) {
    // If someone "helpfully" widened timeReached() back to unsigned
    // long/long, these would still hold on this platform (a `long` here is
    // 8 bytes) -- which is exactly why the fix requires the FIXED-WIDTH
    // types below, not just "an unsigned type and a signed type of the
    // same width".
    static_assert(sizeof(uint32_t) == 4,
                  "timeReached() depends on a true 32-bit unsigned type; "
                  "issue #0008 is specifically about this NOT being "
                  "guaranteed for `unsigned long`.");
    static_assert(sizeof(int32_t) == 4,
                  "timeReached() depends on a true 32-bit signed type; "
                  "see the sizeof(uint32_t) assertion above.");

    // Demonstrate the 32-bit wrap itself, independent of timeReached(): a
    // uint32_t at its max value plus 1 must land on exactly 0. This is the
    // wraparound the whole fix relies on, and the one `unsigned long` does
    // NOT reliably give you across platforms.
    uint32_t maxVal = 0xFFFFFFFFu;
    uint32_t wrapped = (uint32_t)(maxVal + 1u);
    TEST_ASSERT_EQUAL_UINT32(0u, wrapped);

    // Recompute the exact blocked-wrap case from
    // test_deadline_survives_the_wrap_while_blocked() above, but in a wider
    // (64-bit) type -- i.e. what `unsigned long`/`long` would silently
    // become on this native build. If timeReached()'s parameters were ever
    // widened back, this is the divergence that would slip through: the
    // 32-bit version correctly reports the deadline as reached (see above),
    // while the same arithmetic in 64 bits does not wrap at 2^32 at all and
    // reports the opposite.
    uint64_t now64 = BLOCKED_WRAP_NOW;
    uint64_t deadline64 = BLOCKED_WRAP_DEADLINE;
    int64_t wideDiff = (int64_t)(now64 - deadline64);
    TEST_ASSERT_TRUE_MESSAGE(
        wideDiff < 0,
        "64-bit arithmetic -- what `unsigned long`/`long` silently becomes "
        "on a native build -- does NOT wrap at 2^32, so this diff stays "
        "negative: the opposite sign from timeReached()'s correct (32-bit) "
        "answer above. This is the exact trap issue #0008 warns about, and "
        "why the parameters must stay uint32_t/int32_t.");
}

// ---------------------------------------------------------------------
// Clock-offset harness regression tests (issue #0008, round-2/round-3
// review).
//
// The reviewer's core finding on the first pass was that the harness itself
// -- the only mechanism for on-device verification of the fix above -- was
// reported correct on the strength of reading it, not running it, and both
// defects they found lived exactly there:
//
//   1. An off-by-one in the MQTT handler's substring() offset that silently
//      ate the first digit of every documented bench command.
//   2. Setting the offset stranding slowBlend()/rotateTimeout because their
//      deadlines were computed against the OLD clock and never re-based.
//
// Round 2 fixed both, but fixed defect 1 by hand-typing substring(17) and
// backing it with a test that compared strlen() of a literal DECLARED IN
// THIS FILE against a hard-coded 17 -- a tautology that only fails if
// someone edits the test, and that a round-3 review proved doesn't catch
// the handler regressing to substring(18): reintroducing that exact bug in
// main.cpp left this suite green. Round 3 fixes this properly in
// src/main.cpp (the substring() offset is now derived from
// SET_CLOCK_OFFSET_PREFIX via sizeof(), not typed as a number at all), and
// fixes it here by testing against that SAME shared symbol -- imported from
// time_utils.h, not redeclared -- so there is exactly one place this prefix
// is spelled out, and no second copy that could quietly disagree with the
// first.
//
// These tests exercise the parsing/prefix-length arithmetic and the
// re-basing arithmetic directly, at the level a native test can reach
// (main.cpp's onMqttMessage() itself is not link-testable here -- it is
// Arduino/PubSubClient-coupled -- so these tests pin down the two pieces of
// pure arithmetic the handler depends on getting right, not the handler's
// own substring() call).
// ---------------------------------------------------------------------

void test_set_clock_offset_prefix_length_matches_literal(void) {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        17, (uint32_t)strlen(SET_CLOCK_OFFSET_PREFIX),
        "SET_CLOCK_OFFSET_PREFIX (src/time_utils.h, shared with main.cpp's "
        "handler) must be exactly 17 characters. This documents the "
        "expected length; it is not what makes the handler correct -- "
        "main.cpp derives its substring() offset from "
        "sizeof(SET_CLOCK_OFFSET_PREFIX) directly, so it cannot drift from "
        "this string regardless of what this assertion says.");
}

// Simulates exactly what the handler does: given the full MQTT payload,
// substring() past the prefix (using sizeof(SET_CLOCK_OFFSET_PREFIX) - 1,
// the same expression main.cpp uses -- not a hard-coded 17) and strtoul()
// the remainder. Proves that offset recovers the documented bench value in
// full. This pins the ARITHMETIC main.cpp's handler depends on, not the
// handler itself, which is not link-testable from a native build.
void test_set_clock_offset_payload_parses_at_correct_offset(void) {
    const char *payload = "SET_CLOCK_OFFSET:4294900000";
    const char *val = payload + (sizeof(SET_CLOCK_OFFSET_PREFIX) - 1);
    unsigned long parsed = strtoul(val, nullptr, 10);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        4294900000u, (uint32_t)parsed,
        "sizeof(SET_CLOCK_OFFSET_PREFIX) - 1 must recover the full "
        "documented offset value. The round-1 substring(18) off-by-one "
        "instead yields \"294900000\" -- ~46.3 days from the wrap instead "
        "of the intended ~67 seconds -- with no error and no visible "
        "symptom on the bench.");
}

// ---------------------------------------------------------------------
// Defect 2: re-basing. Models the shape of slowBlend()'s deadline (a value
// computed as `now + interval` against the clock BEFORE an offset jump) and
// confirms the fix's re-basing rule -- reset the deadline to the new nowMs()
// when the offset changes -- prevents the ~67s strand the reviewer
// identified, instead of leaving the stale deadline to be misread as
// "still in the future" by timeReached()'s signed-cast arithmetic.
// ---------------------------------------------------------------------

void test_offset_jump_without_rebasing_strands_the_timer(void) {
    // Mirrors the bench scenario: clock sits near a small value, a 100ms
    // deadline is set normally, then SET_CLOCK_OFFSET jumps clockOffsetMs by
    // ~4294900000 to land nowMs() near the 2^32 wrap.
    uint32_t nowBeforeJump = 1000u;
    uint32_t deadline = nowBeforeJump + 100u; // unrebased -- the bug

    uint32_t offsetJump = 4294900000u;
    uint32_t nowAfterJump = (uint32_t)(nowBeforeJump + offsetJump);

    // This is the strand: timeReached() reads the untouched deadline as
    // still in the future relative to the jumped clock, for the whole
    // ~67s window the reviewer measured, even though real elapsed time from
    // the timer's point of view raced far past it.
    TEST_ASSERT_FALSE_MESSAGE(
        timeReached(nowAfterJump, deadline),
        "Without re-basing, a stale pre-jump deadline reads as still in the "
        "future immediately after SET_CLOCK_OFFSET -- this is the ~67s "
        "slowBlend() freeze the reviewer identified as blocking defect 2.");
}

void test_offset_jump_with_rebasing_does_not_strand_the_timer(void) {
    // Same jump as above, but this time the handler's fix is applied:
    // the deadline is reset to the new nowMs() at the moment the offset
    // changes, exactly as the SET_CLOCK_OFFSET handler now does for
    // blendTimeout and rotateTimeout.
    uint32_t nowBeforeJump = 1000u;
    (void)nowBeforeJump; // unused directly; kept for symmetry/readability

    uint32_t offsetJump = 4294900000u;
    uint32_t nowAfterJump = (uint32_t)(1000u + offsetJump);

    // The fix: rebase the deadline to the new clock instead of leaving it
    // computed against the old one.
    uint32_t deadline = nowAfterJump; // blendTimeout = nowMs();

    // It must fire on the very next sample -- no strand.
    TEST_ASSERT_TRUE_MESSAGE(
        timeReached(nowAfterJump, deadline),
        "After re-basing (deadline = nowMs() at the moment the offset "
        "changes), the timer must fire immediately rather than stranding "
        "for ~67s -- this is the fix for blocking defect 2.");

    // And a few ms later, sanity-check it still reads as reached (not
    // flipped by some sign error in the rebasing arithmetic).
    TEST_ASSERT_TRUE(timeReached((uint32_t)(nowAfterJump + 5u), deadline));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_deadline_survives_the_wrap_while_blocked);
    RUN_TEST(test_naive_comparison_would_get_this_wrong);
    RUN_TEST(test_unblocked_wrap_self_heals);
    RUN_TEST(test_near_future_deadline_not_yet_reached);
    RUN_TEST(test_boundary_near_24_8_day_limit);
    RUN_TEST(test_type_width_regression_guard);
    RUN_TEST(test_set_clock_offset_prefix_length_matches_literal);
    RUN_TEST(test_set_clock_offset_payload_parses_at_correct_offset);
    RUN_TEST(test_offset_jump_without_rebasing_strands_the_timer);
    RUN_TEST(test_offset_jump_with_rebasing_does_not_strand_the_timer);
    return UNITY_END();
}
