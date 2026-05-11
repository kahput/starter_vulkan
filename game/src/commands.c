#include "commands.h"
#include "assets/asset_types.h"
#include "core/arena.h"
#include "core/cmath.h"
#include "core/debug.h"

DrawlistBuffer *drawlist_make(Arena *arena, size_t size) {
	DrawlistBuffer *result = arena_push_struct(arena, DrawlistBuffer);
	result->push_buffer = arena_push_size(arena, size);
	result->capacity = size;
	result->offset = 0;

	return result;
}

DrawCommandBase *drawlist_push(DrawlistBuffer *list, size_t size, DrawCommandType type) {
	ASSERT(list->offset + size < list->capacity);

	DrawCommandBase *base = (DrawCommandBase *)list->push_buffer + list->offset;
	base->type = type;
	base->size = size;

	list->offset += size;
	return base;
}

void drawlist_push_rect(DrawlistBuffer *list, Rectangle rect, Color color) {
	DrawCommandRectangle *cmd = drawlist_push_command(list, DrawCommandRectangle);

	cmd->rect = rect;
	cmd->origin = (float2){ 0 };
	cmd->rotation = 0.0f;
	cmd->color = color;
}

void drawlist_push_rectv(DrawlistBuffer *list, float2 position, float2 size, Color color) {
	Rectangle rect = { position.x, position.y, size.x, size.y };
	drawlist_push_rect(list, rect, color);
}

void drawlist_push_texture_ex(DrawlistBuffer *list, RhiTexture texture, Rectangle src, Rectangle dst, float2 origin, float rotation, Color tint) {
	DrawCommandTexture *cmd = drawlist_push_command(list, DrawCommandTexture);

	cmd->texture = texture;
	cmd->src = src;
	cmd->dest = dst;
	cmd->origin = origin;
	cmd->rotation = rotation;
	cmd->tint = tint;
}

void drawlist_push_text(DrawlistBuffer *list, Font *font, const char *text, float2 position, Color color) {
	float cursor_x = position.x;
	float cursor_y = position.y;
	for (const char *c = text; *c; ++c) {
		if (*c == '\n') {
            cursor_x = position.x;
            cursor_y += font->line_height;
			continue;
		}

		ASSERT(*c >= 32 && *c <= 126);
		Glyph *glyph = &font->glyphs[(uint8_t)*c];
		Rectangle src = glyph->atlas_rect;
		Rectangle dst = {
			.x = cursor_x + glyph->bearing.x,
			.y = cursor_y + glyph->bearing.y,
			.width = glyph->size.x,
			.height = glyph->size.y,
		};

		drawlist_push_texture_ex(list, font->atlas, src, dst, (float2){ 0 }, 0.0f, color);
		cursor_x += glyph->advance_x;
	}
}

void drawlist_push_mesh(DrawlistBuffer *list, float4x4 transform, Mesh mesh, Material material) {
	DrawCommandMesh *cmd = drawlist_push_command(list, DrawCommandMesh);

	cmd->world_from_model = transform;
	cmd->mesh = mesh;
	cmd->material = material;
}
