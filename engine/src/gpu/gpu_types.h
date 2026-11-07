#pragma once

#include "common.h"
#include "core/rid.h"
#include "core/strings.h"

typedef enum {
	GPU_RESOURCE_NONE = 0,

	GPU_RESOURCE_BUFFER,
	GPU_RESOURCE_IMAGE,
	GPU_RESOURCE_SAMPLER,

	GPU_RESOURCE_SWAPCHAIN,

	GPU_RESOURCE_MAX,
} GPU_ResourceType;

typedef struct GPU_Buffer GPU_Buffer;
typedef struct GPU_Image GPU_Image;
typedef struct GPU_Sampler GPU_Sampler;
typedef struct GPU_Pipeline GPU_Pipeline;
typedef struct GPU_Shader GPU_Shader;
typedef struct GPU_Swapchain GPU_Swapchain;

typedef enum {
	GPU_LIMIT_UNIFORM_SETS = 4,
	GPU_LIMIT_UNIFORMS_PER_SET = 32,
	GPU_LIMIT_COLOR_ATTACHMENTS = 4,
	GPU_LIMIT_PIPELINE_PERMUTATIONS = 8,
} GPU_Limits;

typedef enum {
	UNIFORM_TYPE_IMAGE,
	UNIFORM_TYPE_STORAGE_IMAGE,
	UNIFORM_TYPE_SAMPLER,
	UNIFORM_TYPE_SAMPLER_WITH_IMAGE,

	UNIFORM_TYPE_UNIFORM_BUFFER,
	UNIFORM_TYPE_STORAGE_BUFFER,

	UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC,
	UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC,

	UNIFORM_TYPE_MAX,
} UniformType;

static string8 uniform_type_to_string[UNIFORM_TYPE_MAX] = {
	[UNIFORM_TYPE_IMAGE] = comp8("UNIFORM_TYPE_IMAGE"),
	[UNIFORM_TYPE_STORAGE_IMAGE] = comp8("UNIFORM_TYPE_STORAGE_IMAGE"),
	[UNIFORM_TYPE_SAMPLER] = comp8("UNIFORM_TYPE_SAMPLER"),
	[UNIFORM_TYPE_SAMPLER_WITH_IMAGE] = comp8("UNIFORM_TYPE_SAMPLER_WITH_IMAGE"),
	[UNIFORM_TYPE_UNIFORM_BUFFER] = comp8("UNIFORM_TYPE_UNIFORM_BUFFER"),
	[UNIFORM_TYPE_STORAGE_BUFFER] = comp8("UNIFORM_TYPE_STORAGE_BUFFER"),
	[UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC] = comp8("UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC"),
	[UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC] = comp8("UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC"),
};

typedef enum {
	PIXEL_FORMAT_RGBA8_UNORM,
	PIXEL_FORMAT_RGBA8_SRGB,

	PIXEL_FORMAT_BGRA8_SRGB,
	PIXEL_FORMAT_BGRA8_UNORM,

	PIXEL_FORMAT_RGBA16_FLOAT,
	PIXEL_FORMAT_R32_FLOAT,
	PIXEL_FORMAT_R8_UNORM,

	PIXEL_FORMAT_DEPTH,
	PIXEL_FORMAT_DEPTHSTENCIL,

	PIXEL_FORMAT_MAX,
} PixelFormat;

static inline bool pixel_format_is_depth_stencil(PixelFormat format) { return format == PIXEL_FORMAT_DEPTHSTENCIL; }
static inline bool pixel_format_is_depth(PixelFormat format) { return format == PIXEL_FORMAT_DEPTH || pixel_format_is_depth_stencil(format); }

