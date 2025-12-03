#ifndef RENDERER_TYPES_H
#define RENDERER_TYPES_H

#include "common.h"
#include "core/identifiers.h"

#include <cglm/cglm.h>

typedef struct {
	mat4 view;
	mat4 projection;
} CameraUpload;

typedef struct {
	vec3 position;
	vec2 uv;
	vec3 normal;
} Vertex;

typedef struct image {
	int32_t width, height, channels;

	void *pixels;
} Image;

typedef struct {
	char *path;
	int32_t width, height, channels;

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

typedef enum shader_stage {
	SHADER_STAGE_VERTEX,
	SHADER_STAGE_FRAGMENT,
} ShaderStage;

typedef enum {
	SHADER_UNIFORM_FREQUENCY_PER_FRAME,
	SHADER_UNIFORM_FREQUENCY_PER_MATERIAL,
	SHADER_UNIFORM_FREQUENCY_PER_OBJECT,
} ShaderUniformFrequency;

typedef enum {
	SHADER_UNIFORM_UNDEFINED,
	SHADER_UNIFORM_TYPE_BUFFER,
	SHADER_UNIFORM_TYPE_CONSTANTS,
	SHADER_UNIFORM_TYPE_COMBINED_IMAGE_SAMPLER
} ShaderUniformType;

typedef enum shader_attribute_format {
	SHADER_ATTRIBUTE_FORMAT_FLOAT,
	SHADER_ATTRIBUTE_FORMAT_FLOAT2,
	SHADER_ATTRIBUTE_FORMAT_FLOAT3,
	SHADER_ATTRIBUTE_FORMAT_FLOAT4,

	SHADER_ATTRIBUTE_FORMAT_INT,
	SHADER_ATTRIBUTE_FORMAT_INT2,
	SHADER_ATTRIBUTE_FORMAT_INT3,
	SHADER_ATTRIBUTE_FORMAT_INT4,

	SHADER_ATTRIBUTE_FORMAT_UINT,
	SHADER_ATTRIBUTE_FORMAT_UINT2,
	SHADER_ATTRIBUTE_FORMAT_UINT3,
	SHADER_ATTRIBUTE_FORMAT_UINT4,

	SHADER_ATTRIBUTE_FORMAT_MAT3,
	SHADER_ATTRIBUTE_FORMAT_MAT4,

	SHADER_ATTRIBUTE_FORMAT_LAST
} ShaderAttributeFormat;

typedef struct shader_attribute {
	const char *name;
	ShaderAttributeFormat format;
	uint8_t binding;
} ShaderAttribute;

typedef struct uniform_binding {
	char name[64];
	ShaderUniformType type;
	ShaderStage stage;
	uint32_t size, count;

	uint32_t set, binding;
} ShaderUniform;

#endif
