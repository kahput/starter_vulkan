#pragma once

#include "common.h"
#include "astring.h"
#include "identifiers.h"

#include <cglm/cglm.h>

typedef Handle RTexture;
typedef Handle RShader;
typedef Handle RMaterial;
typedef Handle RMesh;

typedef enum {
	TEXTURE_FORMAT_R8,
	TEXTURE_FORMAT_R8I,
	TEXTURE_FORMAT_R8U,
	TEXTURE_FORMAT_R8S,
	TEXTURE_FORMAT_R16,
	TEXTURE_FORMAT_R16I,
	TEXTURE_FORMAT_R16U,
	TEXTURE_FORMAT_R16F,
	TEXTURE_FORMAT_R16S,
	TEXTURE_FORMAT_R32I,
	TEXTURE_FORMAT_R32U,
	TEXTURE_FORMAT_R32F,
	TEXTURE_FORMAT_RG8,
	TEXTURE_FORMAT_RG8I,
	TEXTURE_FORMAT_RG8U,
	TEXTURE_FORMAT_RG8S,
	TEXTURE_FORMAT_RG16,
	TEXTURE_FORMAT_RG16I,
	TEXTURE_FORMAT_RG16U,
	TEXTURE_FORMAT_RG16F,
	TEXTURE_FORMAT_RG16S,
	TEXTURE_FORMAT_RG32I,
	TEXTURE_FORMAT_RG32U,
	TEXTURE_FORMAT_RG32F,
	TEXTURE_FORMAT_RGB8,
	TEXTURE_FORMAT_RGB8I,
	TEXTURE_FORMAT_RGB8U,
	TEXTURE_FORMAT_RGB8S,
	TEXTURE_FORMAT_RGB9E5F,
	TEXTURE_FORMAT_BGRA8,
	TEXTURE_FORMAT_RGBA8,
	TEXTURE_FORMAT_RGBA8I,
	TEXTURE_FORMAT_RGBA8U,
	TEXTURE_FORMAT_RGBA8S,
	TEXTURE_FORMAT_RGBA16,
	TEXTURE_FORMAT_RGBA16I,
	TEXTURE_FORMAT_RGBA16U,
	TEXTURE_FORMAT_RGBA16F,
	TEXTURE_FORMAT_RGBA16S,
	TEXTURE_FORMAT_RGBA32I,
	TEXTURE_FORMAT_RGBA32U,
	TEXTURE_FORMAT_RGBA32F,
} TextureFormat;

typedef struct {
	void *pixels;
	uint32_t width;
	uint32_t height;
	uint32_t channels;
	bool is_srgb;
} TextureConfig;

typedef struct {
	void *vertices;
	uint32_t vertex_size;
	uint32_t vertex_count;

	void *indices;
	uint32_t index_size;
	uint32_t index_count;
} MeshConfig;

typedef struct {
	void *vertex_code;
	size_t vertex_code_size;

	void *fragment_code;
	size_t fragment_code_size;

	// PipelineDesc pipeline_desc;

	void *default_ubo_data;
	size_t ubo_size;
} ShaderConfig;

typedef enum {
	// SPT_FLOAT,
	// SPT_FLOAT2,
	// SPT_FLOAT3,
	// SPT_FLOAT4,
	//
	// SPT_DOUBLE,
	// SPT_DOUBLE2,
	// SPT_DOUBLE3,
	// SPT_DOUBLE4,
	//
	// SPT_INT,
	// SPT_INT2,
	// SPT_INT3,
	// SPT_INT4,

	SHADER_PARAMETER_TYPE_COLOR,
	SHADER_PARAMETER_TYPE_TEXTURE,
	SHADER_PARAMETER_TYPE_STRUCT,
} ShaderParameterType;

typedef struct {
	String name;
	ShaderParameterType type;
	size_t size;
	union {
		RTexture texture;

		float f32;
		vec2 vec2f;
		vec3 vec3f;
		vec4 vec4f;

		double d64;
		vec2 vec2d;
		vec3 vec3d;
		vec4 vec4d;

		uint32_t i32;
		ivec2 vec2i;
		ivec3 vec3i;
		ivec4 vec4i;

		void *raw;
	} as;
} ShaderParameter;

typedef enum {
	LIGHT_TYPE_DIRECTIONAL,
	LIGHT_TYPE_POINT,
} LightType;

typedef struct {
	LightType type;
	ivec3 _pad0;
	union {
		vec4 position;
		vec4 direction;
	} as;
	vec4 color;
} Light;
