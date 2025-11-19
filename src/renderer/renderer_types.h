#pragma once

#include "common.h"

#include <cglm/cglm.h>

// Shader
typedef void *Shader;

typedef struct {
	vec3 position;
	vec2 uv;
	vec3 normal;
} Vertex;

typedef struct {
	char *path;
	uint32_t width, height, channels;

	void *internal;
} Texture;

typedef struct material {
	Texture base_color_texture;
	Texture metallic_roughness_texture;

	float base_color_factor[4];
	float metallic_factor;
	float roughness_factor;
} Material;

typedef struct primitive {
	Vertex *vertices;
	uint32_t *indices;

	uint32_t vertex_count, index_count;

	Material material;
} RenderPrimitive;

typedef struct model {
	RenderPrimitive *primitives;
	uint32_t primitive_count;
} Model;

typedef enum buffer_type {
	BUFFER_TYPE_VERTEX,
	BUFFER_TYPE_INDEX,
	BUFFER_TYPE_UNIFORM,
} BufferType;

typedef enum vertex_attribute_format {
	FORMAT_FLOAT,
	FORMAT_FLOAT2,
	FORMAT_FLOAT3,
	FORMAT_FLOAT4,

	FORMAT_COUNT
} VertexAttributeFormat;

typedef struct vertex_attribute {
	const char *name;
	VertexAttributeFormat format;
	uint8_t binding;
} VertexAttribute;

typedef struct buffer {
	uint32_t vertex_count, index_count;

	void *internal;
} Buffer;
