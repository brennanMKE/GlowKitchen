// Native tests for the pulse in src/pulse.h and the retain-flag recovery in
// src/mqtt_retained.h. Run with `pio test -e native`.
#include <unity.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "pulse.h"
#include "mqtt_retained.h"

void setUp(void) {}
void tearDown(void) {}

// The phase boundaries, derived the same way pulseFrame() does, so a change to
// a constant moves the tests with it instead of silently passing.
static uint32_t leadFor(uint32_t dur) {
    return dur - PULSE_PEAK_FRAMES * PULSE_FRAME_MS - PULSE_RETURN_MS;
}
static uint32_t dipFor(uint32_t dur) {
    return leadFor(dur) * PULSE_DIP_PERCENT / 100;
}

// ---- Command parsing ------------------------------------------------------

void test_parse_bare_pulse_is_the_default(void) {
    uint32_t ms = 1;
    uint8_t level = 9;
    TEST_ASSERT_TRUE(parsePulseCommand("PULSE", ms, level));
    TEST_ASSERT_EQUAL_UINT32(750, ms);
    TEST_ASSERT_EQUAL_UINT8(0, level);
}

void test_parse_ms_and_level(void) {
    uint32_t ms = 0;
    uint8_t level = 0;
    TEST_ASSERT_TRUE(parsePulseCommand("PULSE:1200", ms, level));
    TEST_ASSERT_EQUAL_UINT32(1200, ms);
    TEST_ASSERT_EQUAL_UINT8(0, level);
    TEST_ASSERT_TRUE(parsePulseCommand("PULSE:1200:128", ms, level));
    TEST_ASSERT_EQUAL_UINT32(1200, ms);
    TEST_ASSERT_EQUAL_UINT8(128, level);
    TEST_ASSERT_TRUE(parsePulseCommand("PULSE:900:1", ms, level));
    TEST_ASSERT_EQUAL_UINT8(1, level);
    TEST_ASSERT_TRUE(parsePulseCommand("PULSE:900:255", ms, level));
    TEST_ASSERT_EQUAL_UINT8(255, level);
}

void test_parse_clamps_duration(void) {
    uint32_t ms = 0;
    uint8_t level = 0;
    parsePulseCommand("PULSE:1", ms, level);
    TEST_ASSERT_EQUAL_UINT32(PULSE_MIN_MS, ms);
    parsePulseCommand("PULSE:299", ms, level);
    TEST_ASSERT_EQUAL_UINT32(300, ms);
    parsePulseCommand("PULSE:5001", ms, level);
    TEST_ASSERT_EQUAL_UINT32(5000, ms);
    parsePulseCommand("PULSE:99999999999999999999", ms, level);
    TEST_ASSERT_EQUAL_UINT32(5000, ms);
    parsePulseCommand("PULSE:0", ms, level);
    TEST_ASSERT_EQUAL_UINT32(750, ms);
}

void test_parse_malformed_falls_back_to_defaults(void) {
    uint32_t ms = 0;
    uint8_t level = 0;
    const char *bad[] = {"PULSE:", "PULSE:abc", "PULSE:-500", "PULSE::", "PULSE:x:y"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        TEST_ASSERT_TRUE_MESSAGE(parsePulseCommand(bad[i], ms, level), bad[i]);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(750, ms, bad[i]);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, level, bad[i]);
    }
    // A bad level keeps a good duration; a bad duration keeps a good level.
    parsePulseCommand("PULSE:1000:0", ms, level);
    TEST_ASSERT_EQUAL_UINT32(1000, ms);
    TEST_ASSERT_EQUAL_UINT8(0, level);
    parsePulseCommand("PULSE:1000:256", ms, level);
    TEST_ASSERT_EQUAL_UINT32(1000, ms);
    TEST_ASSERT_EQUAL_UINT8(0, level);
    parsePulseCommand("PULSE:1000:-3", ms, level);
    TEST_ASSERT_EQUAL_UINT8(0, level);
    parsePulseCommand("PULSE::64", ms, level);
    TEST_ASSERT_EQUAL_UINT32(750, ms);
    TEST_ASSERT_EQUAL_UINT8(64, level);
}

