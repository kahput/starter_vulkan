#pragma once

#include "common.h"
#include "core/strings.h"

typedef enum {
	GFX_LIMIT_UNIFORM_SETS = 4,
	GFX_LIMIT_UNIFORMS_PER_SET = 32,
	GFX_LIMIT_COLOR_ATTACHMENTS = 4,

	GFX_LIMIT_COUNT,
} GfxLimits;

typedef enum {
	UNIFORM_TYPE_IMAGE,
	UNIFORM_TYPE_STORAGE_IMAGE,
	UNIFORM_TYPE_SAMPLER,
	UNIFORM_TYPE_SAMPLER_WITH_IMAGE,

	UNIFORM_TYPE_UNIFORM_BUFFER,
	UNIFORM_TYPE_STORAGE_BUFFER,

	UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC,
	UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC,

	UNIFORM_TYPE_COUNT,
} UniformType;

static String8 uniform_type_to_string[UNIFORM_TYPE_COUNT] = {
	[UNIFORM_TYPE_IMAGE] = str_comp("UNIFORM_TYPE_IMAGE"),
	[UNIFORM_TYPE_STORAGE_IMAGE] = str_comp("UNIFORM_TYPE_STORAGE_IMAGE"),
	[UNIFORM_TYPE_SAMPLER] = str_comp("UNIFORM_TYPE_SAMPLER"),
	[UNIFORM_TYPE_SAMPLER_WITH_IMAGE] = str_comp("UNIFORM_TYPE_SAMPLER_WITH_IMAGE"),
	[UNIFORM_TYPE_UNIFORM_BUFFER] = str_comp("UNIFORM_TYPE_UNIFORM_BUFFER"),
	[UNIFORM_TYPE_STORAGE_BUFFER] = str_comp("UNIFORM_TYPE_STORAGE_BUFFER"),
	[UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC] = str_comp("UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC"),
	[UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC] = str_comp("UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC"),
};

typedef struct {
	UniformType type;
	uint32_t binding;
	uint32_t count;
} ShaderBinding;

typedef enum {
	PIXEL_FORMAT_RGBA8_UNORM,
	PIXEL_FORMAT_RGBA8_SRGB,

	PIXEL_FORMAT_BGRA8_SRGB,
	PIXEL_FORMAT_BGRA8_UNORM,

	PIXEL_FORMAT_RGBA16_FLOAT,
	PIXEL_FORMAT_R32_FLOAT,

	PIXEL_FORMAT_DEPTH,
	PIXEL_FORMAT_DEPTHSTENCIL,

	PIXEL_FORMAT_COUNT,
} PixelFormat;

static inline bool pixel_format_is_depth_stencil(PixelFormat format) { return format == PIXEL_FORMAT_DEPTHSTENCIL; }
static inline bool pixel_format_is_depth(PixelFormat format) { return format == PIXEL_FORMAT_DEPTH || pixel_format_is_depth_stencil(format); }

static uint32_t pixel_format_to_stride[PIXEL_FORMAT_COUNT] = {
	[PIXEL_FORMAT_RGBA8_UNORM] = 4,
	[PIXEL_FORMAT_RGBA8_SRGB] = 4,

	[PIXEL_FORMAT_BGRA8_UNORM] = 4,
	[PIXEL_FORMAT_BGRA8_SRGB] = 4,

	[PIXEL_FORMAT_RGBA16_FLOAT] = 2 * 4,
	[PIXEL_FORMAT_R32_FLOAT] = 4,
	[PIXEL_FORMAT_DEPTH] = 4,
	[PIXEL_FORMAT_DEPTHSTENCIL] = 4,
};

static const String8 pixel_format_to_string[PIXEL_FORMAT_COUNT] = {
	ENUM_STRING_TABLE_ENTRY(PIXEL_FORMAT, RGBA8_UNORM),
	ENUM_STRING_TABLE_ENTRY(PIXEL_FORMAT, RGBA8_SRGB),
	ENUM_STRING_TABLE_ENTRY(PIXEL_FORMAT, BGRA8_SRGB),
	ENUM_STRING_TABLE_ENTRY(PIXEL_FORMAT, BGRA8_UNORM),
	ENUM_STRING_TABLE_ENTRY(PIXEL_FORMAT, RGBA16_FLOAT),
	ENUM_STRING_TABLE_ENTRY(PIXEL_FORMAT, R32_FLOAT),
	ENUM_STRING_TABLE_ENTRY(PIXEL_FORMAT, DEPTH),
	ENUM_STRING_TABLE_ENTRY(PIXEL_FORMAT, DEPTHSTENCIL),
};

