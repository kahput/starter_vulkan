#pragma once

#include "common.h"
#include "astring.h"
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

typedef struct {
	void *vertex_code;
	size_t vertex_code_size;

	void *fragment_code;
	size_t fragment_code_size;
} ShaderConfig;