void test_parse_rejects_other_commands(void) {
    uint32_t ms = 0;
    uint8_t level = 0;
    TEST_ASSERT_FALSE(parsePulseCommand("PULSES", ms, level));
    TEST_ASSERT_FALSE(parsePulseCommand("PULS", ms, level));
    TEST_ASSERT_FALSE(parsePulseCommand("SWELL", ms, level));
    TEST_ASSERT_FALSE(parsePulseCommand("SET_EFFECT:{\"mode\":\"PULSE\"}", ms, level));
    TEST_ASSERT_FALSE(parsePulseCommand(nullptr, ms, level));
}

void test_peak_level_is_capped_at_the_ceiling(void) {
    TEST_ASSERT_EQUAL_UINT8(225, pulsePeakLevel(0, 225));
    TEST_ASSERT_EQUAL_UINT8(225, pulsePeakLevel(255, 225));
    TEST_ASSERT_EQUAL_UINT8(225, pulsePeakLevel(226, 225));
    TEST_ASSERT_EQUAL_UINT8(100, pulsePeakLevel(100, 225));
    TEST_ASSERT_EQUAL_UINT8(1, pulsePeakLevel(1, 225));
}

// ---- Frame math -----------------------------------------------------------

void test_starts_and_ends_at_base_with_no_white(void) {
    uint8_t peaks = 0;
    PulseFrame f = pulseFrame(0, 750, 180, 225, peaks);
    TEST_ASSERT_EQUAL_UINT8(180, f.bright);
    TEST_ASSERT_EQUAL_UINT8(0, f.white);
    f = pulseFrame(750, 750, 180, 225, peaks);
    TEST_ASSERT_EQUAL_UINT8(180, f.bright);
    TEST_ASSERT_EQUAL_UINT8(0, f.white);
    TEST_ASSERT_EQUAL_UINT8(0, peaks);
}

void test_dips_to_a_fifth_at_the_bottom(void) {
    // GlowLamp's numbers: 750 ms leaves a 659 ms lead-in, a 263 ms dip.
    TEST_ASSERT_EQUAL_UINT32(659, leadFor(750));
    TEST_ASSERT_EQUAL_UINT32(263, dipFor(750));

    uint8_t peaks = 0;
    PulseFrame f = pulseFrame(dipFor(750), 750, 180, 225, peaks);
    // scale8(180, 51) -- the bottom of the dip and the start of the rise.
    TEST_ASSERT_EQUAL_UINT8(pulseScale8(180, PULSE_DIP_FLOOR), f.bright);
    TEST_ASSERT_UINT8_WITHIN(1, 36, f.bright);
    TEST_ASSERT_EQUAL_UINT8(0, f.white);

    // Monotonic down through the dip, no white anywhere in it.
    uint8_t prev = 255;
    for (uint32_t t = 0; t < dipFor(750); t++) {
        f = pulseFrame(t, 750, 180, 225, peaks);
        TEST_ASSERT_TRUE(f.bright <= prev);
        TEST_ASSERT_EQUAL_UINT8(0, f.white);
        prev = f.bright;
    }
}

void test_white_starts_192_into_the_rise(void) {
    const uint32_t dur = 750;
    const uint32_t dip = dipFor(dur);
    const uint32_t rise = leadFor(dur) - dip;
    uint8_t peaks = 0;
    for (uint32_t t = dip; t < leadFor(dur); t++) {
        uint8_t f = (uint8_t)(((t - dip) * 255U) / rise);
        PulseFrame fr = pulseFrame(t, dur, 180, 225, peaks);
        if (f <= PULSE_WHITEN_FROM) {
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, fr.white, "white before 192/255 of the rise");
        } else {
            TEST_ASSERT_TRUE_MESSAGE(fr.white > 0, "no white after 192/255 of the rise");
        }
    }
    TEST_ASSERT_EQUAL_UINT8(0, peaks);
}

