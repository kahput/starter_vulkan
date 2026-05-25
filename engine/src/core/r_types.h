#pragma once

#include "common.h"
#include "strings.h"
#include "identifiers.h"

// clang-format off
#define RHI_HANDLE(name) typedef struct name { uint32_t id; } name
#define INVALID_RHI(type) (type){ 0 }
RHI_HANDLE(RhiBuffer);
RHI_HANDLE(RhiImage);
RHI_HANDLE(RhiSampler);
RHI_HANDLE(RhiShader);
RHI_HANDLE(RhiUniformSet);
// clang-format on

// TEMP
/* typedef enum { */
/* 	SHADER_ATTRIBUTE_LOCATION_POSITION, */
/* 	SHADER_ATTRIBUTE_LOCATION_NORAML, */
/* 	SHADER_ATTRIBUTE_LOCATION_UV0, */
/* 	SHADER_ATTRIBUTE_LOCATION_TANGENT, */
/* 	SHADER_ATTRIBUTE_LOCATION_COLOR, */
/* } ShaderAttributeLocation; */

#define ENUM_NAME_TABLE_ENTRY(value) [IMAGE_FORMAT_##value] = #value
typedef enum {
	IMAGE_FORMAT_RGBA8,
	IMAGE_FORMAT_RGBA8_SRGB,

	IMAGE_FORMAT_RGB8,
	IMAGE_FORMAT_RGB8_SRGB,

	IMAGE_FORMAT_R8,
	IMAGE_FORMAT_R32,

	IMAGE_FORMAT_RGBA16F,
	IMAGE_FORMAT_RGBA32F,

	IMAGE_FORMAT_DEPTH,
	IMAGE_FORMAT_DEPTH_STENCIL,

	IMAGE_FORMAT_MAX,
} ImageFormat;

static const char *image_format_to_string[IMAGE_FORMAT_MAX] = {
	ENUM_NAME_TABLE_ENTRY(RGBA8),
	ENUM_NAME_TABLE_ENTRY(RGBA8_SRGB),
	ENUM_NAME_TABLE_ENTRY(RGB8),
	ENUM_NAME_TABLE_ENTRY(RGB8_SRGB),
	ENUM_NAME_TABLE_ENTRY(R8),
	ENUM_NAME_TABLE_ENTRY(R32),
	ENUM_NAME_TABLE_ENTRY(RGBA16F),
	ENUM_NAME_TABLE_ENTRY(RGBA32F),
	ENUM_NAME_TABLE_ENTRY(DEPTH),
	ENUM_NAME_TABLE_ENTRY(DEPTH_STENCIL),
};

typedef struct {
	RhiImage handle;
	uint32_t width, height;

	ImageFormat format;
} Image;
typedef Image Image2D;

typedef struct {
	RhiBuffer handle;

	size_t vertex_offset, vertex_count;
	size_t index_offset, index_count;
} Mesh;

typedef struct {
	RhiShader shader;

	RhiBuffer uniform_buffer;
	size_t offset, size;

	Image2D images[16];
	uint32_t image_count;
} Material;

typedef enum {
	SHADER_PARAMETER_FLOAT,
	SHADER_PARAMETER_FLOAT2,
	SHADER_PARAMETER_FLOAT3,
	SHADER_PARAMETER_FLOAT4,

	SHADER_PARAMETER_FLOAT3x3,
	SHADER_PARAMETER_FLOAT4x4,
} ShaderParameterType;

typedef enum {
	SHADER_UNIFORM_FREQUENCY_PER_FRAME,
	SHADER_UNIFORM_FREQUENCY_PER_MATERIAL,
	SHADER_UNIFORM_FREQUENCY_PER_OBJECT,

	SHADER_UNIFORM_FREQUENCY_COUNT,
} ShaderUniformFrequency;

typedef enum {
	SHADER_BINDING_UNDEFINED,
	SHADER_BINDING_UNIFORM_BUFFER,
	SHADER_BINDING_STORAGE_BUFFER,

	SHADER_BINDING_IMAGE_2D,
	SHADER_BINDING_IMAGE_CUBE,
	SHADER_BINDING_SAMPLER,

} ShaderBindingType;

typedef struct shader_buffer_member {
	String name;
	size_t offset, size;

	// TODO: support these
	/* ShaderParameterType type; */
	/* uint32_t array_count, array_stride; */
} ShaderBufferMember;

typedef struct shader_buffer {
	String name;
	size_t size;

	ShaderBufferMember *members;
	uint32_t member_count;
} ShaderBuffer;

typedef struct shader_binding {
	String name;
	ShaderBindingType type;
	uint32_t binding_number, count;

	ShaderBuffer *buffer_layout;
} ShaderBinding;

typedef struct {
	struct {
		ShaderBinding *bindings;
		uint32_t binding_count;
	} sets[SHADER_UNIFORM_FREQUENCY_COUNT];
} ShaderReflection;

typedef struct {
	RhiShader handle;
	ShaderReflection reflection;
} Shader;
