#pragma once

#include "gfx/gfx_types.h"

typedef struct {
	Rectangle src;
	float2 bearing;
	float advance_x;
} Glyph;

typedef struct {
	Image2D atlas;
	uint32_t line_height, bake_size;
	uint32_t greatest_bottom_y, greatest_top_y;

	Glyph *glyphs;
	uint32_t glyph_count;
} Font;

typedef enum {
	FONT_BAKE_SIZE_8,
	FONT_BAKE_SIZE_12,
	FONT_BAKE_SIZE_16,
	FONT_BAKE_SIZE_24,
	FONT_BAKE_SIZE_32,
	FONT_BAKE_SIZE_64,

	FONT_BAKE_SIZE_MAX,
} FONT_BakeSize;
extern uint32_t font_bake_size_to_value[FONT_BAKE_SIZE_MAX];

Font load_font(Arena *arena, String8 path, uint32_t font_size);

float2 measure_text(Font *font, String8 text);
static inline float text_height(Font *font, String8 text) {
	return measure_text(font, text).y;
}
static inline float text_width(Font *font, String8 text) {
	return measure_text(font, text).x;
}