static uint32_t pixel_format_to_stride[PIXEL_FORMAT_MAX] = {
	[PIXEL_FORMAT_RGBA8_UNORM] = 4,
	[PIXEL_FORMAT_RGBA8_SRGB] = 4,

	[PIXEL_FORMAT_BGRA8_UNORM] = 4,
	[PIXEL_FORMAT_BGRA8_SRGB] = 4,

	[PIXEL_FORMAT_RGBA16_FLOAT] = 2 * 4,
	[PIXEL_FORMAT_R32_FLOAT] = 4,
	[PIXEL_FORMAT_R8_UNORM] = 1,

	[PIXEL_FORMAT_DEPTH] = 4,
	[PIXEL_FORMAT_DEPTHSTENCIL] = 4,
};
static const string8 pixel_format_to_string[PIXEL_FORMAT_MAX] = {
	ENUM_STRING_TABLE_ENTRY(PIXEL_FORMAT, RGBA8_UNORM),
	ENUM_STRING_TABLE_ENTRY(PIXEL_FORMAT, RGBA8_SRGB),
	ENUM_STRING_TABLE_ENTRY(PIXEL_FORMAT, BGRA8_SRGB),
	ENUM_STRING_TABLE_ENTRY(PIXEL_FORMAT, BGRA8_UNORM),
	ENUM_STRING_TABLE_ENTRY(PIXEL_FORMAT, RGBA16_FLOAT),
	ENUM_STRING_TABLE_ENTRY(PIXEL_FORMAT, R32_FLOAT),
	ENUM_STRING_TABLE_ENTRY(PIXEL_FORMAT, R8_UNORM),
	ENUM_STRING_TABLE_ENTRY(PIXEL_FORMAT, DEPTH),
	ENUM_STRING_TABLE_ENTRY(PIXEL_FORMAT, DEPTHSTENCIL),
};

static const string8 pixel_format_to_display_string[PIXEL_FORMAT_MAX] = {
	[PIXEL_FORMAT_RGBA8_UNORM] = comp8("RGBA8"),
	[PIXEL_FORMAT_RGBA8_SRGB] = comp8("sRGBA8"),
	[PIXEL_FORMAT_BGRA8_SRGB] = comp8("sBGRA8"),
	[PIXEL_FORMAT_BGRA8_UNORM] = comp8("BGRA8"),
	[PIXEL_FORMAT_RGBA16_FLOAT] = comp8("RGBA16f"),
	[PIXEL_FORMAT_R32_FLOAT] = comp8("R32f"),
	[PIXEL_FORMAT_R8_UNORM] = comp8("R8"),
	[PIXEL_FORMAT_DEPTH] = comp8("Depth"),
	[PIXEL_FORMAT_DEPTHSTENCIL] = comp8("Depth Stencil"),

};

typedef enum {
	IMAGE_TYPE_2D,
	IMAGE_TYPE_3D,
	IMAGE_TYPE_CUBE,

	IMAGE_TYPE_MAX,
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
	BUFFER_USAGE_VERTEX = BIT(0),
	BUFFER_USAGE_INDEX = BIT(1),
	BUFFER_USAGE_UNIFORM = BIT(2),
	BUFFER_USAGE_STORAGE = BIT(3),
	BUFFER_USAGE_TRANSFER = BIT(4)
} BufferUsage;

typedef enum {
	MEMORY_KIND_DEVICE,
	MEMORY_KIND_HOST,

	MEMORY_KIND_MAX,
} GPU_MemoryKind;

typedef enum {
	SHADER_STAGE_VERTEX,
	SHADER_STAGE_FRAGMENT,
	SHADER_STAGE_COMPUTE,

	SHADER_STAGE_MAX,
} ShaderStage;

typedef enum {
	SADER_KIND_RASTER,
	SHADER_KIND_COMPUTE,

	SHADER_KIND_MAX,
} GPU_ShaderKind;

typedef enum {
	FILTER_NEAREST = 0,
	FILTER_LINEAR = 1
} GPU_FilterMode;

typedef enum {
	WRAP_MODE_REPEAT = 0,
	WRAP_MODE_REPEAT_MIRROR = 1,
	WRAP_MODE_CLAMP = 2,
	WRAP_MODE_CLAMP_BORDER = 3,

	WRAP_MODE_MAX,
} GPU_WrapMode;

typedef enum {
	CULL_MODE_NONE = 0,
	CULL_MODE_FRONT = 1,
	CULL_MODE_BACK = 2,
	CULL_MODE_FRONT_AND_BACK = 3,

	CULL_MODE_MAX,
} GPU_CullMode;
static const string8 cull_mode_to_string[CULL_MODE_MAX] = {
	[CULL_MODE_NONE] = comp8("CULL_MODE_NONE"),
	[CULL_MODE_FRONT] = comp8("CULL_MODE_FRONT"),
	[CULL_MODE_BACK] = comp8("CULL_MODE_BACK"),
	[CULL_MODE_FRONT_AND_BACK] = comp8("CULL_MODE_FRONT_AND_BACK"),
};

