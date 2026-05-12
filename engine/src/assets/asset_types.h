#pragma once

#include "assets/mesh_source.h"

#include "common.h"
#include "core/cmath.h"
#include "core/strings.h"
#include "core/identifiers.h"

#include "core/r_types.h"

typedef struct image {
	void *pixels;
	int32_t width, height, channels;
} ImageSource;

typedef struct {
	Rectangle atlas_rect;
	float2 bearing;
	float advance_x;
} Glyph;

typedef struct {
	RhiTexture atlas;
	ImageSource atlas_src;

	uint32_t line_height;

	Glyph *glyphs;
	uint32_t glyph_count;
} Font;

typedef struct {
	String vertex_path, fragment_path;
	Buffer vertex, fragment;
} ShaderSource;

typedef enum {
	PROPERTY_TYPE_FLOAT1,
	PROPERTY_TYPE_FLOAT2,
	PROPERTY_TYPE_FLOAT3,
	PROPERTY_TYPE_FLOAT4,

	PROPERTY_TYPE_COLOR,

	PROPERTY_TYPE_INT,
	PROPERTY_TYPE_INT2,
	PROPERTY_TYPE_INT3,
	PROPERTY_TYPE_INT4,

	PROPERTY_TYPE_IMAGE,
} PropertyType;

typedef struct {
	String name;
	PropertyType type;
	union {
		float float32x1;
		float32x2 float32x2;
		float32x3 float32x3;
		float32x4 float32x4;

		uint32_t uint32x1;
		uint32x2 uint32x2;
	} as;
} MaterialProperty;

typedef struct material_source {
	MaterialProperty *properties;
	uint32_t property_count;
} MaterialSource;

typedef struct {
	float32x3 position;
	float32x3 normal;
	float32x2 uv0;
	float32x4 tangent;
} Vertex3;

typedef struct scene_source {
	String path;

	MeshSource *meshes;
	uint32_t mesh_count;

	MaterialSource *materials;
	uint32_t material_count;

	ImageSource *images;
	uint32_t image_count;

	uint32_t *mesh_to_material;
	Interval3 *bounding_boxes;

	uint8_t *vertices;
	size_t vertices_size;

	uint8_t *indices;
	size_t indices_size;
} SceneSource;
