#pragma once

// Issue #0016: a hand-rolled, shape-specific parser for the SET_EFFECT MQTT
// payload, plus the NVS load *decision* (separated from the Preferences I/O
// so it is native-testable).
//
// NOT A JSON VALIDATOR. This purpose-built extractor knows exactly one
// shape (mode/colors/speed/intensity/timeout) and nothing else. Explicit
// non-guarantees, spelled out so a future reader doesn't assume more than
// this does:
//   - Unknown extra keys are ignored (forward compatibility: a field a
//     newer control script sends must not brick an older device).
//   - A key name occurring inside a *string value* could in principle be
//     mistaken for a key. Unreachable with this schema -- values are hex
//     colors and mode names, none of which contain "mode"/"colors"/
//     "speed"/"intensity"/"timeout" -- and bounded anyway by the fact that
//     a mis-found field then has to parse as a valid value for its slot.
//   - Only a top-level `{ ... }` is accepted; nesting is neither parsed nor
//     rejected beyond that.
//   - The only structural gate is: after trimming, the payload starts with
//     `{` and ends with `}`, and is non-empty.
//
// NO ARDUINOJSON, NO ARDUINO STRING. Per #0013/#0016's binding flash
// constraint: ArduinoJson costs 20-30 KB before a single renderer, and this
// payload is a fixed shape with at most 8 hex color strings -- a
// purpose-built parser is both cheaper and easier to bound. `String` would
// not compile natively, heap-churns per field, and spends flash on
// operator+. This header is const char* in, struct out, throughout;
// main.cpp bridges with msg.c_str().
//
// INCLUDE-ORDER CONTRACT (transitively inherited from effects.h): this
// header includes effects.h, which uses CRGB/CHSV/sin8/min/max WITHOUT
// including <FastLED.h>. Any translation unit that includes this header
// must already have those names in scope -- main.cpp via `#include
// <FastLED.h>` at the top of the file, a native test via
// test/test_effects/fastled_shim.h included FIRST.
//
// Header-only, no new .cpp, no platformio.ini change -- same reasoning as
// src/effects.h and src/time_utils.h: [env:native] does not build project
// sources (test_build_src is off), so a .cpp here could not be linked from
// a native test without dragging main.cpp/WiFi.h/FastLED.h into the host
// build. [env:native] already carries -I src.

#include <stdint.h>
#include <string.h>
#include "effects.h"

// The command prefix. Declared here, not typed as a literal length anywhere
// else, so the offset used to skip past it is always
// `sizeof(SET_EFFECT_PREFIX) - 1` -- never a hand-typed number. This is the
// exact defect that cost issue #0008 two rounds: a hand-typed substring()
// offset silently ate a digit, and the test that "covered" it compared one
// copy of the literal against another.
static const char SET_EFFECT_PREFIX[] = "SET_EFFECT:";

enum EffectParseResult {
    EFFECT_PARSE_OK = 0,
    EFFECT_PARSE_NOT_OBJECT,      // missing/unbalanced braces, empty payload
    EFFECT_PARSE_NO_MODE,
    EFFECT_PARSE_BAD_MODE,
    EFFECT_PARSE_NO_COLORS,
    EFFECT_PARSE_BAD_COLOR_COUNT, // 0, or > MAX_CUSTOM_COLORS
    EFFECT_PARSE_BAD_COLOR,       // not #RRGGBB / RRGGBB
    EFFECT_PARSE_BAD_NUMBER       // speed/intensity non-numeric or > 255
};

inline const char* effectParseResultName(EffectParseResult r) {
    switch (r) {
        case EFFECT_PARSE_OK: return "OK";
        case EFFECT_PARSE_NOT_OBJECT: return "NOT_OBJECT";
        case EFFECT_PARSE_NO_MODE: return "NO_MODE";
        case EFFECT_PARSE_BAD_MODE: return "BAD_MODE";
        case EFFECT_PARSE_NO_COLORS: return "NO_COLORS";
        case EFFECT_PARSE_BAD_COLOR_COUNT: return "BAD_COLOR_COUNT";
        case EFFECT_PARSE_BAD_COLOR: return "BAD_COLOR";
        case EFFECT_PARSE_BAD_NUMBER: return "BAD_NUMBER";
        default: return "UNKNOWN";
    }
}