typedef enum {
	POLYGON_MODE_FILL = 0,
	POLYGON_MODE_LINE = 1,
	POLYGON_MODE_POINT = 2,

	POLYGON_MODE_MAX,
} GPU_PolygonMode;
static const string8 polygon_mode_to_string[POLYGON_MODE_MAX] = {
	[POLYGON_MODE_FILL] = comp8("POLYGON_MODE_FILL"),
	[POLYGON_MODE_LINE] = comp8("POLYGON_MODE_LINE"),
	[POLYGON_MODE_POINT] = comp8("POLYGON_MODE_POINT"),
};

typedef enum blend_factor {
	BLEND_FACTOR_ZERO = 0,
	BLEND_FACTOR_ONE = 1,

	BLEND_FACTOR_SRC_COLOR = 2,
	BLEND_FACTOR_ONE_MINUS_SRC_COLOR = 3,
	BLEND_FACTOR_DST_COLOR = 4,
	BLEND_FACTOR_ONE_MINUS_DST_COLOR = 5,

	BLEND_FACTOR_SRC_ALPHA = 6,
	BLEND_FACTOR_ONE_MINUS_SRC_ALPHA = 7,
	BLEND_FACTOR_DST_ALPHA = 8,
	BLEND_FACTOR_ONE_MINUS_DST_ALPHA = 9,

	BLEND_FACTOR_MAX,
} BlendFactor;
static const string8 blend_factor_to_string[BLEND_FACTOR_MAX] = {
	[BLEND_FACTOR_ZERO] = comp8("BLEND_FACTOR_ZERO"),
	[BLEND_FACTOR_ONE] = comp8("BLEND_FACTOR_ONE"),
	[BLEND_FACTOR_SRC_COLOR] = comp8("BLEND_FACTOR_SRC_COLOR"),
	[BLEND_FACTOR_ONE_MINUS_SRC_COLOR] = comp8("BLEND_FACTOR_ONE_MINUS_SRC_COLOR"),
	[BLEND_FACTOR_DST_COLOR] = comp8("BLEND_FACTOR_DST_COLOR"),
	[BLEND_FACTOR_ONE_MINUS_DST_COLOR] = comp8("BLEND_FACTOR_ONE_MINUS_DST_COLOR"),
	[BLEND_FACTOR_SRC_ALPHA] = comp8("BLEND_FACTOR_SRC_ALPHA"),
	[BLEND_FACTOR_ONE_MINUS_SRC_ALPHA] = comp8("BLEND_FACTOR_ONE_MINUS_SRC_ALPHA"),
	[BLEND_FACTOR_DST_ALPHA] = comp8("BLEND_FACTOR_DST_ALPHA"),
	[BLEND_FACTOR_ONE_MINUS_DST_ALPHA] = comp8("BLEND_FACTOR_ONE_MINUS_DST_ALPHA"),
};

typedef enum {
	BLEND_OP_ADD = 0,
	BLEND_OP_SUB = 1,
	BLEND_OP_REVERSE_SUB = 2,
	BLEND_OP_MIN = 3,
	BLEND_OP_MAX = 4,

	BLEND_OP_COUNT,
} BlendOp;

static const string8 blend_op_to_string[BLEND_OP_COUNT] = {
	[BLEND_OP_ADD] = comp8("BLEND_OP_ADD"),
	[BLEND_OP_SUB] = comp8("BLEND_OP_SUB"),
	[BLEND_OP_REVERSE_SUB] = comp8("BLEND_OP_REVERSE_SUB"),
	[BLEND_OP_MIN] = comp8("BLEND_OP_MIN"),
	[BLEND_OP_MAX] = comp8("BLEND_OP_MAX"),
};

typedef enum {
	LOAD_OP_LOAD = 0,
	LOAD_OP_CLEAR = 1,
	LOAD_OP_DONT_CARE = 2,
} LoadOp;

