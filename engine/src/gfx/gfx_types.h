#pragma once

#include "common.h"

typedef enum {
	PIXEL_FORMAT_RGBA8_UNORM,
	PIXEL_FORMAT_RGBA8_SRGB,
	PIXEL_FORMAT_RGBA16_FLOAT,
	PIXEL_FORMAT_R32_FLOAT,

	PIXEL_FORMAT_DEPTH,
	PIXEL_FORMAT_DEPTH_STENCIL,
    PIXEL_FORMAT_BACKBUFFER,
} PixelFormat;

static inline bool pixel_format_is_depth_stencil(PixelFormat format) { return format == PIXEL_FORMAT_DEPTH_STENCIL; }
static inline bool pixel_format_is_depth(PixelFormat format) { return format == PIXEL_FORMAT_DEPTH || pixel_format_is_depth_stencil(format); }

typedef enum {
	IMAGE_USAGE_SAMPLE = BIT(0),
	IMAGE_USAGE_RENDER = BIT(1),
	IMAGE_USAGE_STORAGE = BIT(2),
	IMAGE_USAGE_TRANSFER = BIT(3)
} ImageUsageFlags;

// Defalut = SAMPLE_COUNT_1
typedef enum {
	SAMPLE_COUNT_1 = BIT(0),
	SAMPLE_COUNT_2 = BIT(1),
	SAMPLE_COUNT_4 = BIT(2),
	SAMPLE_COUNT_8 = BIT(3),
	SAMPLE_COUNT_16 = BIT(4),
	SAMPLE_COUNT_32 = BIT(5),
	SAMPLE_COUNT_64 = BIT(6),
} ImageSampleCount;

typedef enum {
	BUFFER_USAGE_VERTEX = 0x1,
	BUFFER_USAGE_INDEX = 0x2,
	BUFFER_USAGE_UNIFORM = 0x4,
	BUFFER_USAGE_STORAGE = 0x8,
	BUFFER_USAGE_TRANSFER = 0x10
} BufferUsage;

typedef enum {
	BUFFER_MEMORY_LOCAL,
	BUFFER_MEMORY_SHARED,
} BufferMemory;

typedef enum {
	SHADER_STAGE_VERTEX,
	SHADER_STAGE_FRAGMENT,
	SHADER_STAGE_COMPUTE,

	SHADER_STAGE_COUNT,
} ShaderStage;

typedef struct {
	PixelFormat format;
	ImageUsageFlags usage; // zero initialized equals IMAGE_USAGE_SAMPLE | IMAGE_USAGE_TRANSFER
	ImageSampleCount sample; // zero initilized equals SAMPLE_COUNT_1
} ImageOptions;
