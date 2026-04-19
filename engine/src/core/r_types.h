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
