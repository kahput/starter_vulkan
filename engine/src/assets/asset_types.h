#pragma once

#include "assets/mesh_source.h"

#include "common.h"
#include "core/cmath.h"
#include "core/strings.h"
#include "core/identifiers.h"

typedef enum {
	ASSET_TYPE_UNDEFINED,
	ASSET_TYPE_GEOMETRY,
	ASSET_TYPE_IMAGE,
	ASSET_TYPE_SHADER,
	ASSET_TYPE_COUNT,
} AssetType;

typedef struct image {
	void *pixels;
	int32_t width, height, channels;
} ImageSource;

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
	float _pad0;
	float32x3 normal;
	float _pad1;
	float32x2 uv;
	float2 _pad2;
	float32x4 tangent;
	float32x4 color;
} Vertex;

typedef struct scene_source {
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