void test_exactly_one_peak_frame_however_late_it_lands(void) {
    const uint32_t dur = 750;
    const uint32_t lead = leadFor(dur);

    // On time.
    uint8_t peaks = 0;
    PulseFrame f = pulseFrame(lead, dur, 180, 225, peaks);
    TEST_ASSERT_EQUAL_UINT8(225, f.bright);
    TEST_ASSERT_EQUAL_UINT8(255, f.white);
    TEST_ASSERT_EQUAL_UINT8(1, peaks);
    // The next frame, even one still inside the peak's 16 ms, is the return.
    f = pulseFrame(lead + 5, dur, 180, 225, peaks);
    TEST_ASSERT_TRUE(f.white < 255);
    TEST_ASSERT_TRUE(f.bright < 225);

    // A frame that only arrives well into the return still gets the peak once.
    peaks = 0;
    f = pulseFrame(lead + 60, dur, 180, 225, peaks);
    TEST_ASSERT_EQUAL_UINT8(225, f.bright);
    TEST_ASSERT_EQUAL_UINT8(255, f.white);
    f = pulseFrame(lead + 70, dur, 180, 225, peaks);
    TEST_ASSERT_TRUE(f.white < 255);
}

void test_return_eases_back_to_base(void) {
    const uint32_t dur = 750;
    const uint32_t lead = leadFor(dur);
    uint8_t peaks = PULSE_PEAK_FRAMES;  // peak already shown
    uint8_t prevBright = 255;
    uint8_t prevWhite = 255;
    for (uint32_t t = lead; t < dur; t++) {
        PulseFrame f = pulseFrame(t, dur, 180, 225, peaks);
        TEST_ASSERT_TRUE(f.bright <= prevBright);
        TEST_ASSERT_TRUE(f.white <= prevWhite);
        prevBright = f.bright;
        prevWhite = f.white;
    }
    TEST_ASSERT_UINT8_WITHIN(3, 180, prevBright);
    TEST_ASSERT_UINT8_WITHIN(3, 0, prevWhite);
}

void test_peak_never_below_base_or_above_requested(void) {
    // A strip already brighter than the requested level flashes at its own
    // level rather than dimming into the "peak".
    uint8_t peaks = 0;
    PulseFrame f = pulseFrame(leadFor(750), 750, 200, 100, peaks);
    TEST_ASSERT_EQUAL_UINT8(200, f.bright);
    // A dim strip climbs to the requested level, never past it.
    for (uint32_t t = 0; t < 750; t++) {
        uint8_t p = (t >= leadFor(750)) ? PULSE_PEAK_FRAMES : 0;
        f = pulseFrame(t, 750, 40, 128, p);
        TEST_ASSERT_TRUE(f.bright <= 128);
    }
}

void test_every_duration_completes_every_phase(void) {
    // Each clamped duration, walked at a 10 ms frame, dips, whitens, peaks once
    // and comes back, with no division by zero at the 300 ms floor.
    const uint32_t durs[] = {PULSE_MIN_MS, 301, 500, PULSE_DEFAULT_MS, 2000, PULSE_MAX_MS};
    for (size_t d = 0; d < sizeof(durs) / sizeof(durs[0]); d++) {
        uint8_t peaks = 0;
        uint8_t lowest = 255;
        bool sawWhite = false;
        for (uint32_t t = 0; t < durs[d]; t += 10) {
            PulseFrame f = pulseFrame(t, durs[d], 180, 225, peaks);
            if (f.bright < lowest) lowest = f.bright;
            if (f.white) sawWhite = true;
        }
        TEST_ASSERT_EQUAL_UINT8(PULSE_PEAK_FRAMES, peaks);
        TEST_ASSERT_TRUE(sawWhite);
        TEST_ASSERT_UINT8_WITHIN(6, pulseScale8(180, PULSE_DIP_FLOOR), lowest);
    }
}

