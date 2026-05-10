#pragma once

#include "common.h"
#include "strings.h"
#include "identifiers.h"

// clang-format off
#define RHI_HANDLE(name) typedef struct name { uint32_t id; } name
#define INVALID_RHI(type) (type){ 0 }
RHI_HANDLE(RhiBuffer);
RHI_HANDLE(RhiTexture);
RHI_HANDLE(RhiSampler);
RHI_HANDLE(RhiShader);
RHI_HANDLE(RhiUniformSet);
// clang-format on

typedef enum {
	SHADER_ATTRIBUTE_LOCATION_POSITION,
	SHADER_ATTRIBUTE_LOCATION_NORAML,
	SHADER_ATTRIBUTE_LOCATION_UV0,
	SHADER_ATTRIBUTE_LOCATION_TANGENT,
	SHADER_ATTRIBUTE_LOCATION_COLOR,
} ShaderAttributeLocation;

typedef enum {
	TEXTURE_FORMAT_RGBA8,
	TEXTURE_FORMAT_RGBA8_SRGB,

	TEXTURE_FORMAT_RGB8,
	TEXTURE_FORMAT_RGB8_SRGB,

	TEXTURE_FORMAT_R8,
	TEXTURE_FORMAT_R32,

	TEXTURE_FORMAT_RGBA16F,
	TEXTURE_FORMAT_RGBA32F,

	TEXTURE_FORMAT_DEPTH,
	TEXTURE_FORMAT_DEPTH_STENCIL
} TextureFormat;

typedef struct {
	RhiTexture handle;
	uint32_t width, height;

	TextureFormat format;
} Texture;
typedef Texture Texture2D;

typedef struct {
	RhiBuffer handle;

	size_t vertex_offset, vertex_count;
	size_t index_offset, index_count;
} Mesh;

typedef struct {
	RhiShader shader;

	RhiBuffer uniform_buffer;
	size_t offset, size;

	RhiTexture textures[16];
	uint32_t texture_count;
} Material;
