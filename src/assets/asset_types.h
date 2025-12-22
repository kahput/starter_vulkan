#pragma once

#include "common.h"
#include "core/astring.h"
#include "platform/filesystem.h"
#include "renderer/renderer_types.h"

#include <cglm/cglm.h>

typedef enum {
	ASSET_TYPE_UNDEFINED,
	ASSET_TYPE_GEOMETRY,
	ASSET_TYPE_IMAGE,
	ASSET_TYPE_SHADER,
	ASSET_TYPE_COUNT,
} AssetType;

typedef struct image {
	UUID id;
	String path;

	void *pixels;
	int32_t width, height, channels;
} ImageSource;

typedef struct shader_source {
	UUID id;
	String path;

	FileContent vertex_shader, fragment_shader;
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
		float f;
		vec2 vecf2;
		vec3 vecf3;
		vec4 vecf4;

		int32_t i;
		ivec2 veci2;
		ivec3 veci3;
		ivec4 veci4;

		uint32_t u;

		ImageSource *image;

		uint8_t *bytes;
	} as;
} MaterialProperty;

typedef struct material_source {
	UUID id;
	ShaderSource *shader;

	PipelineDesc description;

	MaterialProperty *properties;
	uint32_t property_count;

} MaterialSource;

typedef struct mesh_source {
	UUID id;

	Vertex *vertices;
	uint32_t vertex_count;

	uint32_t *indices;
	uint32_t index_count;

	MaterialSource *material;
} MeshSource;

typedef struct model_source {
	MeshSource *meshes;
	uint32_t mesh_count;

	MaterialSource *materials;
	uint32_t material_count;

	ImageSource *images;
	uint32_t image_count;
} ModelSource;
