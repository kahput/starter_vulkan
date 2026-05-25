#include "commands.h"
#include "assets/asset_types.h"
#include "common.h"
#include "core/arena.h"
#include "core/cmath.h"
#include "core/debug.h"
#include "core/r_types.h"

static inline void _drawlist_batch2d_push_textured_quad(DrawlistBuffer *buffer, Rectangle src, Rectangle dst, Texture2D texture_index, Color tint);

static Texture2D WHITE_TEXTURE = { { 1 }, 1, 1, TEXTURE_FORMAT_RGBA8_SRGB };

DrawlistBuffer *drawlist_make(Arena *arena, size_t size, DrawlistFlags flags) {
	DrawlistBuffer *result = arena_push_struct(arena, DrawlistBuffer);
	result->push_buffer = arena_push_size(arena, size);
	result->capacity = size;
	result->offset = 0;
	result->flags = flags;

	return result;
}

DrawCommandBase *drawlist_push(DrawlistBuffer *buffer, size_t size, DrawCommandType type) {
	ASSERT(buffer->offset + size < buffer->capacity);

	DrawCommandBase *base = (DrawCommandBase *)(buffer->push_buffer + buffer->offset);
	base->type = type;
	base->size = size;

	buffer->offset += size;
	return base;
}

void drawlist_push_rect(DrawlistBuffer *buffer, Rectangle rect, Color color) {
	_drawlist_batch2d_push_textured_quad(buffer, (Rectangle){ 0, 0, rect.width, rect.height }, rect, WHITE_TEXTURE, color);
	if (FLAG_GET(buffer->flags, DRAWLIST_FLAG_NO_BATCHING))
		buffer->active_batch = NULL;
}

void drawlist_push_rectv(DrawlistBuffer *buffer, float2 position, float2 size, Color color) {
	Rectangle rect = { position.x, position.y, size.x, size.y };
	drawlist_push_rect(buffer, rect, color);
}

void drawlist_push_texture_ex(DrawlistBuffer *buffer, Texture2D texture, Rectangle src, Rectangle dst, float2 origin, float rotation, Color tint) {
	_drawlist_batch2d_push_textured_quad(buffer, src, dst, texture, tint);
	if (FLAG_GET(buffer->flags, DRAWLIST_FLAG_NO_BATCHING))
		buffer->active_batch = NULL;
}

float2 measure_text(Font *font, String text) {
	float max_width = 0.0f;
	float width = 0.0f, height = 0.0f;

	for (uint32_t index = 0; index < text.length; ++index) {
		uint8_t c = text.chars[index];

		if (c == '\n') {
			max_width = maxf(max_width, width);
			break;
		}

		Glyph *glyph = &font->glyphs[c];

		width += glyph->advance_x;
		height = maxf(height, glyph->atlas_rect.height);
	}

	return (float2){
		.x = maxf(max_width, width),
		.y = height,
	};
}

void drawlist_push_text(DrawlistBuffer *buffer, Font *font, String text, float2 position, Color color) {
	float2 dimensions = measure_text(font, text);

	float cursor_x = position.x;
	float cursor_y = position.y + dimensions.y;
	for (uint32_t index = 0; index < text.length; ++index) {
		uint8_t c = text.chars[index];

		if (c == '\n') {
			cursor_x = position.x;
			cursor_y += font->line_height;
			continue;
		}

		ASSERT(c >= 32 && c <= 126);
		Glyph *glyph = &font->glyphs[(uint8_t)c];
		Rectangle src = glyph->atlas_rect;
		Rectangle dst = {
			.x = cursor_x + glyph->bearing.x,
			.y = cursor_y + glyph->bearing.y,
			.width = glyph->atlas_rect.width,
			.height = glyph->atlas_rect.height,
		};

		if (c != 32)
			drawlist_push_texture_ex(buffer, font->atlas, src, dst, (float2){ 0 }, 0.0f, color);
		cursor_x += glyph->advance_x;
	}
}

void drawlist_push_mesh(DrawlistBuffer *buffer, float4x4 transform, Mesh mesh, Material material) {
	DrawCommandMesh *cmd = drawlist_push_command(buffer, DrawCommandMesh);

	cmd->world_from_model = transform;
	cmd->mesh = mesh;
	cmd->material = material;
}

static inline uint32_t _drawlist_batch2d_find_texture(DrawCommandSpriteBatch *batch, Texture2D texture) {
	for (uint32_t index = 0; index < batch->texture_count; ++index) {
		if (batch->textures[index].handle.id == texture.handle.id)
			return index;
	}

	return 0xFFFFFFFF;
}

void _drawlist_batch2d_push_textured_quad(DrawlistBuffer *buffer, Rectangle src, Rectangle dst, Texture2D texture, Color tint) {
	DrawCommandSpriteBatch *batch = buffer->active_batch;

	// TODO: Don't hard-code white to be ID 1
	if (batch == NULL) {
		buffer->active_batch = batch = drawlist_push_command(buffer, DrawCommandSpriteBatch);
		batch->textures[batch->texture_count++] = WHITE_TEXTURE;
	} else if (batch->texture_count >= countof(batch->textures)) {
		buffer->active_batch = batch = drawlist_push_command(buffer, DrawCommandSpriteBatch);
		batch->textures[batch->texture_count++] = WHITE_TEXTURE;
	}

	uint32_t texture_index = _drawlist_batch2d_find_texture(batch, texture);
	if (texture_index == 0xFFFFFFFF) {
		texture_index = batch->texture_count++;
		batch->textures[texture_index] = texture;
	}

	float x0 = dst.x;
	float y0 = dst.y;
	float x1 = dst.x + dst.width;
	float y1 = dst.y + dst.height;

	float u0 = src.x / texture.width;
	float v0 = src.y / texture.height;
	float u1 = (src.x + src.width) / texture.width;
	float v1 = (src.y + src.height) / texture.height;

	uint32_t packed = color_pack(tint);

	// clang-format off
    Vertex2 quad[] = {
        // pos      // tex
        (Vertex2){.position = {x0, y1}, .uv = {u0, v1}, .color = packed, .texture_id = texture_index},
        (Vertex2){.position = {x1, y0}, .uv = {u1, v0}, .color = packed, .texture_id = texture_index},
        (Vertex2){.position = {x0, y0}, .uv = {u0, v0}, .color = packed, .texture_id = texture_index}, 

        (Vertex2){.position = {x0, y1}, .uv = {u0, v1}, .color = packed, .texture_id = texture_index},
        (Vertex2){.position = {x1, y1}, .uv = {u1, v1}, .color = packed, .texture_id = texture_index},
        (Vertex2){.position = {x1, y0}, .uv = {u1, v0}, .color = packed, .texture_id = texture_index}
    };
	// clang-format on

	uint64_t size = sizeof(quad);
	ASSERT_MESSAGE(buffer->offset + size < buffer->capacity, "push_textured_quad - drawlist buffer-overflow");
	memory_copy(&batch->quads[6 * batch->quad_count], quad, size);
	buffer->offset += size;

	batch->base.size += size;
	batch->quad_count++;
}
