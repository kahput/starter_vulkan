#pragma once

#include "common.h"
#include "core/astring.h"
#include "core/identifiers.h"

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
		vec2 vec2f;
		vec3 vec3f;
		vec4 vec4f;

		int32_t i;
		ivec2 vec2i;
		ivec3 vec3i;
		ivec4 vec4i;

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
	vec3 position;
	vec3 normal;
	vec2 uv;
	vec4 tangent;
} Vertex;

typedef struct mesh_source2 {
	UUID id;

	Vertex *vertices;
	uint32_t vertex_count;

	void *indices;
	size_t index_size;
	uint32_t index_count;

	MaterialSource *material;
} MeshSource2;

typedef struct model_source {
	MeshSource2 *meshes;
	uint32_t mesh_count;

	MaterialSource *materials;
	uint32_t material_count;

	ImageSource *images;
	uint32_t image_count;
} ModelSource;
