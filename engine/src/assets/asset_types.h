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

typedef struct shader_source {
	UUID id;
	String path;

	String vertex_shader, fragment_shader;
} ShaderSource;

typedef enum {
	PROPERTY_TYPE_FLOAT,
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
		float float1;
		float32_2 float2;
		float32_3 float3;
		float32_4 float4;

		uint32_t u;

		ImageSource *image;
	} as;
} MaterialProperty;

typedef struct material_source {
	UUID id;
	ShaderSource *shader;

	MaterialProperty *properties;
	uint32_t property_count;

} MaterialSource;

typedef struct {
	float32_3 position;
	float32_3 normal;
	float32_2 uv;
	float32_4 tangent;
} Vertex;

typedef struct model_source {
	MeshSource *meshes;
	uint32_t mesh_count;

	MaterialSource *materials;
	uint32_t material_count;

	ImageSource *images;
	uint32_t image_count;

	uint32_t *mesh_to_material;
} SModel;