void test_rollover_safe_elapsed(void) {
    // The firmware feeds pulseFrame() `millis() - pulseStart` and ends the pulse
    // on `millis() - pulseStart >= pulseMs`. Across the 2^32 wrap that
    // difference is still the true elapsed time.
    const uint32_t start = 0xFFFFFF00UL;  // 256 ms before the wrap
    const uint32_t now = 0x00000100UL;    // 256 ms after it
    TEST_ASSERT_EQUAL_UINT32(512, now - start);
    TEST_ASSERT_FALSE(now - start >= PULSE_DEFAULT_MS);
    TEST_ASSERT_TRUE((uint32_t)(start + PULSE_DEFAULT_MS) - start >= PULSE_DEFAULT_MS);
    uint8_t peaks = 0;
    PulseFrame f = pulseFrame(now - start, PULSE_DEFAULT_MS, 180, 225, peaks);
    TEST_ASSERT_TRUE(f.bright < 180);  // in the rise, not stuck at base
}

void test_every_other_led_whitens_on_any_length(void) {
    TEST_ASSERT_TRUE(pulseWhitensLed(0));
    TEST_ASSERT_FALSE(pulseWhitensLed(1));
    TEST_ASSERT_TRUE(pulseWhitensLed(2));
    TEST_ASSERT_TRUE(pulseWhitensLed(498));
    TEST_ASSERT_FALSE(pulseWhitensLed(499));
    // Half, rounded up, for odd and even strips alike.
    const int lengths[] = {1, 2, 3, 7, 240, 241, 500};
    for (size_t n = 0; n < sizeof(lengths) / sizeof(lengths[0]); n++) {
        int count = 0;
        for (int i = 0; i < lengths[n]; i++) count += pulseWhitensLed(i) ? 1 : 0;
        TEST_ASSERT_EQUAL_INT((lengths[n] + 1) / 2, count);
    }
}

void test_whiten_channel(void) {
    TEST_ASSERT_EQUAL_UINT8(255, pulseWhitenChannel(0, 255));
    TEST_ASSERT_EQUAL_UINT8(255, pulseWhitenChannel(200, 255));
    TEST_ASSERT_EQUAL_UINT8(0, pulseWhitenChannel(0, 0));
    TEST_ASSERT_EQUAL_UINT8(77, pulseWhitenChannel(77, 0));
    TEST_ASSERT_UINT8_WITHIN(1, 128, pulseWhitenChannel(0, 128));
}

// ---- Retain flag recovery -------------------------------------------------

// Build the buffer PubSubClient 2.8's loop() leaves behind before calling the
// callback, and return the `topic` pointer it would pass.
static char *pubsubBuffer(uint8_t *buf, uint8_t header, const char *topic, unsigned int payloadLen) {
    const uint32_t tl = (uint32_t)strlen(topic);
    const bool hasId = (header & 0x06) != 0;
    uint32_t remaining = 2 + tl + payloadLen + (hasId ? 2 : 0);
    size_t pos = 0;
    buf[pos++] = header;
    do {
        uint8_t b = remaining & 0x7F;
        remaining >>= 7;
        if (remaining) b |= 0x80;
        buf[pos++] = b;
    } while (remaining);
    const size_t llen = pos - 1;
    buf[pos++] = (uint8_t)(tl >> 8);
    buf[pos++] = (uint8_t)(tl & 0xFF);
    memcpy(buf + pos, topic, tl);
    // PubSubClient: memmove(buffer+llen+2, buffer+llen+3, tl); buffer[llen+2+tl] = 0
    memmove(buf + llen + 2, buf + llen + 3, tl);
    buf[llen + 2 + tl] = 0;
    return (char *)buf + llen + 2;
}

