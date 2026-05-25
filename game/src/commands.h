#ifndef COMMANDS_H_
#define COMMANDS_H_

#include <common.h>
#include <core/cmath.h>
#include <core/r_types.h>

#include "assets/asset_types.h"
#include "renderer/r_internal.h"

typedef enum {
	// 2D
	DCT_DrawCommandSpriteBatch = 1,

	// 3D
	DCT_DrawCommandMesh,

	DCT_MAX,
} DrawCommandType;

typedef struct {
	DrawCommandType type;
	size_t size;
} DrawCommandBase;

typedef struct {
	DrawCommandBase base;

	Texture2D textures[32];
	uint32_t texture_count, quad_count;
	Vertex2 quads[];
} DrawCommandSpriteBatch;

typedef struct {
	DrawCommandBase base;

	float4x4 world_from_model;
	Mesh mesh;
	Material material;
} DrawCommandMesh;

typedef enum {
	DRAWLIST_FLAG_NO_BATCHING = 0x1,
} DrawlistFlags;

typedef struct {
	DrawlistFlags flags;

	uint8_t *push_buffer;
	size_t capacity, offset;

	DrawCommandSpriteBatch *active_batch;
} DrawlistBuffer;

typedef struct {
	uint8_t *push_buffer;
	size_t size, offset;
} ComputelistBuffer;

DrawlistBuffer *drawlist_make(Arena *arena, size_t max_size, DrawlistFlags flags);

DrawCommandBase *drawlist_push(DrawlistBuffer *list, size_t size, DrawCommandType type);
#define drawlist_push_command(list, T) (T *)drawlist_push((list), sizeof(T), DCT_##T)

// 2D
void drawlist_push_rect(DrawlistBuffer *buffer, Rectangle rect, Color color);
void drawlist_push_rectv(DrawlistBuffer *buffer, float2 position, float2 size, Color color);
void drawlist_push_texture_ex(DrawlistBuffer *buffer, Texture2D texture, Rectangle src, Rectangle dst, float2 origin, float rotation, Color tint);
void drawlist_push_text(DrawlistBuffer *buffer, Font *font, String text, float2 position, Color color);

// 3D
void drawlist_push_mesh(DrawlistBuffer *buffer, float4x4 transform, Mesh mesh, Material material);

#endif /* COMMANDS_H_ */
