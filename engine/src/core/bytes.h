#pragma once

#include "common.h"
#include "strings.h"

typedef struct {
	uint8_t *start, *end;
	uint8_t *at;
} Stream;

INLINE uint32_t load32le(const uint8_t *bytes) { return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24; }
INLINE uint32_t load24le(const uint8_t *bytes) { return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16; }
INLINE uint16_t load16le(const uint8_t *bytes) { return (uint16_t)bytes[0] | (uint16_t)bytes[1] << 8; }
INLINE uint16_t load16be(const uint8_t *bytes) { return (uint16_t)bytes[1] | (uint16_t)bytes[0] << 8; }
INLINE uint16_t load24be(const uint8_t *bytes) { return (uint16_t)bytes[2] | (uint16_t)bytes[1] << 8 | (uint32_t)bytes[0] << 16; }
INLINE uint32_t load32be(const uint8_t *bytes) { return (uint32_t)bytes[3] | (uint32_t)bytes[2] << 8 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[0] << 24; }

INLINE void store16le(uint8_t *bytes, uint16_t value) { bytes[0] = (value >> 0) & 0xFF, bytes[1] = (value >> 8) & 0xFF; }
INLINE void store24le(uint8_t *bytes, uint16_t value) { bytes[0] = (value >> 0) & 0xFF, bytes[1] = (value >> 8) & 0xFF, bytes[2] = (value >> 16) & 0xFF; }
INLINE void store32le(uint8_t *bytes, uint32_t value) { bytes[0] = (value >> 0) & 0xFF, bytes[1] = (value >> 8) & 0xFF, bytes[2] = (value >> 16) & 0xFF, bytes[3] = (value >> 24) & 0xFF; }
INLINE void store16be(uint8_t *bytes, uint16_t value) { bytes[1] = (value >> 0) & 0xFF, bytes[0] = (value >> 8) & 0xFF; }
INLINE void store24be(uint8_t *bytes, uint16_t value) { bytes[2] = (value >> 0) & 0xFF, bytes[1] = (value >> 8) & 0xFF, bytes[0] = (value >> 16) & 0xFF; }
INLINE void store32be(uint8_t *bytes, uint32_t value) { bytes[3] = (value >> 0) & 0xFF, bytes[2] = (value >> 8) & 0xFF, bytes[1] = (value >> 16) & 0xFF, bytes[0] = (value >> 24) & 0xFF; }

INLINE float bitcast_float32(uint32_t v) { return *(float *)memory_copy(&(float){ 0 }, &v, sizeof(float)); }

INLINE Stream stream(void *start, void *end) { return (Stream){ start, end, start }; }
INLINE Stream stream_from_string(string8 s) { return stream(s.bytes, s.bytes + s.length); }
INLINE bool stream_at_end(Stream *s) { return s->at >= s->end; }

INLINE string8 readchars(Stream *s, uint32_t count) { return s->at += count, str8_range(s->at - count, s->at); }
INLINE uint16_t read16le(Stream *s) { return s->at += 2, load16le(s->at - 2); }
INLINE uint16_t read24le(Stream *s) { return s->at += 3, load24le(s->at - 3); }
INLINE uint32_t read32le(Stream *s) { return s->at += 4, load32le(s->at - 4); }
INLINE uint16_t read16be(Stream *s) { return s->at += 2, load16be(s->at - 2); }
INLINE uint16_t read24be(Stream *s) { return s->at += 3, load16be(s->at - 3); }
INLINE uint32_t read32be(Stream *s) { return s->at += 4, load32be(s->at - 4); }
#define readstruct(s, T) *(s->at += sizeof(T), (T *)s->at - 1)

INLINE void writechars(Stream *s, string8 chars) { s->at += chars.length, memory_copy(s->at - chars.length, chars.bytes, chars.length); }
INLINE void write16le(Stream *s, uint16_t value) { s->at += 2, store16le(s->at - 2, value); }
INLINE void write32le(Stream *s, uint32_t value) { s->at += 4, store24le(s->at - 3, value); }
INLINE void write24le(Stream *s, uint32_t value) { s->at += 3, store32le(s->at - 4, value); }
INLINE void write16be(Stream *s, uint16_t value) { s->at += 2, store16be(s->at - 2, value); }
INLINE void write32be(Stream *s, uint32_t value) { s->at += 4, store24be(s->at - 3, value); }
INLINE void write24be(Stream *s, uint32_t value) { s->at += 3, store32be(s->at - 4, value); }
