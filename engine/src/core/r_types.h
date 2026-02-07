#pragma once

#include "common.h"
#include "astring.h"
#include "identifiers.h"

// clang-format off
#define RHI_HANDLE(name) typedef struct name { uint32_t id, generation; } name
#define INVALID_RHI(type) (type){ 0, 0}
RHI_HANDLE(RhiBuffer);
RHI_HANDLE(RhiGeometry);

RHI_HANDLE(RhiTexture);
RHI_HANDLE(RhiSampler);

RHI_HANDLE(RhiShader);
RHI_HANDLE(RhiShaderVariant);
RHI_HANDLE(RhiGlobalResource);
RHI_HANDLE(RhiGroupResource);

RHI_HANDLE(RhiPass);
// clang-format on

typedef Handle RTexture;
typedef Handle RShader;
typedef Handle RMaterial;
typedef Handle RMesh;

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
		float2 vec2f;
		float3 vec3f;
		float4 vec4f;

		void *raw;
	} as;
} ShaderParameter;
