#ifndef COMMANDS_H_
#define COMMANDS_H_

#include <common.h>
#include <core/cmath.h>
#include <core/r_types.h>

#include "assets/asset_types.h"
#include "renderer/r_internal.h"

typedef enum {
	// 2D
	DCT_DrawCommandRectangle = 1,
	DCT_DrawCommandTexture,
	DCT_DrawCommandText,

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

	Rectangle rect;
	float2 origin;
	float rotation;
	Color color;
} DrawCommandRectangle;

typedef struct {
	DrawCommandBase base;

	RhiTexture texture;
	Rectangle src, dest;
	float2 origin;
	float rotation;
	Color tint;
} DrawCommandTexture;

typedef struct {
	DrawCommandBase base;

	float4x4 world_from_model;
	Mesh mesh;
	Material material;
} DrawCommandMesh;

typedef struct {
	uint8_t *push_buffer;
	size_t capacity, offset;
} DrawlistBuffer;

typedef struct {
	uint8_t *push_buffer;
	size_t size, offset;
} ComputelistBuffer;

DrawlistBuffer *drawlist_make(Arena *arena, size_t max_size);

DrawCommandBase *drawlist_push(DrawlistBuffer *list, size_t size, DrawCommandType type);
#define drawlist_push_command(list, T) (T *)drawlist_push((list), sizeof(T), DCT_##T)

// 2D
void drawlist_push_rect(DrawlistBuffer *list, Rectangle rect, Color color);
void drawlist_push_rectv(DrawlistBuffer *list, float2 position, float2 size, Color color);

void drawlist_push_texture_ex(DrawlistBuffer *list, RhiTexture texture, Rectangle src, Rectangle dst, float2 origin, float rotation, Color tint);

void drawlist_push_text(DrawlistBuffer *list, Font *font, const char *text, float2 position, Color color);

// 3D
void drawlist_push_mesh(DrawlistBuffer *list, float4x4 transform, Mesh mesh, Material material);

#endif /* COMMANDS_H_ */