typedef enum {
	IMAGE_TYPE_2D,
	IMAGE_TYPE_3D,
	IMAGE_TYPE_CUBE,

	IMAGE_TYPE_COUNT,
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
	MEMORY_TYPE_GPU,
	MEMORY_TYPE_CPU,

	MEMORY_TYPE_COUNT,
} MemoryType;

typedef enum {
	SHADER_STAGE_VERTEX,
	SHADER_STAGE_FRAGMENT,
	SHADER_STAGE_COMPUTE,

	SHADER_STAGE_COUNT,
} ShaderStage;

typedef enum sampler_filter {
	FILTER_NEAREST = 0,
	FILTER_LINEAR = 1
} FilterMode;

typedef enum sampler_address_mode {
	WRAP_MODE_REPEAT = 0,
	WRAP_MODE_REPEAT_MIRROR = 1,
	WRAP_MODE_CLAMP = 2,
	WRAP_MODE_CLAMP_BORDER = 3,

	WRAP_MODE_COUNT,
} WrapMode;

typedef enum cull_mode {
	CULL_MODE_NONE = 0,
	CULL_MODE_FRONT = 1,
	CULL_MODE_BACK = 2,
	CULL_MODE_FRONT_AND_BACK = 3
} CullMode;

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
} BlendFactor;

typedef struct {
	const char *debug_name;

	BufferUsage usage;
	MemoryType memory;

	void *data;
} BufferOptions;

typedef struct {
	const char *debug_name;

	ImageType type;
	uint32_t slice_count, max_mip_level;
	PixelFormat format;
	ImageUsageFlags usage; // zero initializes to IMAGE_USAGE_SAMPLE | IMAGE_USAGE_TRANSFER
	ImageSampleCount sample; // zero initializes to SAMPLE_COUNT_1

	void *pixels;
} ImageOptions;

typedef struct {
	const char *debug_name;

	FilterMode min_filter;
	FilterMode mag_filter;

	FilterMode mipmap_filter;

	WrapMode address_mode_u;
	WrapMode address_mode_v;
	WrapMode address_mode_w;

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
	const char *debug_name;

	ImageSampleCount sample_count;
	CullMode cull_mode;

	PixelFormat color_attachments[GFX_LIMIT_COLOR_ATTACHMENTS];
	uint32_t color_attachment_count;

	PixelFormat depth_attachment;
	bool disable_depth_test, disable_depth_write;

	bool enable_blend;
	BlendFactor src_color_factor, dst_color_factor;
	BlendFactor src_alpha_factor, dst_alpha_factor;
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

	RESOURCE_USAGE_COUNT,
} ResourceUsage;

// ============================================================
// ========================= TEMPORARY =========================
// ============================================================

#if 1
	#define MAX_FRAMES_IN_FLIGHT 2
	#define MAX_TRANSFERS_IN_FLIGHT 2

	#include <vulkan/vulkan_core.h>
	#include "os.h"

typedef struct GFX_Buffer GFX_Buffer;
struct GFX_Buffer {
	GFX_Buffer *next;

	VkBuffer handle;
	VkDeviceMemory memory;
	uint8_t *mapped;
	VkDeviceAddress address;

	uint64_t size;
	VkBufferCreateInfo info;

	BufferOptions options;
};

typedef struct GFX_Image GFX_Image;
struct GFX_Image {
	GFX_Image *next;
    // TEMP_START
    uint32_t imageid;
    // TEMP_END

	VkImage handle;
	VkImageView view;
	VkDeviceMemory memory;

	uint32_t width, height, miplevels;

	ImageOptions options;
	ResourceUsage res_usage;
};

typedef struct GFX_Sampler GFX_Sampler;
struct GFX_Sampler {
	GFX_Sampler *next;

	VkSampler handle;
	VkSamplerCreateInfo info;
};

typedef struct {
	char name[128];

	UniformType type;
	uint32_t binding, count;

	union {
		struct {
			GFX_Buffer *handle;
			uint64_t offset, size;
			void *data;
		} buffer;

		struct {
			GFX_Image **images;
			GFX_Sampler *sampler;
		} sampler_with_textures;
	} resource;
} Uniform;

typedef struct {
	Uniform uniforms[GFX_LIMIT_UNIFORMS_PER_SET];
	uint32_t uniform_count;
} UniformSet;

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

