#include "font.h"
#include "core/debug.h"
#include <stb/stb_truetype.h>

uint32_t font_bake_size_to_value[FONT_BAKE_SIZE_MAX] = { 8, 12, 16, 24, 32, 64 };

Font load_font(Arena *arena, String8 path, uint32_t font_size) {
	ArenaTemp scratch = arena_scratch_begin(arena);
	Font result = { 0 };

	String8 file_content = os_file_read_entire(scratch.arena, path);
	stbtt_fontinfo font_info = { 0 };

	bool ok = arena && file_content.length;
	if (ok) {
		ok = stbtt_InitFont(&font_info, file_content.text, 0);

		if (ok == false)
			LOG_WARN("%s - failed to process font data", __func__);
	}

	if (ok) {
		float scale_factor = stbtt_ScaleForPixelHeight(&font_info, (float)font_size);

		int32_t ascent = 0, descent = 0, line_gap = 0;
		if (!stbtt_GetFontVMetricsOS2(&font_info, &ascent, &descent, &line_gap))
			stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &line_gap);

		result.line_height = (ascent - descent + line_gap) * scale_factor;
		result.bake_size = font_size;

		uint32_t codepoint_count = 96;
		int32_t *codepoints = arena_push_count(arena, int32_t, codepoint_count);
		for (uint32_t index = 0; index < 96; ++index)
			codepoints[index] = index + KEY_CODE_SPACE;

		uint32_t atlas_size = 512;

		result.glyph_count = 128;
		result.glyphs = arena_push_count(arena, Glyph, result.glyph_count);

		result.atlas = (Image2D){
			.pixels = arena_push_count(arena, uint8_t, atlas_size * atlas_size * 4),
			.width = atlas_size,
			.height = atlas_size,
		};

		uint32_t padding = 2;
		uint32_t row = 0;
		uint32_t col = padding;

		uint8_t *temp_bitmap = arena_push_count(scratch.arena, uint8_t, atlas_size *atlas_size);

		int32_t min_y = 0;
		for (uint32_t index = 0; index < codepoint_count; ++index) {
			int32_t codepoint_width = 0, codepoint_height = 0;
			int32_t codepoint = codepoints[index];

			int glyph_index = stbtt_FindGlyphIndex(&font_info, codepoint);

			if (glyph_index) {
				int32_t x0, y0, x1, y1, advance;
				stbtt_GetGlyphBitmapBox(&font_info, glyph_index, scale_factor, scale_factor, &x0, &y0, &x1, &y1);
				stbtt_GetGlyphHMetrics(&font_info, glyph_index, &advance, NULL);

				uint32_t width = x1 - x0, height = y1 - y0;

				if (col + width + padding >= atlas_size) {
					col = padding;
					row += (uint32_t)(font_size + 0.5f);
					ASSERT(row + y1 - y0 < atlas_size);
				}
				stbtt_MakeGlyphBitmap(&font_info, (uint8_t *)temp_bitmap + col + (row * atlas_size), width, height, atlas_size, scale_factor, scale_factor, glyph_index);

				min_y = MIN(min_y, y0);
				result.glyphs[codepoint] = (Glyph){
					.src = { .x = col, .y = row, .width = width, .height = height },
					.bearing = { x0, y0 },
					.advance_x = (int32_t)(advance * scale_factor),
				};

				col += width + padding;
			}
		}

		uint8_t *src = temp_bitmap;
		uint8_t *dst = result.atlas.pixels;
		for (uint32_t i = 0; i < atlas_size * atlas_size; i++) {
			*dst++ = 255;
			*dst++ = 255;
			*dst++ = 255;
			*dst++ = *src++;
		}
	}

	arena_temp_end(scratch);
	return result;
}

float2 measure_text(Font *font, String8 text) {
	float2 result = { 0.0f, 0.0f };
	float x_offset = 0.0f;
	for (uint32_t index = 0; index < text.length; ++index) {
		uint8_t c = text.text[index];
		if (c == '\n') {
			result.x = maxf(result.x, x_offset);
			x_offset = 0.0f;
			result.y += font->bake_size;
		}

		Glyph *glyph = &font->glyphs[c];
		x_offset += glyph->advance_x;
		result.y = maxf(result.y, glyph->src.height);
	}

	result.x = maxf(result.x, x_offset);
	return result;
}
