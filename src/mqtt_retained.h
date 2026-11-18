#pragma once

#include <stdint.h>
#include <string.h>

// Whether the PUBLISH that PubSubClient just handed to the callback carried
// the RETAIN flag.
//
// MQTT 3.1.1 (3.3.1.3) has the broker set RETAIN only on a message it sends
// because of a new subscription -- the replay every reconnect gets -- and
// clear it on everything forwarded live. That is exactly the line between a
// PULSE someone just sent and one stuck on the broker replaying on every
// reconnect, but PubSubClient's callback(topic, payload, length) does not
// pass the flag on.
//
// It does leave it in place. PubSubClient 2.8's loop() calls the callback
// with `topic` pointing into its receive buffer, laid out as
//
//   buffer[0]               fixed header: 0x3_ for PUBLISH, bit 0 = RETAIN
//   buffer[1..llen]         remaining length, 1-4 byte varint
//   buffer[llen+1]          topic length MSB (left behind by the memmove)
//   buffer[llen+2..]        topic, NUL-terminated in place   <- `topic`
//
// so the header is recoverable by walking back from `topic`. That depends on
// the library's internals, so nothing is trusted unless it checks out: the
// header must be a PUBLISH and the varint must decode to exactly the length
// this topic and payload imply. Anything else answers UNKNOWN, and the caller
// falls back to something conservative.
//
// Lives in its own header so the native suite (test/test_pulse/) can build
// the buffer PubSubClient would and check the walk against it.

enum MqttRetained
{
    MQTT_LIVE = 0,
    MQTT_RETAINED = 1,
    MQTT_RETAINED_UNKNOWN = 2,
};

static inline MqttRetained mqttMessageRetained(const char *topic, unsigned int payloadLen) {
    if (topic == nullptr) return MQTT_RETAINED_UNKNOWN;
    const uint8_t *t = (const uint8_t *)topic;
    const uint32_t topicLen = (uint32_t)strlen(topic);

    // t[-2] is the varint's last byte (continuation bit clear); any earlier
    // varint bytes have it set; the byte before them is the header.
    const uint8_t *last = t - 2;
    if (*last & 0x80) return MQTT_RETAINED_UNKNOWN;
    const uint8_t *first = last;
    while (last - first < 3 && (first[-1] & 0x80)) first--;
    const uint8_t header = first[-1];
    if ((header & 0xF0) != 0x30) return MQTT_RETAINED_UNKNOWN;

    uint32_t remaining = 0;
    uint32_t mult = 1;
    for (const uint8_t *p = first; p <= last; p++) {
        remaining += (uint32_t)(*p & 0x7F) * mult;
        mult *= 128;
    }
    // Topic length field (2) + topic + payload, plus a packet id (2) at QoS 1+.
    const uint32_t qos0 = 2 + topicLen + payloadLen;
    const bool hasId = (header & 0x06) != 0;
    if (remaining != qos0 + (hasId ? 2U : 0U)) return MQTT_RETAINED_UNKNOWN;

    return (header & 0x01) ? MQTT_RETAINED : MQTT_LIVE;
}