// ---------------------------------------------------------------------
// Low-level helpers
// ---------------------------------------------------------------------

inline bool isWs(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

inline const char* skipWs(const char* p) {
    while (*p && isWs(*p)) p++;
    return p;
}

// Case-insensitive char compare, ASCII only (sufficient for this schema --
// key names and mode names are all ASCII).
inline char lowerChar(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

// Finds `"key"` (case-insensitive) followed by optional whitespace, a `:`,
// and more optional whitespace, anywhere in `json`; returns a pointer to
// the first non-space character after the colon, or nullptr if `key` never
// appears as a quoted, colon-followed token. Because each field is found
// independently by name, key ORDER in the payload is irrelevant and
// whitespace is handled in exactly two places (skipWs, and the scan below).
inline const char* findField(const char* json, const char* key) {
    size_t keyLen = strlen(key);
    const char* p = json;
    while (*p) {
        if (*p == '"') {
            const char* start = p + 1;
            const char* q = start;
            while (*q && *q != '"') q++;
            if (*q != '"') return nullptr; // unterminated string -- no more fields to find
            size_t len = (size_t)(q - start);
            bool match = (len == keyLen);
            if (match) {
                for (size_t i = 0; i < len; i++) {
                    if (lowerChar(start[i]) != lowerChar(key[i])) { match = false; break; }
                }
            }
            const char* afterQuote = q + 1;
            if (match) {
                const char* r = skipWs(afterQuote);
                if (*r != ':') { p = afterQuote; continue; } // not actually "key": -- keep scanning
                r = skipWs(r + 1);
                return r;
            }
            p = afterQuote;
        } else {
            p++;
        }
    }
    return nullptr;
}

// Requires >=1 digit; consumes digits only -- a leading '-', '+', '.' or
// quote is a failure, so `"speed":"128"` is rejected (a quoted number is a
// different type, and accepting it invites a second parse path nobody
// tests). Once the accumulator exceeds `cap` it pins at `cap` and sets
// `capped`, so no digit string of any length can overflow uint32_t. The
// caller decides what `capped` means: timeout clamps, speed/intensity
// reject. Advances `p` past the consumed digits on success.
inline bool parseUIntCapped(const char*& p, uint32_t cap, uint32_t& value, bool& capped) {
    if (!p || *p < '0' || *p > '9') return false;
    uint64_t acc = 0;
    capped = false;
    while (*p >= '0' && *p <= '9') {
        acc = acc * 10 + (uint32_t)(*p - '0');
        if (acc > cap) { acc = cap; capped = true; }
        p++;
    }
    value = (uint32_t)acc;
    return true;
}

inline int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline bool hexByte(const char* s, uint8_t& out) {
    int hi = hexNibble(s[0]);
    int lo = hexNibble(s[1]);
    if (hi < 0 || lo < 0) return false;
    out = (uint8_t)((hi << 4) | lo);
    return true;
}

// Accepts an optional leading '#', then EXACTLY six hex digits and nothing
// else -- 5 digits, 7 digits, a "0x" prefix, "#GG0000", an empty string are
// all rejected. Shorthand #RGB is deliberately NOT supported (it is a
// second code path for a form nobody sends from a script).
inline bool parseHexColor(const char* s, size_t len, CRGB& c) {
    if (len > 0 && s[0] == '#') { s++; len--; }
    if (len != 6) return false;
    uint8_t r, g, b;
    if (!hexByte(s, r)) return false;
    if (!hexByte(s + 2, g)) return false;
    if (!hexByte(s + 4, b)) return false;
    c = CRGB(r, g, b);
    return true;
}

// Requires a leading '"', records the span up to (not including) the next
// '"', and fails on a backslash inside the span -- there is no escape
// processing: an escape has no meaning in a mode name or a hex color, and
// rejecting is cheaper and safer than half-implementing unescaping.
// Advances `p` to just past the closing quote on success.
inline bool parseQuotedSpan(const char*& p, const char*& start, size_t& len) {
    if (!p || *p != '"') return false;
    const char* s = p + 1;
    const char* q = s;
    while (*q && *q != '"') {
        if (*q == '\\') return false;
        q++;
    }
    if (*q != '"') return false;
    start = s;
    len = (size_t)(q - s);
    p = q + 1;
    return true;
}

inline bool matchesModeName(const char* name, const char* s, size_t len) {
    size_t nameLen = strlen(name);
    if (nameLen != len) return false;
    for (size_t i = 0; i < len; i++) {
        if (lowerChar(name[i]) != lowerChar(s[i])) return false;
    }
    return true;
}

// Linear scan of EFFECT_MODE_NAMES[9], case-insensitive, length-checked.
// Names only -- a bare integer ("mode":2) is NOT accepted: the wire format
// is names, and one form means one thing to test.
inline bool parseMode(const char* s, size_t len, EffectMode& m) {
    size_t count = sizeof(EFFECT_MODE_NAMES) / sizeof(EFFECT_MODE_NAMES[0]);
    for (size_t i = 0; i < count; i++) {
        if (matchesModeName(EFFECT_MODE_NAMES[i], s, len)) {
            m = (EffectMode)i;
            return true;
        }
    }
    return false;
}

// Requires '[', then quoted spans separated by ',' with whitespace allowed
// anywhere, then ']'. Counts as it goes and STOPS/REJECTS on the 9th
// element rather than parsing all of them first -- the bound is enforced by
// the loop itself, not a check afterward that a future edit can slip past.
// Empty array -> BAD_COLOR_COUNT (via the caller, since this returns a
// generic false/true and a count).
inline EffectParseResult parseColorArray(const char* p, CustomEffectConfig& out) {
    p = skipWs(p);
    if (*p != '[') return EFFECT_PARSE_BAD_COLOR_COUNT; // "colors" value isn't an array at all
    p = skipWs(p + 1);

    uint8_t count = 0;
    if (*p == ']') {
        return EFFECT_PARSE_BAD_COLOR_COUNT; // empty array
    }

    for (;;) {
        p = skipWs(p);
        const char* spanStart;
        size_t spanLen;
        if (!parseQuotedSpan(p, spanStart, spanLen)) return EFFECT_PARSE_BAD_COLOR;

        if (count >= MAX_CUSTOM_COLORS) return EFFECT_PARSE_BAD_COLOR_COUNT; // 9th element

        CRGB c;
        if (!parseHexColor(spanStart, spanLen, c)) return EFFECT_PARSE_BAD_COLOR;
        out.colors[count] = c;
        count++;

        p = skipWs(p);
        if (*p == ',') { p = skipWs(p + 1); continue; }
        if (*p == ']') break;
        return EFFECT_PARSE_BAD_COLOR; // neither a separator nor the close -- malformed
    }

    out.colorCount = count;
    return EFFECT_PARSE_OK;
}

// ---------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------

// `out` is a scratch struct owned by the caller, never the live
// customEffect -- this is the structural guarantee behind "on any parse
// failure, keep prior state": partial application is unreachable, not a
// discipline the caller has to remember. Starts by resetting `out`, so a
// rejected parse also leaves no half-filled scratch for a careless caller
// to copy.
inline EffectParseResult parseEffectPayload(const char* json, CustomEffectConfig& out) {
    resetCustomEffect(out);

    if (!json) return EFFECT_PARSE_NOT_OBJECT;
    const char* start = skipWs(json);
    size_t len = strlen(start);
    while (len > 0 && isWs(start[len - 1])) len--;
    if (len == 0 || start[0] != '{' || start[len - 1] != '}') return EFFECT_PARSE_NOT_OBJECT;

    // mode (required)
    const char* modeField = findField(start, "mode");
    if (!modeField) return EFFECT_PARSE_NO_MODE;
    {
        const char* p = modeField;
        const char* spanStart;
        size_t spanLen;
        if (!parseQuotedSpan(p, spanStart, spanLen)) return EFFECT_PARSE_BAD_MODE;
        EffectMode m;
        if (!parseMode(spanStart, spanLen, m)) return EFFECT_PARSE_BAD_MODE;
        out.mode = m;
    }

    // colors (required)
    const char* colorsField = findField(start, "colors");
    if (!colorsField) return EFFECT_PARSE_NO_COLORS;
    {
        EffectParseResult r = parseColorArray(colorsField, out);
        if (r != EFFECT_PARSE_OK) return r;
    }

    // speed (optional, default 128; 0-255, reject out of range)
    const char* speedField = findField(start, "speed");
    if (speedField) {
        const char* p = speedField;
        uint32_t value;
        bool capped;
        if (!parseUIntCapped(p, 0xFFFFFFFFu, value, capped)) return EFFECT_PARSE_BAD_NUMBER;
        if (value > 255) return EFFECT_PARSE_BAD_NUMBER;
        out.speed = (uint8_t)value;
    }

    // intensity (optional, default 128; 0-255, reject out of range)
    const char* intensityField = findField(start, "intensity");
    if (intensityField) {
        const char* p = intensityField;
        uint32_t value;
        bool capped;
        if (!parseUIntCapped(p, 0xFFFFFFFFu, value, capped)) return EFFECT_PARSE_BAD_NUMBER;
        if (value > 255) return EFFECT_PARSE_BAD_NUMBER;
        out.intensity = (uint8_t)value;
    }

    // timeout (optional, default 0 == until changed); seconds; >28800
    // clamped to 28800, never rejected. parseUIntCapped's own cap prevents
    // any digit string of any length from overflowing uint32_t before the
    // 28800 policy is even applied.
    const char* timeoutField = findField(start, "timeout");
    if (timeoutField) {
        const char* p = timeoutField;
        uint32_t seconds;
        bool capped;
        if (!parseUIntCapped(p, 28800u, seconds, capped)) return EFFECT_PARSE_BAD_NUMBER;
        out.timeoutMs = seconds * 1000UL; // max 28,800,000 -- fits uint32_t
    }

    return EFFECT_PARSE_OK;
}

// ---------------------------------------------------------------------
// NVS load decision (issue #0016 section 5.2) -- pure function, no
// Preferences dependency, so it is native-testable on its own.
// ---------------------------------------------------------------------

// Returns true if the stored blob was accepted; false if `out` was reset to
// defaults. Logic: reset `out` first; accept only if
// storedVersion == SETTINGS_VERSION AND storedLen == sizeof(CustomEffectConfig)
// AND the copied struct passes customEffectIsValid(); on acceptance force
// out.activatedAt = 0 (a millis() stamp has no meaning across a reboot --
// issue #0017 replaces its role with an absolute wall-clock end time; this
// phase must not re-seed it to "now", which would silently restart the
// timer).
inline bool applyLoadedCustomEffect(uint8_t storedVersion, size_t storedLen,
                                     const void* bytes, CustomEffectConfig& out) {
    resetCustomEffect(out);

    if (storedVersion != SETTINGS_VERSION) return false;
    if (storedLen != sizeof(CustomEffectConfig)) return false;

    CustomEffectConfig candidate;
    memcpy(&candidate, bytes, sizeof(CustomEffectConfig));
    if (!customEffectIsValid(candidate)) return false;

    out = candidate;
    out.activatedAt = 0;
    return true;
}