typedef enum {
	STORE_OP_STORE = 0,
	STORE_OP_DONT_CARE = 1,
} StoreOp;

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
	RESOURCE_USAGE_DEPTH_STENCIL_ATTACHMENT,

	RESOURCE_USAGE_MAX,
} ResourceUsage;

typedef enum {
	GPU_SWIZZLE_IDENTITY = 0,
	GPU_SWIZZLE_ZERO = 1,
	GPU_SWIZZLE_ONE = 2,
	GPU_SWIZZLE_R = 3,
	GPU_SWIZZLE_G = 4,
	GPU_SWIZZLE_B = 5,
	GPU_SWIZZLE_A = 6,

} GPU_Swizzle;

typedef struct {
	const char *debug_name;

	BufferUsage usage;
	GPU_MemoryKind memory;

	void *data;
} BufferOptions;

typedef struct {
	const char *debug_name;

	ImageType type;
	uint32_t slice_count, max_mip_level;
	PixelFormat format;
	ImageUsageFlags usage; // zero initializes to IMAGE_USAGE_SAMPLE | IMAGE_USAGE_TRANSFER
	ImageSampleCount sample; // zero initializes to SAMPLE_COUNT_1
	GPU_Swizzle swizzle[4];

	void *pixels;
} ImageOptions;

typedef struct {
	const char *debug_name;

	GPU_FilterMode min_filter;
	GPU_FilterMode mag_filter;

	GPU_FilterMode mipmap_filter;

	GPU_WrapMode address_mode_u;
	GPU_WrapMode address_mode_v;
	GPU_WrapMode address_mode_w;

	bool compare_enable;
} SamplerOptions;

#define sampler_opt(name, filter, mode) \
	(SamplerOptions) {                  \
		.debug_name = (name),           \
		.min_filter = (filter),         \
		.mag_filter = (filter),         \
		.mipmap_filter = (filter),      \
		.address_mode_u = (mode),       \
		.address_mode_v = (mode),       \
		.address_mode_w = (mode),       \
	}

typedef struct {
	GPU_CullMode cull_mode;
	GPU_PolygonMode polygon_mode;

	bool disable_depth_test, disable_depth_write;
	bool blend_enable, enable_stencil_test;

	BlendOp color_op, alpha_op;
	BlendFactor src_color_factor, dst_color_factor;
	BlendFactor src_alpha_factor, dst_alpha_factor;
} PipelineOptions;

typedef struct {
	PixelFormat color_formats[GPU_LIMIT_COLOR_ATTACHMENTS];
	uint32_t color_count;

	PixelFormat depth_format;
	ImageSampleCount sample_count;
} GPU_DrawTargetLayout;

typedef struct {
	const char *debug_name;

	struct {
		GPU_Image *target;
		GPU_Image *resolve;
		LoadOp load;
		StoreOp store;
		rgba8 clear;
	} colors[GPU_LIMIT_COLOR_ATTACHMENTS];
	struct {
		GPU_Image *target;
		LoadOp load;
		StoreOp store;
		float clear;
	} depth;

	Rectangle area;
} GPU_DrawPassInfo;

// ============================================================
// ========================= TEMPORARY =========================
// ============================================================

#if 1
	#define MAX_FRAMES_IN_FLIGHT 2
	#define MAX_TRANSFERS_IN_FLIGHT 2

	#include <vulkan/vulkan_core.h>

	#include "wm.h"

struct GPU_Buffer {
	RID id;

	VkBuffer handle;
	VkDeviceMemory memory;
	uint8_t *mapped;
	VkDeviceAddress address;

	uint64_t size;
	VkBufferCreateInfo info;

	BufferOptions options;
};

struct GPU_Image {
	RID id;

	VkImage handle;
	VkImageView view;
	VkDeviceMemory memory;

	uint32_t width, height, miplevels;

	ImageOptions options;
	ResourceUsage res_usage;
};

struct GPU_Sampler {
	RID id;

	VkSampler handle;
	VkSamplerCreateInfo info;
};

typedef struct {
	char name[128];
	UniformType type;

	uint32_t binding, count;
	union {
		struct {
			GPU_Buffer *handle;
			uint64_t offset, size;
			void *data;
		} buffer;

		struct {
			GPU_Image **images;
			GPU_Sampler *sampler;
		} sampler_with_textures;
	} resource;
} Uniform;