void test_retain_flag_read_from_the_header(void) {
    static uint8_t buf[1024];
    const char *topic = "lights/all/cmd";
    TEST_ASSERT_EQUAL_INT(MQTT_RETAINED, mqttMessageRetained(pubsubBuffer(buf, 0x31, topic, 5), 5));
    TEST_ASSERT_EQUAL_INT(MQTT_LIVE, mqttMessageRetained(pubsubBuffer(buf, 0x30, topic, 5), 5));
    // QoS 1 carries a packet id.
    TEST_ASSERT_EQUAL_INT(MQTT_RETAINED, mqttMessageRetained(pubsubBuffer(buf, 0x33, topic, 14), 14));
    TEST_ASSERT_EQUAL_INT(MQTT_LIVE, mqttMessageRetained(pubsubBuffer(buf, 0x32, topic, 14), 14));
}

void test_retain_flag_with_multibyte_length(void) {
    // Over 127 bytes remaining: a two-byte varint, whose first byte has the
    // continuation bit set and must not be mistaken for the header.
    static uint8_t buf[1024];
    const char *topic = "lights/84fce68774a4/cmd";
    TEST_ASSERT_EQUAL_INT(MQTT_RETAINED, mqttMessageRetained(pubsubBuffer(buf, 0x31, topic, 300), 300));
    TEST_ASSERT_EQUAL_INT(MQTT_LIVE, mqttMessageRetained(pubsubBuffer(buf, 0x30, topic, 300), 300));
    TEST_ASSERT_EQUAL_INT(MQTT_LIVE, mqttMessageRetained(pubsubBuffer(buf, 0x30, topic, 101), 101));
}

void test_retain_flag_unknown_when_the_layout_does_not_check_out(void) {
    static uint8_t buf[1024];
    const char *topic = "lights/all/cmd";
    // Wrong payload length for this buffer.
    TEST_ASSERT_EQUAL_INT(MQTT_RETAINED_UNKNOWN, mqttMessageRetained(pubsubBuffer(buf, 0x31, topic, 5), 6));
    // Not a PUBLISH header.
    TEST_ASSERT_EQUAL_INT(MQTT_RETAINED_UNKNOWN, mqttMessageRetained(pubsubBuffer(buf, 0x91, topic, 5), 5));
    // A topic that is not in a PubSubClient buffer at all.
    static char plain[64] = "\x55\x55\x55\x55\x55\x55lights/all/cmd";
    TEST_ASSERT_EQUAL_INT(MQTT_RETAINED_UNKNOWN, mqttMessageRetained(plain + 6, 5));
    TEST_ASSERT_EQUAL_INT(MQTT_RETAINED_UNKNOWN, mqttMessageRetained(nullptr, 5));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_bare_pulse_is_the_default);
    RUN_TEST(test_parse_ms_and_level);
    RUN_TEST(test_parse_clamps_duration);
    RUN_TEST(test_parse_malformed_falls_back_to_defaults);
    RUN_TEST(test_parse_rejects_other_commands);
    RUN_TEST(test_peak_level_is_capped_at_the_ceiling);
    RUN_TEST(test_starts_and_ends_at_base_with_no_white);
    RUN_TEST(test_dips_to_a_fifth_at_the_bottom);
    RUN_TEST(test_white_starts_192_into_the_rise);
    RUN_TEST(test_exactly_one_peak_frame_however_late_it_lands);
    RUN_TEST(test_return_eases_back_to_base);
    RUN_TEST(test_peak_never_below_base_or_above_requested);
    RUN_TEST(test_every_duration_completes_every_phase);
    RUN_TEST(test_rollover_safe_elapsed);
    RUN_TEST(test_every_other_led_whitens_on_any_length);
    RUN_TEST(test_whiten_channel);
    RUN_TEST(test_retain_flag_read_from_the_header);
    RUN_TEST(test_retain_flag_with_multibyte_length);
    RUN_TEST(test_retain_flag_unknown_when_the_layout_does_not_check_out);
    return UNITY_END();
}
