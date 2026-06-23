#pragma once

#include "common.h"

typedef enum {
	RESOURCE_TYPE_BUFFER,
	RESOURCE_TYPE_IMAGE,
	RESOURCE_TYPE_SAMPLER,
	RESOURCE_TYPE_PIPELINE,
} ResourceType;

typedef enum {
	PIXELFORMAT_RGBA8_UNORM,
	PIXELFORMAT_RGBA8_SRGB,
	PIXELFORMAT_RGBA16_FLOAT,
	PIXELFORMAT_R32_FLOAT,

	PIXELFORMAT_DEPTH,
	PIXELFORMAT_DEPTH_STENCIL,
	PIXELFORMAT_BACKBUFFER,
} PixelFormat;

static inline bool pixel_format_is_depth_stencil(PixelFormat format) { return format == PIXELFORMAT_DEPTH_STENCIL; }
static inline bool pixel_format_is_depth(PixelFormat format) { return format == PIXELFORMAT_DEPTH || pixel_format_is_depth_stencil(format); }

typedef enum {
	IMAGE_TYPE_2D,
	IMAGE_TYPE_3D,
	IMAGE_TYPE_CUBE,
} ImageType;

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

typedef enum sampler_filter {
	FILTER_NEAREST = 0,
	FILTER_LINEAR = 1
} SamplerFilterMode;

typedef enum sampler_address_mode {
	WRAP_MODE_REPEAT = 0,
	WRAP_MODE_REPEAT_MIRROR = 1,
	WRAP_MODE_CLAMP = 2,
	WRAP_MODE_CLAMP_BORDER = 3,

	WRAP_MODE_COUNT,
} SamplerWrapMode;

typedef enum cull_mode {
	CULL_MODE_NONE = 0,
	CULL_MODE_FRONT = 1,
	CULL_MODE_BACK = 2,
	CULL_MODE_FRONT_AND_BACK = 3
} PipelineCullMode;

typedef struct {
	const char *debug_name;

	ImageType type;
	uint32_t slice_count;
	PixelFormat format;
	ImageUsageFlags usage; // zero initialized equals IMAGE_USAGE_SAMPLE | IMAGE_USAGE_TRANSFER
	ImageSampleCount sample; // zero initilized equals SAMPLE_COUNT_1
} ImageOptions;

typedef struct {
	const char *debug_name;

	SamplerFilterMode min_filter;
	SamplerFilterMode mag_filter;

	SamplerFilterMode mipmap_filter;

	SamplerWrapMode address_mode_u;
	SamplerWrapMode address_mode_v;
	SamplerWrapMode address_mode_w;

	bool compare_enable;
} SamplerOptions;

#define sampler_opt(filter, mode)  \
	(SamplerOptions) {             \
		.min_filter = (filter),    \
		.mag_filter = (filter),    \
		.mipmap_filter = (filter), \
		.address_mode_u = (mode),  \
		.address_mode_v = (mode),  \
		.address_mode_w = (mode),  \
	}

#define MAX_COLOR_ATTACHMENTS 8
typedef struct {
	ImageSampleCount sample_count;
	PipelineCullMode cull_mode;

	PixelFormat color_attachments[MAX_COLOR_ATTACHMENTS];
	uint32_t color_attachment_count;

	PixelFormat depth_attachment;
	bool disable_depth_test, disable_depth_write;
} PipelineOptions;

typedef enum {
	RESOURCE_USAGE_UNDEFINED,

	RESOURCE_USAGE_TRANSFER_SRC,
	RESOURCE_USAGE_SHADER_READ,
	RESOURCE_USAGE_VERTEX_SHADER_READ,
	RESOURCE_USAGE_FRAGMENT_SHADER_READ,
	RESOURCE_USAGE_COMPUTE_SHADER_READ,
	RESOURCE_USAGE_VERTEX_BUFFER,
	RESOURCE_USAGE_INDEX_BUFFER,
	RESOURCE_USAGE_PRESENT,

	RESOURCE_USAGE_TRANSFER_DST,
	RESOURCE_USAGE_SHADER_WRITE,
	RESOURCE_USAGE_VERTEX_SHADER_WRITE,
	RESOURCE_USAGE_FRAGMENT_SHADER_WRITE,
	RESOURCE_USAGE_COMPUTE_SHADER_WRITE,
	RESOURCE_USAGE_COLOR_ATTACHMENT,
	RESOURCE_USAGE_DEPTH_ATTACHMENT,
} ResourceUsage;