typedef struct {
	Uniform uniforms[GPU_LIMIT_UNIFORMS_PER_SET];
	uint32_t uniform_count;
} UniformSet;

// TODO: Move to this
/* static inline Uniform uniform_data(uint32_t binding, void *data, uint64_t size) { return (Uniform){ .type = UNIFORM_TYPE_UNIFORM_BUFFER, .binding = binding, .size = size, .data = data }; } */
/* static inline Uniform storage_data(uint32_t binding, void *data, uint64_t size) { return (Uniform){ .type = UNIFORM_TYPE_STORAGE_BUFFER, .binding = binding, .size = size, .data = data }; } */
/* static inline Uniform storage_image(uint32_t binding, uint32_t array_index, RID image) { return (Uniform){ .type = UNIFORM_TYPE_STORAGE_IMAGE, .binding = binding, .array_index = array_index, .resource = image }; } */
/* static inline Uniform storage_buffer(uint32_t binding, RID buffer, uint64_t offset, uint64_t size) { return (Uniform){ .type = UNIFORM_TYPE_STORAGE_BUFFER, .binding = binding, .resource = buffer, .offset = offset, .size = size }; } */
/* static inline Uniform sampled_texture(uint32_t binding, RID sampler, RID image, uint32_t array_index) { return (Uniform){ .type = UNIFORM_TYPE_SAMPLER_WITH_IMAGE, .binding = binding, .resource = image, .sampler = sampler, .array_index = array_index }; } */

static inline Uniform uniform_data(uint32_t binding, void *data, uint64_t size) {
	Uniform result = {
		.name = { 0 },
		.type = UNIFORM_TYPE_UNIFORM_BUFFER,
		.binding = binding,
		.count = 1,
		.resource.buffer = { .data = data, .size = size },
	};
	/* memory_copy(result.name, name.text, MIN(name.length, s(result.name).length)); */

	return result;
}

static inline Uniform storage_data(uint32_t binding, void *data, uint64_t size) {
	Uniform result = {
		.name = { 0 },
		.type = UNIFORM_TYPE_STORAGE_BUFFER,
		.binding = binding,
		.count = 1,
		.resource.buffer = { .data = data, .size = size },
	};
	/* memory_copy(result.name, name.text, MIN(name.length, sizeof(result.name) - 1)); */

	return result;
}

static inline Uniform storage_images(uint32_t binding, GPU_Image **images, uint32_t image_count) {
	Uniform result = {
		.name = { 0 },
		.type = UNIFORM_TYPE_STORAGE_IMAGE,
		.binding = binding,
		.count = image_count,
		.resource.sampler_with_textures.images = images,
	};
	/* memory_copy(result.name, name.text, MIN(name.length, sizeof(result.name) - 1)); */

	return result;
}

/* static inline Uniform uniform_buffer(uint32_t binding, GPU_Buffer *buffer, uint64_t offset, uint64_t size) { */
/* 	return (Uniform){ .type = UNIFORM_TYPE_UNIFORM_BUFFER, .binding = binding, .count = 1, .as.buffer = { .handle = buffer, .offset = offset, .size = size } }; */
/* } */

static inline Uniform storage_buffers(uint32_t binding, GPU_Buffer *buffer, uint64_t offset, uint64_t size) {
	Uniform result = {
		.name = { 0 },
		.type = UNIFORM_TYPE_STORAGE_BUFFER,
		.binding = binding,
		.count = 1,
		.resource.buffer = { .handle = buffer, .offset = offset, .size = size },
	};
	/* memory_copy(result.name, name.text, MIN(name.length, sizeof(result.name) - 1)); */

	return result;
}

static inline Uniform sampler_with_textures(uint32_t binding, GPU_Image **images, uint32_t image_count, GPU_Sampler *sampler) {
	Uniform result = {
		.name = { 0 },
		.type = UNIFORM_TYPE_SAMPLER_WITH_IMAGE,
		.binding = binding,
		.count = image_count,
		.resource.sampler_with_textures = { .images = images, .sampler = sampler },
	};
	/* memory_copy(result.name, name.text, MIN(name.length, sizeof(result.name) - 1)); */

	return result;
}

struct GPU_Pipeline {
	GPU_Pipeline *next;
	GPU_Shader *shader;

