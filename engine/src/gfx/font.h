#pragma once

#include "gfx/gfx_types.h"
#include "core/geom.h"

typedef struct {
	Rectangle src;
	float2 bearing;
	float advance_x;
} Glyph;

typedef struct {
	Image2D atlas;
	uint32_t line_height, bake_size;

	Glyph *glyphs;
	uint32_t glyph_count;
} Font;

Font load_font(Arena *arena, String8 path, uint32_t font_size);

float2 measure_text(Font *font, String8 text);
static inline float text_height(Font *font, String8 text) {
	return measure_text(font, text).y;
}
static inline float text_width(Font *font, String8 text) {
	return measure_text(font, text).x;
}
