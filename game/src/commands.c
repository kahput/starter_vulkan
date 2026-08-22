#include "commands.h"
#include "assets/asset_types.h"
#include "common.h"
#include "core/arena.h"
#include "core/cmath.h"
#include "core/debug.h"
#include "core/r_types.h"

static inline void _drawlist_batch2d_push_imaged_quad(DrawlistBuffer *buffer, Rectangle src, Rectangle dst, Image2D image_index, Color tint);

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
	_drawlist_batch2d_push_imaged_quad(buffer, (Rectangle){ 0, 0, rect.width, rect.height }, rect, WHITE_IMAGE, color);
	if (FLAG_GET(buffer->flags, DRAWLIST_FLAG_NO_BATCHING))
		buffer->active_batch = NULL;
}

void drawlist_push_rectv(DrawlistBuffer *buffer, float2 position, float2 size, Color color) {
	Rectangle rect = { position.x, position.y, size.x, size.y };
	drawlist_push_rect(buffer, rect, color);
}

void drawlist_push_image_ex(DrawlistBuffer *buffer, Image2D image, Rectangle src, Rectangle dst, float2 origin, float rotation, Color tint) {
	_drawlist_batch2d_push_imaged_quad(buffer, src, dst, image, tint);
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
			drawlist_push_image_ex(buffer, font->atlas, src, dst, (float2){ 0 }, 0.0f, color);
		cursor_x += glyph->advance_x;
	}
}

void drawlist_push_mesh(DrawlistBuffer *buffer, float4x4 transform, Mesh mesh, Material material) {
	if (buffer->active_batch)
		buffer->active_batch = NULL;

	DrawCommandMesh *cmd = drawlist_push_command(buffer, DrawCommandMesh);

	cmd->world_from_model = transform;
	cmd->mesh = mesh;
	cmd->material = material;
}

static inline uint32_t _drawlist_batch2d_find_image(DrawCommandSpriteBatch *batch, Image2D image) {
	for (uint32_t index = 0; index < batch->image_count; ++index) {
		if (batch->images[index].handle.id == image.handle.id)
			return index;
	}

	return 0xFFFFFFFF;
}

void _drawlist_batch2d_push_imaged_quad(DrawlistBuffer *buffer, Rectangle src, Rectangle dst, Image2D image, Color tint) {
	DrawCommandSpriteBatch *batch = buffer->active_batch;

	// TODO: Don't hard-code white to be ID 1
	if (batch == NULL) {
		buffer->active_batch = batch = drawlist_push_command(buffer, DrawCommandSpriteBatch);
		batch->images[batch->image_count++] = WHITE_IMAGE;
	} else if (batch->image_count >= countof(batch->images)) {
		buffer->active_batch = batch = drawlist_push_command(buffer, DrawCommandSpriteBatch);
		batch->images[batch->image_count++] = WHITE_IMAGE;
	}

	uint32_t image_index = _drawlist_batch2d_find_image(batch, image);
	if (image_index == 0xFFFFFFFF) {
		image_index = batch->image_count++;
		batch->images[image_index] = image;
	}

	float x0 = dst.x;
	float y0 = dst.y;
	float x1 = dst.x + dst.width;
	float y1 = dst.y + dst.height;

	float u0 = src.x / image.width;
	float v0 = src.y / image.height;
	float u1 = (src.x + src.width) / image.width;
	float v1 = (src.y + src.height) / image.height;

	uint32_t packed = color_pack(tint);

	// clang-format off
    Vertex2 quad[] = {
        // pos      // tex
        (Vertex2){.position = {x0, y1}, .uv = {u0, v1}, .color = packed, .image_id = image_index},
        (Vertex2){.position = {x1, y0}, .uv = {u1, v0}, .color = packed, .image_id = image_index},
        (Vertex2){.position = {x0, y0}, .uv = {u0, v0}, .color = packed, .image_id = image_index}, 

        (Vertex2){.position = {x0, y1}, .uv = {u0, v1}, .color = packed, .image_id = image_index},
        (Vertex2){.position = {x1, y1}, .uv = {u1, v1}, .color = packed, .image_id = image_index},
        (Vertex2){.position = {x1, y0}, .uv = {u1, v0}, .color = packed, .image_id = image_index}
    };
	// clang-format on

	uint64_t size = sizeof(quad);
	ASSERT_MESSAGE(buffer->offset + size < buffer->capacity, "push_imaged_quad - drawlist buffer-overflow");
	memory_copy(&batch->quads[6 * batch->quad_count], quad, size);
	buffer->offset += size;

	batch->base.size += size;
	batch->quad_count++;
}