	GPU_DrawTargetLayout layout;
	VkPipeline handle;
};

struct GPU_Shader {
	GPU_Shader *next;
	const char *debug_name;

	VkShaderModule modules[SHADER_STAGE_MAX];

	VkPipelineLayout layout;
	VkDescriptorSetLayout layouts[GPU_LIMIT_UNIFORM_SETS];
	struct {
		UniformSet sets[GPU_LIMIT_UNIFORM_SETS];
		uint32_t set_count;
	} reflection;

	struct {
		uint64_t hash;
		PipelineOptions options;

		GPU_Pipeline *first_pipeline;
	} pipelines[GPU_LIMIT_PIPELINE_PERMUTATIONS];
	uint32_t pipeline_count;

	GPU_Pipeline *compute;
};

	#define SWAPCHAIN_IMAGE_COUNT 3
struct GPU_Swapchain {
	RID id;

	WM_Surface *native;

	VkSwapchainKHR handle;
	VkSurfaceKHR surface;

	VkSwapchainCreateInfoKHR info;

	VkImage images[SWAPCHAIN_IMAGE_COUNT];
	VkImageView views[SWAPCHAIN_IMAGE_COUNT];
	VkImageViewCreateInfo view_infos[SWAPCHAIN_IMAGE_COUNT];
	uint32_t image_count;

	GPU_Image wrapper;

	VkSemaphore image_available_semaphores[MAX_FRAMES_IN_FLIGHT]; // has to be MAX_FRAMES_IN_FLIGHT, as you need one for each frame index
	VkSemaphore render_done_semaphores[SWAPCHAIN_IMAGE_COUNT]; // has to be SWAPCHAIN_IMAGE_COUNT, as you need one for each swapchain image
};

	#define MAX_BUFFERS 1024
	#define MAX_IMAGES 512
	#define MAX_SAMPLERS 32
	#define MAX_SHADERS 32
	#define MAX_PIPELINES 1024
	#define MAX_SWAPCHAINS 8

typedef struct {
	VkCommandBuffer handle;
	VkFence in_flight_fence;

	VkDescriptorPool descriptor_pool;
	uint32_t descriptor_count;

	GPU_Swapchain *swapchains[MAX_SWAPCHAINS];
	uint32_t swapchain_image_indices[MAX_SWAPCHAINS];
	uint32_t swapchain_count;

	struct {
		struct {
			bool active;
			GPU_DrawTargetLayout layout;
		} pass;
		GPU_Shader *shader;
	} bound;

	GPU_Buffer *transient_buffer;
	Arena transient_arena[1];

	bool recording;
	uint32_t label_count;
} GPU_CommandList;

typedef struct {
	Arena arena[1];

	VkInstance instance;
	VkPhysicalDevice gpu;
	VkDevice handle;
	struct {
		VkPhysicalDeviceLimits limits;
		VkPhysicalDeviceProperties properties;
	} info;

	// Engine globals
	VkCommandPool graphics_command_pool;
	VkPushConstantRange global_range;

	GPU_Buffer *frame_staging_buffer;
	uint64_t frame_staging_buffer_slice_size;

	GPU_Buffer *transfer_staging_buffer;
	uint64_t transfer_staging_buffer_slice_size;

	VkQueue graphics_queue, present_queue;
	VkQueue transfer_queue, compute_queue;

	int32_t graphics_index, present_index;
	int32_t transfer_index, compute_index;

	// Transfer
	GPU_CommandList transfer_commands[2];
	uint32_t current_transfer_index;

	// Frame
	GPU_CommandList frame_commands[MAX_FRAMES_IN_FLIGHT];
	uint32_t current_frame_index;

	// Resources
	GPU_Buffer *buffers;
	GPU_Image *images;
	GPU_Sampler *samplers;
	GPU_Swapchain *swapchains;
	RID_Pool pools[GPU_RESOURCE_MAX];

	GPU_Shader *shader_pool;
	uint32_t shader_count;

	GPU_Pipeline *pipeline_pool;
	uint32_t pipeline_count;

	GPU_Shader *first_free_shader;
	GPU_Pipeline *first_free_pipeline;

	bool initialized;
	VkDebugUtilsMessengerEXT debug_messenger;
} GPU_Device;

#endif
