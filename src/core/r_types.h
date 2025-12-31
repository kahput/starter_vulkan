#pragma once

#include "common.h"
#include "astring.h"
#include "identifiers.h"

#include <cglm/cglm.h>

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

typedef struct {
	vec4 position;
	vec4 color;
} PointLight;