static inline Uniform storage_images(uint32_t binding, GFX_Image **images, uint32_t image_count) {
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

/* static inline Uniform uniform_buffer(uint32_t binding, GFX_Buffer *buffer, uint64_t offset, uint64_t size) { */
/* 	return (Uniform){ .type = UNIFORM_TYPE_UNIFORM_BUFFER, .binding = binding, .count = 1, .as.buffer = { .handle = buffer, .offset = offset, .size = size } }; */
/* } */

static inline Uniform storage_buffers(uint32_t binding, GFX_Buffer *buffer, uint64_t offset, uint64_t size) {
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

static inline Uniform sampler_with_textures(uint32_t binding, GFX_Image **images, uint32_t image_count, GFX_Sampler *sampler) {
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

typedef struct Shader GFX_Pipeline;
struct Shader {
	GFX_Pipeline *next;

	VkPipeline handle;
	VkPipelineLayout layout;
	VkShaderModule shaders[SHADER_STAGE_COUNT];

	VkDescriptorSetLayout set_layouts[GFX_LIMIT_UNIFORM_SETS];
	UniformSet set_infos[GFX_LIMIT_UNIFORM_SETS];

	PipelineOptions options;
};

	#define SWAPCHAIN_IMAGE_COUNT 3
typedef struct Swapchain GFX_Swapchain;
struct Swapchain {
	GFX_Swapchain *next;

	OS_Surface *native;

	VkSwapchainKHR handle;
	VkSurfaceKHR surface;

	VkSwapchainCreateInfoKHR info;

	VkImage images[SWAPCHAIN_IMAGE_COUNT];
	VkImageView views[SWAPCHAIN_IMAGE_COUNT];
	VkImageViewCreateInfo view_infos[SWAPCHAIN_IMAGE_COUNT];
	uint32_t image_count;

	GFX_Image wrapper;

	VkSemaphore image_available_semaphores[MAX_FRAMES_IN_FLIGHT]; // has to be MAX_FRAMES_IN_FLIGHT, as you need one for each frame index
	VkSemaphore render_done_semaphores[SWAPCHAIN_IMAGE_COUNT]; // has to be SWAPCHAIN_IMAGE_COUNT, as you need one for each swapchain image
};

	#define MAX_BUFFERS 1024
	#define MAX_IMAGES 512
	#define MAX_SAMPLERS 32
	#define MAX_SHADERS 32
	#define MAX_SWAPCHAINS 8
/* #define MAX_SAMPLERS 32 */
/* #define MAX_BINDSETS 4096 */

typedef struct {
	VkPhysicalDevice physical;
	VkDevice logical;

	VkPhysicalDeviceLimits limits;
	VkPhysicalDeviceProperties properties;
} VulkanDevice;

typedef struct {
	VkCommandBuffer handle;
	VkFence in_flight_fence;

	VkDescriptorPool descriptor_pool;
	uint32_t descriptor_count;

	GFX_Swapchain *swapchains[MAX_SWAPCHAINS];
	uint32_t swapchain_image_indices[MAX_SWAPCHAINS];
	uint32_t swapchain_count;

	GFX_Buffer *transient_buffer;
	Arena transient_arena[1];

	uint32_t frame_index, recording;
} GFX_CommandContext;

typedef struct {
	Arena arena[1];

	VkInstance instance;
	VulkanDevice device;

	// Engine globals
	VkCommandPool graphics_command_pool;
	VkPushConstantRange global_range;

	GFX_Buffer *frame_staging_buffer;
	uint64_t frame_staging_buffer_slice_size;

	GFX_Buffer *transfer_staging_buffer;
	uint64_t transfer_staging_buffer_slice_size;

	VkQueue graphics_queue, present_queue;
	VkQueue transfer_queue, compute_queue;

	int32_t graphics_index, present_index;
	int32_t transfer_index, compute_index;

	// Transfer
	GFX_CommandContext transfer_commands[2];
	uint32_t current_transfer_index;

	// Frame
	GFX_CommandContext frame_commands[MAX_FRAMES_IN_FLIGHT];
	uint32_t current_frame_index;

	// Resources
	GFX_Buffer *buffer_pool;
	uint32_t buffer_count;

	GFX_Image *image_pool;
	uint32_t image_count;

	GFX_Sampler *sampler_pool;
	uint32_t sampler_count;

	GFX_Pipeline *shader_pool;
	uint32_t shader_count;

	GFX_Swapchain *swapchain_pool;
	uint32_t swapchain_count;

	GFX_Buffer *first_free_buffer;
	GFX_Image *first_free_image;
	GFX_Sampler *first_free_sampler;
	GFX_Pipeline *first_free_shader;
	GFX_Swapchain *first_free_swapchain;

	bool initialized;
	#ifdef DEV_BUILD
	VkDebugUtilsMessengerEXT debug_messenger;
	#endif
} GFX_Context;

typedef struct Image2D {
	GFX_Image *handle;
	uint8_t *pixels;

	// METADATA
	ImageType type;
	PixelFormat format;
	uint32_t width, height;
} Image2D;
#endif
