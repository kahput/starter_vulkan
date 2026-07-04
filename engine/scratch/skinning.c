#include "common.h"
#include "core/arena.h"
#include "core/cmath.h"
#include "core/geom.h"
#include "core/input_types.h"
#include "core/logger.h"
#include "core/debug.h"

#include "core/strings.h"

#include "input.h"
#include "os.h"
#include "anim.h"
#include "gfx/gfx_types.h"
#include "scene.h"
#include "spirv_reflect/spirv_reflect.h"

#include <math.h>

#include <vulkan/vulkan.h>
#include <cgltf/cgltf.h>
#include <stb/stb_image.h>
#include <vulkan/vulkan_core.h>

#define MAX_FRAMES_IN_FLIGHT 2
#define MAX_TRANSFERS_IN_FLIGHT 2

// clang-format off
typedef struct { uint16_t index, gen; } BufferID;
typedef struct { uint16_t index, gen; } ImageID;
typedef struct { uint16_t index, gen; } SamplerID;
typedef struct { uint16_t index, gen; } SurfaceID;
typedef struct { uint16_t index, gen; } ShaderID;
typedef struct { uint16_t index, gen; } PipelineID;
// clang-format on

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
	Uniform uniforms[32];
	uint32_t uniform_count;
} UniformSet;

#define MAX_UNIFORM_SETS 4
typedef struct Shader GFX_Pipeline;
struct Shader {
	GFX_Pipeline *next;

	VkPipeline handle;
	VkPipelineLayout layout;
	VkShaderModule shaders[SHADER_STAGE_COUNT];

	VkDescriptorSetLayout set_layouts[MAX_UNIFORM_SETS];
	UniformSet set_infos[MAX_UNIFORM_SETS];

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

bool gfx_startup(GFX_Context *context);
void gfx_shutdown(GFX_Context *context);

GFX_CommandContext *gfx_frame_begin(GFX_Context *context);
bool gfx_frame_end(GFX_Context *context, GFX_CommandContext *cmd);

GFX_CommandContext *gfx_transfer_cmd(GFX_Context *context);
bool gfx_transfer_flush(GFX_Context *context);

typedef struct {
	float2 position, uv;
	float4 color;
} Vertex2;

typedef struct {
	float3 position;
	float thickness;
	uint32_t color;
	float3 _pad0;
} Line;

typedef struct {
	float3 position;
	float _pad0;
	float3 normal;
	float _pad1;
	float2 uv;
	float4 tangent;
} Vertex3;

typedef struct {
	uint32x4 bone_ids;
	float32x4 weights;
} SkinningData;

GFX_Buffer *gfx_buffer_make(GFX_Context *context, uint64_t size, BufferOptions options);
GFX_Image *gfx_image_make(GFX_Context *context, uint32_t width, uint32_t height, ImageOptions options);
GFX_Sampler *gfx_sampler_make(GFX_Context *context, SamplerOptions opt);
GFX_Swapchain *gfx_swapchain_make(GFX_Context *context, OS_Surface *surface, const char *debug_name);

GFX_Pipeline compute_pipeline_make(GFX_Context *context, String8 bytecode);
GFX_Pipeline graphics_pipeline_make(GFX_Context *context, String8 vs_bytecode, String8 fs_bytecode, PipelineOptions options);

bool gfx_buffer_destroy(GFX_Context *context, GFX_Buffer *buffer);
bool gfx_image_destroy(GFX_Context *context, GFX_Image *image);
bool gfx_sampler_destroy(GFX_Context *context, GFX_Sampler *sampler);
bool gfx_swapchain_destroy(GFX_Context *context, GFX_Swapchain *surface);
void pipeline_destroy(GFX_Context *context, GFX_Pipeline *pipeline);

GFX_Image *gfx_backbuffer(GFX_Context *context, GFX_CommandContext *cmd, GFX_Swapchain *swapchain);
void gfx_present(GFX_Context *context, GFX_Swapchain *swapchain, GFX_Image *image);
bool gfx_swapchain_resize(GFX_Context *context, GFX_Swapchain *swapchain, uint32_t new_width, uint32_t new_height);
bool gfx_image_resize(GFX_Context *context, GFX_Image *image, uint32_t new_width, uint32_t new_height);

bool gfx_bind(GFX_Context *context, GFX_Pipeline *pipeline, uint32_t set, Uniform *uniforms, uint32_t uniform_count);

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

// pushes data to command context staging ring buffer, aligned to 256
uint64_t gfx_cmd_put(GFX_CommandContext *cmd, uint64_t size, void *src);
void gfx_cmd_buffer_to_buffer(GFX_CommandContext *cmd, GFX_Buffer *dst, GFX_Buffer *src, uint64_t dst_offset, uint64_t src_offset, uint64_t size);
void gfx_cmd_buffer_to_image(GFX_CommandContext *cmd, GFX_Image *dst, GFX_Buffer *src, uint64_t src_offset, uint32_t width, uint32_t height);
void gfx_cmd_buffer_barrier(GFX_CommandContext *cmd, ResourceUsage src, ResourceUsage dst, uint64_t offset, uint64_t size, GFX_Buffer *target);
bool gfx_cmd_image_barrier(GFX_CommandContext *cmd, ResourceUsage src, ResourceUsage dst, uint32_t base_miplevel, uint32_t level_count, GFX_Image *target);
bool gfx_cmd_image_transition(GFX_CommandContext *cmd, ResourceUsage dst, GFX_Image *target);
void gfx_cmd_image_blit(GFX_CommandContext *cmd, Rectangle source_rect, GFX_Image *source, Rectangle target_rect, GFX_Image *target);
void gfx_cmd_image_upload(GFX_CommandContext *cmd, GFX_Image *image, uint32_t width, uint32_t height, void *pixels);
void gfx_cmd_buffer_upload(GFX_CommandContext *cmd, GFX_Buffer *buffer, uint64_t offset, uint64_t size, void *data);

void gfx_cmd_pipeline_bind(GFX_CommandContext *cmd, GFX_Pipeline *pipeline);
void gfx_cmd_dispatch(GFX_CommandContext *context, uint32_t x, uint32_t y, uint32_t z);

typedef enum {
	TEXTURE_SLOT_ALBEDO,
	TEXTURE_SLOT_METAL_ROUGHNESS,
	TEXTURE_SLOT_NORMAL,
	TEXTURE_SLOT_OCCLUSION,
	TEXTURE_SLOT_EMISSIVE,

	TEXTURE_SLOT_COUNT,
} TextureSlot;

typedef struct {
	// CPU
	uint8_t *pixels;

	// GPU
	GFX_Image *gpu;

	// METADATA
	ImageType type;
	PixelFormat format;
	uint32_t width, height;
} Image2D;

typedef struct {
	Image2D textures[TEXTURE_SLOT_COUNT];

	float4 tint, emissive;
	float2 metallic_roughness;
} Material;

typedef struct {
	uint32_t vertex_offset, index_offset;
	uint32_t vertex_count, index_count;
	uint32_t material_id;

	AABB3 bounds;
} MeshPart;

typedef struct {
	// CPU
	Vertex3 *vertices;
	SkinningData *skinning;
	uint32_t *indices;

	// GPU
	GFX_Buffer *buffer;
	uint64_t buffer_vertex_byte_offset;
	uint64_t buffer_index_byte_offset;
	uint64_t buffer_skinning_data_byte_offset;

	Material *materials;
	uint32_t material_count;

	// METADATA
	MeshPart *parts;
	uint32_t part_count;
	Skeleton skeleton;

	uint64_t total_vertex_count;
	uint64_t total_index_count;

	AABB3 bounds;
} Mesh;

typedef enum {
	MESH_HERO_MALE,
	MESH_GDBOT,
	MESH_MAGE,
	MESH_BARREL,
	MESH_ROOM,
	MESH_TEST_LEVEL,

	MESH_ROOM_LARGE,
	MESH_TERRAIN_FLAT,
	MESH_TERRAIN_HEIGHTMAP,
	MESH_GRASS_BILLBOARD,

	MESH_COUNT,
} MeshID;

typedef struct {
	MeshID id;
	Transform3 transform;
	bool cast_shadow;

	// anim
	uint64_t skinned_vertices_offset;
	float4x4 *skin_matrices;
} MeshInstance;

String8 meshid_to_metadata[MESH_COUNT] = {
	[MESH_HERO_MALE] = str_comp("assets/models/hero_male.glb"),
	[MESH_GDBOT] = str_comp("assets/models/gdbot.glb"),
	[MESH_MAGE] = str_comp("assets/models/mage.glb"),
	[MESH_BARREL] = str_comp("assets/models/barrel.glb"),
	[MESH_ROOM] = str_comp("assets/models/room.glb"),
	[MESH_ROOM_LARGE] = str_comp("assets/models/room-large.glb"),
	[MESH_TEST_LEVEL] = str_comp("assets/models/test_level.glb"),
	[MESH_GRASS_BILLBOARD] = str_comp("assets/models/grass.glb"),
};

typedef enum {
	ENTITY_FEATURE_DRAW_MESH,
	ENTITY_FEATURE_CAST_SHADOW,

	// Gameplay
	ENTITY_FEATURE_PLAYER_CONTROLLED,
	ENTITY_FEATURE_INTERACTABLE,
	ENTITY_FEATURE_COLLIDABLE,
	ENTITY_FEATURE_FOLLOW_TARGET,

	ENTITY_FEATURE_ANIMATE,

	ENTITY_FEATURE_MAX,
} EntityFeature;

#define ENTITY_BITSET_WIDTH 64
#define ENTITY_BITSET_SIZE (ENTITY_FEATURE_MAX + 63) / 64
typedef uint64_t FeatureBitset[ENTITY_BITSET_SIZE];
typedef struct Entity {
	FeatureBitset features;

	MeshID meshid;
	Transform3 transform;

	// skinning
	uint64_t skinned_vertices_offset;
	float4x4 *skin_matrices;

	uint32_t current_anim;
	float anim_t, blend_t;

	// gameplay
	float interact_radius;
	Shape shape;

	float move_speed;

	struct Entity *target;
} Entity;

#define MAX_ENTITIES 1024
typedef struct {
	Entity entities[MAX_ENTITIES];
	uint32_t entity_count;
} World;

typedef enum {
	AXIS_PLANE_RIGHT,
	AXIS_PLANE_LEFT,
	AXIS_PLANE_UP,
	AXIS_PLANE_DOWN,
	AXIS_PLANE_FORWARD,
	AXIS_PLANE_BACKWARD,

	AXIS_PLANE_COUNT,
} AxisPlane;

typedef enum {
	AXIS_X,
	AXIS_Y,
	AXIS_Z,
	AXIS_XY = AXIS_Z,
	AXIS_XYZ,
} Axis;
/* typedef enum { */
/* 	AXIS_MODE_X = BIT(0), */
/* 	AXIS_MODE_Y = BIT(1), */
/* 	AXIS_MODE_Z = BIT(2), */
/* 	AXIS_MODE_XY = AXIS_MODE_X | AXIS_MODE_Y, */
/* 	AXIS_MODE_YZ = AXIS_MODE_Y | AXIS_MODE_Z, */
/* 	AXIS_MODE_ZX = AXIS_MODE_Z | AXIS_MODE_X, */
/* 	AXIS_MODE_XYZ = AXIS_MODE_X | AXIS_MODE_Y | AXIS_MODE_Z, */
/* } AxisMode; */

// :functions
Image2D load_image(Arena *arena, String8 path);
Image2D load_cubemap(Arena *arena, String8 paths[], uint32_t count);
Mesh load_gltf(Arena *arena, String8 path);

Mesh generate_plane(Arena *arena, AxisPlane orientation, float width, float height, uint32_t subdivision_x, uint32_t subdivision_z);
Mesh generate_plane_from_heightmap(Arena *arena, AxisPlane orientation, float w, float h, Image2D heightmap);
AnimationClip *load_gltf_animations(Arena *arena, String8 path, uint32_t *count);

void push_rect2(Arena *arena, Rectangle rect, Color color);
void push_line2(Arena *arena, float2 start, float2 end, float thickness, Color color);
void push_rect2_outline(Arena *arena, Rectangle rect, float thickness, Color color);
void push_triangle2(Arena *arena, Triangle2 triangle, float thickness, Color color);

void push_arc3(Arena *arena, float3 center, float radius, uint8_t segments, AxisPlane plane, float angle_span, float thickness, Color color);
void push_line3(Arena *arena, float3 start, float3 end, float thickness, Color color);
void push_aabb3_outline(Arena *arena, AABB3 aabb3, float thickness, Color color);
void push_triangle3(Arena *arena, Triangle3 t, float thickness, Color color);
void push_rect3_outline(Arena *arena, Plane plane, float width, float height, float thickness, Color color);

typedef struct {
	Arena *batch_arena;

	float2 mouse_position;
	bool mouse_pressed, mouse_released;

	uint64_t hovered_id, active_id;
} ImguiState;
static ImguiState imgui_state = { 0 };

void imgui_frame_begin(Arena *arena);
void imgui_frame_end(void);

typedef struct {
	bool pressed, clicked;

	bool held, hovering;
} ImguiInteraction;

ImguiInteraction imgui_interact(uint64_t id, Rectangle rect);
ImguiInteraction imgui_button(uint64_t id, float x, float y, float w, float h);
float imgui_slidervf(uint64_t id, Rectangle bounds, float min, float max, float t);
float imgui_sliderhf(uint64_t id, Rectangle bounds, float min, float max, float t);

uint32_t find_animation(AnimationClip *clips, uint32_t count, String8 target) {
	for (uint32_t anim_index = 0; anim_index < count; ++anim_index)
		if (str8_equals(str8_wrap(clips[anim_index].name), target))
			return anim_index;

	return 0;
}

Entity *entity_spawn(World *world) {
	Entity *result = 0;
	bool ok = world;

	if (ok) {
		result = &world->entities[0];

		ok = world->entity_count < MAX_ENTITIES;
	}

	if (ok) {
		result = &world->entities[world->entity_count++];

		result->transform.rotation = quat4_identity();
		result->transform.scale = FLOAT3_ONE;
	}

	if (ok == false) {
		ASSERT(false);
	}

	return result;
}

static inline void entity__bitset_flip(FeatureBitset bitset, EntityFeature feature, bool on) {
	uint32_t index = feature / ENTITY_BITSET_WIDTH;

	if (on)
		bitset[index] |= 1ULL << (feature % ENTITY_BITSET_WIDTH);
	else
		bitset[index] &= ~(1ULL << (feature % ENTITY_BITSET_WIDTH));
}
static inline bool entity__bitset_test(FeatureBitset bitset, EntityFeature feature) {
	uint32_t index = feature / ENTITY_BITSET_WIDTH;
	return bitset[index] & (1ULL << (feature % ENTITY_BITSET_WIDTH));
}

void entity_enable(Entity *entity, EntityFeature feature) {
	bool ok = entity && feature < ENTITY_FEATURE_MAX;
	if (ok)
		entity__bitset_flip(entity->features, feature, true);
}
void entity_disable(Entity *entity, EntityFeature feature) {
	bool ok = entity && (uint32_t)feature < ENTITY_FEATURE_MAX;
	if (ok)
		entity__bitset_flip(entity->features, feature, false);
}

static inline bool entity_has(Entity *entity, EntityFeature feature) {
	bool ok = entity && (uint32_t)feature < ENTITY_FEATURE_MAX;
	if (ok)
		ok = entity__bitset_test(entity->features, feature);

	return ok;
}

int main(void) {
	logger_set_level(LOG_LEVEL_DEBUG);

	os_display_startup();
	OS_Surface *main_render = os_surface_open(1280, 720, s("main_render"), OS_SURFACE_FLAG_RESIZEABLE);
	OS_Surface *popup_compute = os_surface_open(640, 360, s("popup_compute"), 0);

	uint64_t start_time = os_time_ns();

	InputState input_state = { 0 };
	input_set_context(&input_state);

	Arena arena[1] = { arena_make(MiB(256)) };

	GFX_Context context[] = { 0 };
	gfx_startup(context);
	GFX_Swapchain *swapchains[] = { gfx_swapchain_make(context, popup_compute, "popup"), gfx_swapchain_make(context, main_render, "main") };

	// :targets
	GFX_Image *compute_image = gfx_image_make(context, 640, 360, (ImageOptions){ .format = PIXELFORMAT_RGBA16_FLOAT, .usage = IMAGE_USAGE_STORAGE | IMAGE_USAGE_TRANSFER });
	GFX_Image *offscreen_render = gfx_image_make(context, 948, 1044, (ImageOptions){ .format = PIXELFORMAT_BACKBUFFER, .usage = IMAGE_USAGE_RENDER, .sample = SAMPLE_COUNT_8 });
	GFX_Image *depthbuffer = gfx_image_make(context, 948, 1044, (ImageOptions){ .format = PIXELFORMAT_DEPTH, .usage = IMAGE_USAGE_RENDER, .sample = SAMPLE_COUNT_8 });
	GFX_Image *shadow_depthbuffer = gfx_image_make(context, 2048, 2048, (ImageOptions){ .format = PIXELFORMAT_DEPTH, .usage = IMAGE_USAGE_RENDER | IMAGE_USAGE_SAMPLE });

	Image2D skybox = load_cubemap(arena,
		array_arg(
			String8,
			s("assets/textures/skybox_mc/dayRight.png"),
			s("assets/textures/skybox_mc/dayLeft.png"),
			s("assets/textures/skybox_mc/dayTop.png"),
			s("assets/textures/skybox_mc/dayBottom.png"),
			s("assets/textures/skybox_mc/dayFront.png"),
			s("assets/textures/skybox_mc/dayBack.png") //
			) //
	);
	skybox.gpu = gfx_image_make(context, skybox.width, skybox.height,
		(ImageOptions){
		  .format = PIXELFORMAT_RGBA8_SRGB,
		  .type = IMAGE_TYPE_CUBE,
		  .pixels = skybox.pixels,
		});

	Image2D terrain_texture = load_image(arena, s("assets/textures/base_grass.png"));
	Image2D grid_texture = load_image(arena, s("assets/textures/prototype/texture_09.png"));

	Image2D heightmap = load_image(arena, s("assets/textures/heightmap.png"));
	heightmap.gpu = gfx_image_make(context, heightmap.width, heightmap.height,
		(ImageOptions){
		  .format = PIXELFORMAT_RGBA8_UNORM,
		  .pixels = heightmap.pixels,
		});

	GFX_Sampler *linear_sampler[WRAP_MODE_COUNT] = {
		[WRAP_MODE_REPEAT] = gfx_sampler_make(context, sampler_opt(FILTER_LINEAR, WRAP_MODE_REPEAT)),
		[WRAP_MODE_CLAMP] = gfx_sampler_make(context, sampler_opt(FILTER_LINEAR, WRAP_MODE_CLAMP))
	};
	GFX_Sampler *nearest_sampler = gfx_sampler_make(context, sampler_opt(FILTER_NEAREST, WRAP_MODE_CLAMP));
	SamplerOptions shadow_opt = sampler_opt(FILTER_LINEAR, WRAP_MODE_CLAMP_BORDER);
	shadow_opt.debug_name = "shadow_sampler";
	/* shadow_opt.compare_enable = true; */
	GFX_Sampler *shadow_sampler = gfx_sampler_make(context, shadow_opt);
	GFX_Image *white_texture = gfx_image_make(context, 1, 1,
		(ImageOptions){
		  .format = PIXELFORMAT_RGBA8_SRGB,
		  .pixels = &(uint32_t){ 0xFFFFFFFF },
		});

	// create compute pipeline
	OS_Timestamp compute_ts = os_file_last_modified(s("assets/shaders/compute/bin/test.compute.spv"));
	GFX_Pipeline c_pipeline = { 0 };
	{
		ArenaTemp scratch = arena_scratch_begin(NULL);
		String8 compute_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/compute/bin/test.compute.spv"));
		c_pipeline = compute_pipeline_make(context, compute_bytecode);
		arena_scratch_end(scratch);
	}

	GFX_Pipeline pipeline_skinning = { 0 };
	{ // :skinning
		ArenaTemp scratch = arena_scratch_begin(NULL);
		String8 compute_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/compute/bin/skinning.compute.spv"));
		pipeline_skinning = compute_pipeline_make(context, compute_bytecode);
		arena_scratch_end(scratch);
	}

	GFX_Pipeline pipeline_shadow = { 0 };
	{
		ArenaTemp scratch = arena_scratch_begin(NULL);
		String8 vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/shadow.vertex.spv"));
		String8 fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/blank.fragment.spv"));

		pipeline_shadow = graphics_pipeline_make(context, vertex_bytecode, fragment_bytecode,
			(PipelineOptions){
			  .debug_name = "pipeline_shadow",
			});
		arena_scratch_end(scratch);
	}

	GFX_Pipeline pipeline_3d = { 0 };
	GFX_Pipeline pipeline_skybox = { 0 };
	GFX_Pipeline pipeline_grass = { 0 };
	GFX_Pipeline pipeline_line3d = { 0 };
	{ // create 3d graphics pipelines
		ArenaTemp scratch = arena_scratch_begin(NULL);
		String8 vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/batch3d.vertex.spv"));
		String8 fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/phong.fragment.spv"));

		pipeline_3d = graphics_pipeline_make(context, vertex_bytecode, fragment_bytecode,
			(PipelineOptions){
			  .debug_name = "spatial",
			  .color_attachments = { PIXELFORMAT_BACKBUFFER },
			  .color_attachment_count = 1,
			  .sample_count = SAMPLE_COUNT_8,
			  .cull_mode = CULL_MODE_BACK,
			});

		vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/grass.vertex.spv"));
		fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/grass.fragment.spv"));

		pipeline_grass = graphics_pipeline_make(context, vertex_bytecode, fragment_bytecode,
			(PipelineOptions){
			  .debug_name = "pipeline_grass",
			  .color_attachment_count = 1,
			  .color_attachments = { PIXELFORMAT_BACKBUFFER },
			  .sample_count = SAMPLE_COUNT_8,
			});

		vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/skybox.vertex.spv"));
		fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/skybox.fragment.spv"));
		pipeline_skybox = graphics_pipeline_make(context, vertex_bytecode, fragment_bytecode,
			(PipelineOptions){
			  .debug_name = "pipeline_skybox",
			  .color_attachment_count = 1,
			  .color_attachments = { PIXELFORMAT_BACKBUFFER },
			  .sample_count = SAMPLE_COUNT_8,
			});

		vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/line.vertex.spv"));
		fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/flat.fragment.spv"));
		pipeline_line3d = graphics_pipeline_make(context, vertex_bytecode, fragment_bytecode,
			(PipelineOptions){
			  .debug_name = "line3d",
			  .color_attachment_count = 1,
			  .color_attachments = { PIXELFORMAT_BACKBUFFER },
			  .sample_count = SAMPLE_COUNT_8,
			});

		arena_scratch_end(scratch);
	}

	GFX_Pipeline pipeline_2d = { 0 };
	GFX_Pipeline pipeline_line2d = { 0 };
	{
		ArenaTemp scratch = arena_scratch_begin(0);

		String8 vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/batch2d.vertex.spv"));
		String8 fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/textured.fragment.spv"));

		pipeline_2d = graphics_pipeline_make(context, vertex_bytecode, fragment_bytecode,
			(PipelineOptions){
			  .debug_name = "canvas",
			  .color_attachments = { PIXELFORMAT_BACKBUFFER },
			  .color_attachment_count = 1,
			  .sample_count = SAMPLE_COUNT_8,
			  .cull_mode = CULL_MODE_BACK,
			  .disable_depth_test = true,
			  .disable_depth_write = true,
			});

		vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/line2d.vertex.spv"));
		fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/flat.fragment.spv"));
		pipeline_line2d = graphics_pipeline_make(context, vertex_bytecode, fragment_bytecode,
			(PipelineOptions){
			  .debug_name = "line2d",
			  .color_attachments = { PIXELFORMAT_BACKBUFFER },
			  .color_attachment_count = 1,
			  .sample_count = SAMPLE_COUNT_8,
			  .cull_mode = CULL_MODE_BACK,
			  .disable_depth_test = true,
			  .disable_depth_write = true,
			});

		arena_scratch_end(scratch);
	}

	GFX_Buffer *geometry = gfx_buffer_make(context, MiB(256),
		(BufferOptions){
		  .debug_name = "geometry",
		  .memory = MEMORY_TYPE_GPU,
		  .usage = BUFFER_USAGE_STORAGE | BUFFER_USAGE_INDEX | BUFFER_USAGE_TRANSFER,
		});
	GFX_Buffer *grass_instancing_buffer = gfx_buffer_make(context, MiB(128),
		(BufferOptions){
		  .debug_name = "grass_instancing",
		  .memory = MEMORY_TYPE_GPU,
		  .usage = BUFFER_USAGE_STORAGE | BUFFER_USAGE_TRANSFER,
		});

	const uint32_t map_width = 32;
	const uint32_t map_depth = 32;

	Mesh meshes[MESH_COUNT] = { 0 };
	meshes[MESH_TERRAIN_FLAT] = generate_plane(arena, AXIS_PLANE_UP, map_width, map_depth, map_width, map_depth);
	meshes[MESH_TERRAIN_FLAT].materials[0].textures[TEXTURE_SLOT_ALBEDO] = terrain_texture;

	meshes[MESH_TERRAIN_HEIGHTMAP] = generate_plane_from_heightmap(arena, AXIS_PLANE_UP, 256.f, 256.f, heightmap);
	meshes[MESH_TERRAIN_HEIGHTMAP].materials[0].textures[TEXTURE_SLOT_ALBEDO] = terrain_texture;

	uint32_t animation_counts[MESH_COUNT] = { 0 };
	AnimationClip *animations[MESH_COUNT] = { 0 };
	(void)animations;

	for (uint32_t meshid = 0; meshid < MESH_COUNT; ++meshid) {
		if (meshid_to_metadata[meshid].length == 0)
			continue;
		meshes[meshid] = load_gltf(arena, meshid_to_metadata[meshid]);

		if (meshes[meshid].skeleton.bone_count == 0 || meshid == MESH_MAGE)
			continue;
		animations[meshid] = load_gltf_animations(arena, meshid_to_metadata[meshid], &animation_counts[meshid]);
	}

	{ // :upload
		GFX_CommandContext *cmd = gfx_transfer_cmd(context);

		gfx_cmd_buffer_barrier(cmd, RESOURCE_USAGE_UNDEFINED, RESOURCE_USAGE_TRANSFER_DST, 0, geometry->size, geometry);
		gfx_cmd_buffer_barrier(cmd, RESOURCE_USAGE_UNDEFINED, RESOURCE_USAGE_TRANSFER_DST, 0, grass_instancing_buffer->size, grass_instancing_buffer);

		if (cmd->handle) {
			for (uint32_t mesh_index = 0; mesh_index < countof(meshes); ++mesh_index) {
				Mesh *mesh = &meshes[mesh_index];

				for (uint32_t material_index = 0; material_index < mesh->material_count; ++material_index) {
					Material *material = &mesh->materials[material_index];

					for (uint32_t texture_slot = 0; texture_slot < TEXTURE_SLOT_COUNT; ++texture_slot) {
						Image2D *img = &material->textures[texture_slot];
						uint32_t mip_level = 0;

						if (img->pixels) {
							img->gpu = gfx_image_make(context, img->width, img->height, (ImageOptions){ .pixels = img->pixels, .format = img->format, .max_mip_level = mip_level });
						} else {
							img->width = img->height = 1;
							img->format = PIXELFORMAT_RGBA8_SRGB;
							img->gpu = white_texture;
						}
					}
				}
			}

			uint64_t grass_upload_offset = arena_mark(cmd->transient_arena);
			// vertex_count = 256 * 256 * 3 * 6 = 1.179.648 = 1
			for (uint32_t z = 0; z < map_depth; ++z) {
				for (uint32_t x = 0; x < map_width; ++x) {
					float3 pos = {
						.x = x - (map_width * 0.5f) + randf_range(0.0, 1.0),
						.y = 0.0f,
						.z = z - (map_depth * 0.5f) + randf_range(0.0, 1.0),
					};
					pos = float3_scale(pos, 1.f / 2.f);
					*arena_push_count(cmd->transient_arena, float4, 1) = float4_from_float3(pos, 1.0f);
				}
			}
			gfx_cmd_buffer_to_buffer(cmd, grass_instancing_buffer, cmd->transient_buffer, 0, grass_upload_offset, sizeof(float4) * map_width * map_depth);
			gfx_cmd_buffer_barrier(cmd, RESOURCE_USAGE_TRANSFER_DST, RESOURCE_USAGE_SHADER_READ, 0, sizeof(float4) * map_width * map_depth, grass_instancing_buffer);

			uint64_t transient_upload_start_offset = cmd->transient_arena->offset;
			uint64_t geometry_upload_cursor = 0;

			for (uint32_t mesh_index = 0; mesh_index < countof(meshes); ++mesh_index) {
				Mesh *mesh = &meshes[mesh_index];
				mesh->buffer = geometry;

				uint64_t total_vertex_buffer_size = alignup(mesh->total_vertex_count * sizeof(Vertex3), 256);
				uint64_t total_index_buffer_size = alignup(mesh->total_index_count * sizeof(uint32_t), 256);
				uint64_t total_skinning_buffer_size = alignup(mesh->total_vertex_count * sizeof(SkinningData), 256);

				// Vertices
				mesh->buffer_vertex_byte_offset = geometry_upload_cursor;
				memory_copy(arena_push(cmd->transient_arena, total_vertex_buffer_size, 1, false), mesh->vertices, mesh->total_vertex_count * sizeof(Vertex3));
				geometry_upload_cursor += total_vertex_buffer_size;

				// Indices
				mesh->buffer_index_byte_offset = geometry_upload_cursor;
				memory_copy(arena_push(cmd->transient_arena, total_index_buffer_size, 1, false), mesh->indices, mesh->total_index_count * sizeof(uint32_t));
				geometry_upload_cursor += total_index_buffer_size;

				// Skinning
				if (mesh->skeleton.bone_count) {
					mesh->buffer_skinning_data_byte_offset = geometry_upload_cursor;
					memory_copy(arena_push(cmd->transient_arena, total_skinning_buffer_size, 1, false), mesh->skinning, mesh->total_vertex_count * sizeof(SkinningData));
					geometry_upload_cursor += total_skinning_buffer_size;
				}
			}

			gfx_cmd_buffer_to_buffer(cmd, geometry, cmd->transient_buffer, 0, transient_upload_start_offset, geometry_upload_cursor);

			gfx_cmd_buffer_barrier(cmd, RESOURCE_USAGE_TRANSFER_DST, RESOURCE_USAGE_SHADER_READ, 0, geometry_upload_cursor, geometry);
			gfx_cmd_buffer_barrier(cmd, RESOURCE_USAGE_TRANSFER_DST, RESOURCE_USAGE_INDEX_BUFFER, 0, geometry_upload_cursor, geometry);
		}
	}

	bool is_open = true;

	float dt = 0.0f;
	float last_frame = 0.0f;

	Arena frame_arena[] = { arena_make(MiB(4)) };

	// :init
	typedef enum {
		VIEWPORT_STATE_EDITOR,
		VIEWPORT_STATE_GAME,

		VIEWPORT_STATE_COUNT,
	} ViewportState;

	ViewportState state = VIEWPORT_STATE_EDITOR;

	Camera3 cameras[VIEWPORT_STATE_COUNT] = {
		[VIEWPORT_STATE_EDITOR] = {
		  .projection = CAMERA_PROJECTION_PERSPECTIVE,
		  .position = { 0.0f, 1.5f, 20.f },
		  .target = { 0.0f, 1.5f, 0.0f },
		  .up = FLOAT3_Y,
		  .fovy = 45.f,
		},
		[VIEWPORT_STATE_GAME] = {
		  .projection = CAMERA_PROJECTION_PERSPECTIVE,
		  .position = { 0.0f, 1.5f, 20.f },
		  .target = { 0.0f, 1.5f, 0.0f },
		  .up = FLOAT3_Y,
		  .fovy = 45.f,
		},
	};

	World world = { .entity_count = 1 };
	bool draw_collision_shapes = true;

	Entity *player = entity_spawn(&world);
	player->meshid = MESH_HERO_MALE;
	/* player->shape = shape_capsule((float3){ 0.0f, 1.0f, 0.0f }, 2.0f, 0.34f); */
	player->transform.translation.z = -3;
	player->shape = shape_sphere((float3){ 0.0f, 1.0f, 0.0f }, 1.0f);
	/* player->shape.as.aabb3 = meshes[player->meshid].bounds; */

	entity_enable(player, ENTITY_FEATURE_DRAW_MESH);
	/* entity_enable(player, ENTITY_FEATURE_CAST_SHADOW); */
	entity_enable(player, ENTITY_FEATURE_PLAYER_CONTROLLED);
	entity_enable(player, ENTITY_FEATURE_COLLIDABLE);
	entity_enable(player, ENTITY_FEATURE_ANIMATE);

	Entity *barrel = entity_spawn(&world);
	barrel->meshid = MESH_BARREL;
	barrel->transform.translation = (float3){ 10.0f, 0.0f, 0.0f };
	barrel->interact_radius = 3.0f;
	barrel->shape = shape_from_aabb3(meshes[barrel->meshid].bounds);
	barrel->target = player;

	entity_enable(barrel, ENTITY_FEATURE_DRAW_MESH);
	entity_enable(barrel, ENTITY_FEATURE_CAST_SHADOW);
	entity_enable(barrel, ENTITY_FEATURE_INTERACTABLE);
	entity_enable(barrel, ENTITY_FEATURE_COLLIDABLE);
	/* entity_enable(barrel, ENTITY_FEATURE_FOLLOW_TARGET); */
	/* entity_enable(barrel, ENTITY_FEATURE_ANIMATE); */

	/* Entity *terrain = entity_spawn(&world); */
	/* terrain->meshid = MESH_TERRAIN_FLAT; */
	/* entity_enable(terrain, ENTITY_FEATURE_DRAW_MESH); */

	Entity *level = entity_spawn(&world);
	level->meshid = MESH_TEST_LEVEL;
	entity_enable(level, ENTITY_FEATURE_DRAW_MESH);

	Triangle3 test_triangle = { { 0.0f, 0.25f, 1.0f }, { 1.0f, 0.25f, -1.0f }, { -1.0f, 0.25f, -1.0f } };

	while (is_open) {
		double time = os_time_ns() * 1e-9 - start_time * 1e-9;
		dt = time - last_frame;
		last_frame = time;

		input_update();
		OS_Event event = { 0 };
		while (os_event_poll(&event)) {
			switch (event.type) {
				case OS_EVENT_TYPE_SURFACE_CLOSE:
					is_open = false;
					break;

				case OS_EVENT_TYPE_SURFACE_RESIZE: {
					uint2 dims = { event.as.resize.width, event.as.resize.height };
					if (event.surface == main_render) {
						gfx_swapchain_resize(context, swapchains[1], dims.x, dims.y);
						gfx_image_resize(context, depthbuffer, dims.x, dims.y);
						gfx_image_resize(context, offscreen_render, dims.x, dims.y);

					} else {
						gfx_swapchain_resize(context, swapchains[0], dims.x, dims.y);
						gfx_image_resize(context, compute_image, dims.x, dims.y);
					}
				} break;

				case OS_EVENT_TYPE_KEY_PRESS:
				case OS_EVENT_TYPE_KEY_RELEASE:
					input_feed_key(event.as.key.key_code, event.type == OS_EVENT_TYPE_KEY_PRESS);
					break;

				case OS_EVENT_TYPE_MOUSE_PRESS:
				case OS_EVENT_TYPE_MOUSE_RELEASE:
					if (event.surface == main_render)
						input_feed_mouse_button(event.as.mouse_button.button, event.type == OS_EVENT_TYPE_MOUSE_PRESS);
					break;

				case OS_EVENT_TYPE_MOUSE_MOVE:
					if (event.surface == main_render)
						input_feed_mouse_motion((double)event.as.mouse_move.x, (double)event.as.mouse_move.y);
					break;
				default:
					break;
			}
		}

		// :update
		Arena batch_2d[] = { {
		  .base = arena_push_count(frame_arena, Vertex2, 6 * 1024),
		  .capacity = sizeof(Vertex2) * 6 * 1024,
		} };
		Arena batch_line3d[] = { {
		  .base = arena_push_count(frame_arena, Line, 6 * 1024),
		  .capacity = sizeof(Line) * 6 * 1024,
		} };

		Arena batch_line2d[] = {
			{
			  .base = arena_push_count(frame_arena, Line, 6 * 1024),
			  .capacity = sizeof(Line) * 6 * 1024,
			}
		};

		static float ambient_strength = 0.2f;
		static float fog_density = 0.02f, fog_gradient = 5.0f;
		static bool use_heightmap = false, draw_grass = false;
		static uint32_t light_index = 0;

		typedef struct {
			float4 position;
			float4 color;
			float4x4 matrix;
		} Light;

		Light lights[] = {
			{ .position = { 0.0f, 20.0f, -30.0f, 1.0f }, (float4){ 1.0f, 1.0f, 1.0f, 1.0f }, float4x4_identity() }, // day
			{ .position = { 0.0f, 20.0f, -30.0f, 1.0f }, (float4){ 1.0f, 0.5f, 0.2f, 1.0f }, float4x4_identity() }, // sunset
			{ .position = { 0.0f, 20.0f, -30.0f, 1.0f }, (float4){ 0.05f, 0.15f, 0.6f, 1.0f }, float4x4_identity() }, // night
		};

		uint2 dims = os_surface_size(main_render);
		float2 mouse_delta = float2_from_double2(input_mouse_delta());
		mouse_delta.x /= dims.x;
		mouse_delta.y /= dims.y;

		if (input_key_pressed(KEY_CODE_TAB))
			state = (state + 1) % VIEWPORT_STATE_COUNT;

		Camera3 *camera = &cameras[state];
#if 0
		switch (state) {
			case VIEWPORT_STATE_EDITOR: {
				if (input_mouse_down(MOUSE_BUTTON_MIDDLE))
					os_cursor_capture(main_render, true);
				else
					os_cursor_capture(main_render, false);

				scene_camera_orbit(camera, mouse_delta);

				for (uint32_t index = 0; index < world.entity_count; ++index) {
					Entity *entity = &world.entities[index];
					if (entity_has(entity, ENTITY_FEATURE_ANIMATE)) {
						uint32_t bone_count = meshes[entity->meshid].skeleton.bone_count;
						entity->skin_matrices = arena_push_count(frame_arena, float4x4, bone_count);
						for (uint32_t bone_index = 0; bone_index < bone_count; ++bone_index) {
							entity->skin_matrices[bone_index] = float4x4_identity();
						}
					}
				}

				// :editor
				imgui_frame_begin(batch_2d);
				{
					static float slider_t = 0.0f;

					float pad = 8.0f;
					float panel_x = 10.f, panel_y = 10.0f;

					float element_h = 15.f;
					uint32_t element_count = 5;
					float panel_w = 200.f;

					/* slider_t = imgui_slidervf(__LINE__, rect(100, 100, 50, 300), min, max, slider_t); */

					float fence_h = (element_count - 1) * pad;
					float total_element_h = element_count * element_h;

					Rectangle panel = rect(panel_x, panel_y, panel_w, fence_h + total_element_h + pad * 2);

					push_rect2(batch_2d, panel, rgb(25, 25, 25));
					fog_density = imgui_sliderhf(__LINE__, rect(panel_x + pad, panel_y + pad, panel_w - 2 * pad, element_h), 0.001f, 0.05f, fog_density);
					panel_y += element_h + pad;
					fog_gradient = imgui_sliderhf(__LINE__, rect(panel_x + pad, panel_y + pad, panel_w - 2 * pad, element_h), 0.0f, 15.0f, fog_gradient);
					panel_y += element_h + pad;
					ambient_strength = imgui_sliderhf(__LINE__, rect(panel_x + pad, panel_y + pad, panel_w - 2 * pad, element_h), 0.0f, 1.0f, ambient_strength);
					panel_y += element_h + pad;

					float button_w = (panel_w - (2 * pad + pad * 3)) / 4.f;
					float button_x = panel_x + pad;
					if (imgui_button(__LINE__, button_x, panel_y + pad, button_w, element_h).clicked) {
						use_heightmap = !use_heightmap;
						for (uint32_t entity_index = 0; entity_index < world.entity_count; ++entity_index) {
							Entity *entity = &world.entities[entity_index];

							if (entity->meshid == MESH_TERRAIN_FLAT || entity->meshid == MESH_TERRAIN_HEIGHTMAP)
								entity->meshid = use_heightmap ? MESH_TERRAIN_HEIGHTMAP : MESH_TERRAIN_FLAT;
						}
					}
					button_x += button_w + pad;
					if (imgui_button(__LINE__, button_x, panel_y + pad, button_w, element_h).clicked)
						draw_grass = !draw_grass;
					button_x += button_w + pad;
					if (imgui_button(__LINE__, button_x, panel_y + pad, button_w, element_h).clicked) {
						light_index = (light_index + 1) % countof(lights);
					}
					button_x += button_w + pad;
					if (imgui_button(__LINE__, button_x, panel_y + pad, button_w, element_h).clicked) {
						draw_collision_shapes = !draw_collision_shapes;
					}

					imgui_frame_end();
				}

			} break;
			case VIEWPORT_STATE_GAME: {
				os_cursor_capture(main_render, input_key_pressed(KEY_CODE_E) ? !os_cursor_captured(main_render) : os_cursor_captured(main_render));

	#if 0
				for (uint32_t index = 0; index < world.entity_count; ++index) { // :heightmap
					Entity *entity = &world.entities[index];
					if (entity_has(entity, ENTITY_FEATURE_DRAW_MESH) == false)
						continue;

					if (use_heightmap) {
						float px = clampf(entity->transform.translation.x + map_width * 0.5f, 0.0f, map_width);
						float pz = clampf(entity->transform.translation.z + map_depth * 0.5f, 0.0f, map_depth);

						int32_t x0 = (int32_t)px;
						int32_t z0 = (int32_t)pz;

						int32_t x1 = MIN(x0 + 1, (int32_t)heightmap.width - 1);
						int32_t z1 = MIN(z0 + 1, (int32_t)heightmap.height - 1);

						float hs[4] = {
							[0] = (heightmap.pixels[(x0 + z0 * heightmap.width) * 4] / 255.f) - 0.5f,
							[1] = (heightmap.pixels[(x1 + z0 * heightmap.width) * 4] / 255.f) - 0.5f,
							[2] = (heightmap.pixels[(x0 + z1 * heightmap.width) * 4] / 255.f) - 0.5f,
							[3] = (heightmap.pixels[(x1 + z1 * heightmap.width) * 4] / 255.f) - 0.5f,
						};
						float u = px - (float)x0;
						float v = pz - (float)z0;

						float top_edge = hs[0] + u * (hs[1] - hs[0]);
						float bottom_edge = hs[2] + u * (hs[3] - hs[2]);

						float height = top_edge + v * (bottom_edge - top_edge);
						/* float hs = (heightmap.pixels[(x0 + z0 * heightmap.width) * 4] / 255.f) - 0.5f; */
						/* tranform->translation.y = hs * 40.f; */
						entity->transform.translation.y = height * 40.f;
					} else
						entity->transform.translation.y = 0.0f;
				}
	#endif

				for (uint32_t index = 0; index < world.entity_count; ++index) {
					Entity *entity = &world.entities[index];

					float2 input_vector = { 0 };
					float3 velocity = { 0 };
					float3 direction = { 0 };

					// :player
					if (entity_has(entity, ENTITY_FEATURE_PLAYER_CONTROLLED)) {
						input_vector = (float2){
							.x = input_key_down(KEY_CODE_W) - input_key_down(KEY_CODE_S),
							.y = input_key_down(KEY_CODE_D) - input_key_down(KEY_CODE_A),
						};

						if (float2_length_sq(input_vector) > 0) {
							float3 camera_offset = float3_subtract(camera->position, player->transform.translation);
							float r = float3_length(camera_offset);
							if (r < EPSILON)
								r = EPSILON;

							float current_theta = acosf(camera_offset.y / r);
							float current_azimuth = atan2f(camera_offset.z, camera_offset.x); // [-pi, pi]
																							  //
							float target_angle = -current_azimuth - C_PIf + atan2f(input_vector.x, input_vector.y);
							quat4 target_rotation = quat4_from_axis_angle(FLOAT3_Y, target_angle);

							float t = 1.0f - expf(-15.0f * dt);
							entity->transform.rotation = quat4_slerp(entity->transform.rotation, target_rotation, t);
						}

						const float walk_speed = 3.0f, run_speed = 6.0f;

						float3 camera_position = camera->position;
						float3 camera_target = camera->target;

						camera_position.y = 0.0f;
						camera_target.y = 0.0f;

						float3 forward, right;

						forward = float3_normalize(float3_subtract(camera_target, camera_position));

						right = float3_cross(forward, camera->up);
						right = float3_normalize(right);

						direction = float3_add(direction, float3_scale(forward, input_vector.x));
						direction = float3_add(direction, float3_scale(right, input_vector.y));

						float length = float3_length(direction);
						if (length > EPSILON)
							direction = float3_scale(direction, 1 / length);
						entity->move_speed = input_key_down(KEY_CODE_LEFTSHIFT) ? run_speed : walk_speed;

						velocity = float3_scale(direction, entity->move_speed * dt);
						/* velocity = float3_add(velocity, (float3){ 0.0f, -0.1f, 0.0f }); */
					}

					// :ai
					if (entity_has(entity, ENTITY_FEATURE_FOLLOW_TARGET)) {
						float3 delta = float3_subtract(entity->target->transform.translation, entity->transform.translation);
						float3 direction = float3_normalize_safe(delta, EPSILON);

						entity->transform.rotation = quat4_from_axis_angle(FLOAT3_Y, atan2f(direction.x, direction.z));

						entity->move_speed = 3.0f;

						input_vector = (float2){ direction.x, direction.z };
						velocity = float3_scale(direction, entity->move_speed * dt);
					}

					// :collision
					if (entity_has(entity, ENTITY_FEATURE_COLLIDABLE)) {
						for (uint32_t iteration = 0; iteration < 6; ++iteration) {
							if (float3_length_sq(velocity) <= EPSILON)
								break;
							Raycast3Result nearest = RAY3_NO_HIT;

							// Collision detection
							float3 ro = float3_add(entity->transform.translation, aabb3_center(entity->shape.as.aabb3));
							float3 rd = velocity;

							for (uint32_t other_index = 0; other_index < world.entity_count; ++other_index) {
								Entity *other = &world.entities[other_index];
								if (other == entity || entity_has(other, ENTITY_FEATURE_COLLIDABLE) == false)
									continue;

								float3 center = float3_add(aabb3_center(other->shape.as.aabb3), other->transform.translation);
								// NOTE: Minkowski sum
								float3 half_extent =
									float4x4_transform(
										float4x4_scaling(other->transform.scale),
										float4_from_float3(aabb3_half_extent(other->shape.as.aabb3), 0.0f));

								float3 swept_extent = float3_add(
									half_extent,
									aabb3_half_extent(entity->shape.as.aabb3) //
								);

								Raycast3Result result = raycast_aabb3(ro, rd, center, swept_extent);
								if (result.t < nearest.t)
									nearest = result;
							}

							if (entity->shape.kind == SHAPE_KIND_SPHERE) {
								Mesh *collision_mesh = &meshes[level->meshid];
								for (uint32_t part_index = 0; part_index < collision_mesh->part_count; ++part_index) {
									MeshPart *part = &collision_mesh->parts[part_index];
									for (uint32_t index = 0; index < part->index_count / 3; ++index) {
										Triangle3 t = {
											.a = collision_mesh->vertices[part->vertex_offset + collision_mesh->indices[part->index_offset + index * 3 + 0]].position,
											.b = collision_mesh->vertices[part->vertex_offset + collision_mesh->indices[part->index_offset + index * 3 + 1]].position,
											.c = collision_mesh->vertices[part->vertex_offset + collision_mesh->indices[part->index_offset + index * 3 + 2]].position,
										};

										float3 sphere_center = float3_add(entity->transform.translation, entity->shape.as.sphere.center);
										float radius = entity->shape.as.sphere.radius;
										float len = float3_length(velocity);
										float3 dir = float3_scale(velocity, 1 / len);
										Raycast3Result test_triangle_sweep = sphere_sweep_triangle3(sphere_center, radius, dir, len, t);
										if (test_triangle_sweep.hit && test_triangle_sweep.t < nearest.t)
											nearest = test_triangle_sweep;
									}
								}
							}

							if (nearest.hit == false) {
								entity->transform.translation = float3_add(entity->transform.translation, velocity);
								break;
							}

							// Collision resolution
							float t_min = maxf(minf(nearest.t, 1.0f) - 0.01f, 0.0f);

							entity->transform.translation = float3_add(entity->transform.translation, float3_scale(velocity, t_min));

							velocity = float3_subtract(velocity, float3_scale(nearest.normal, float3_dot(velocity, nearest.normal)));
							velocity = float3_scale(velocity, 1.0f - t_min);
							push_line3(batch_line, nearest.point, float3_add(nearest.point, float3_scale(nearest.normal, 1.0f)), 3.0f, RED);
						}
					}

					if (
						entity_has(entity, ENTITY_FEATURE_ANIMATE) &&
						entity_has(entity, ENTITY_FEATURE_DRAW_MESH) &&
						animation_counts[entity->meshid] // :skeletal
					) {
						Pose final = anim_pose_sample(
							frame_arena,
							&animations[entity->meshid][entity->current_anim],
							fmodf(entity->anim_t, animations[entity->meshid][entity->current_anim].duration) //
						);

						uint32_t target_anim = entity->current_anim;
						if (float2_length_sq(input_vector)) {
							input_vector = float2_normalize(input_vector);

							if (input_key_down(KEY_CODE_LEFTSHIFT))
								target_anim = find_animation(animations[entity->meshid], animation_counts[entity->meshid], s("freehand/run-loop"));
							else
								target_anim = find_animation(animations[entity->meshid], animation_counts[entity->meshid], s("freehand/walk-loop"));
						} else
							target_anim = find_animation(animations[entity->meshid], animation_counts[entity->meshid], s("freehand/idle-loop"));

						if (entity->current_anim != target_anim) {
							Pose start = final;
							Pose end = anim_pose_sample(frame_arena, &animations[entity->meshid][target_anim], fmodf(entity->anim_t, animations[entity->meshid][target_anim].duration));

							final = anim_pose_blend_local(frame_arena, &end, &start, entity->blend_t, 0);

							entity->blend_t += dt * 5;
							if (entity->blend_t >= 1.0f) {
								entity->current_anim = target_anim;
								entity->blend_t = 0.0f;
							}
						}

						Mesh *mesh = &meshes[entity->meshid];
						entity->skin_matrices = anim_pose_skinning_matrices(frame_arena, anim_pose_local_to_model(frame_arena, &final, &mesh->skeleton), &mesh->skeleton);

						entity->anim_t += dt;
					}
				}
				push_triangle3(batch_line, test_triangle, 3.0f, WHITE);

				{ // :camera
					static float azimuth = C_PIf * 3 / 2.f;
					static float theta = C_PIf / 3.f;
					static const float sensitivity = 1.0f;
					static const float spring_arm_length = 10.f;

					float yaw_delta = mouse_delta.x * sensitivity;
					float pitch_delta = mouse_delta.y * sensitivity;

					azimuth = fmodf(azimuth + yaw_delta, C_PIf * 2.f);
					if (azimuth < 0)
						azimuth += C_PIf * 2.f;

					theta = clampf(theta - pitch_delta, C_PIf / 4.f, C_PIf / 2.f);

					float3 camera_offset = float3_subtract(camera->position, player->transform.translation);

					float r = float3_length(camera_offset);
					if (r < EPSILON)
						r = EPSILON;

					float current_theta = acosf(camera_offset.y / r);
					float current_azimuth = atan2f(camera_offset.z, camera_offset.x); // [-pi, pi]

					if (current_azimuth < 0)
						current_azimuth += C_PIf * 2.f;

					float da = azimuth - current_azimuth;
					if (da > C_PI)
						da -= C_PI * 2.f;
					if (da < -C_PI)
						da += C_PI * 2.f;

					float t = 1.0f - expf(-15.0f * dt);

					current_azimuth += t * da;
					current_theta += t * (theta - current_theta);

					camera->position = (float3){
						(spring_arm_length * sinf(current_theta) * cosf(current_azimuth)) + player->transform.translation.x,
						spring_arm_length * cosf(current_theta),
						(spring_arm_length * sinf(current_theta) * sinf(current_azimuth)) + player->transform.translation.z,
					};

					camera->target.x = player->transform.translation.x;
					camera->target.z = player->transform.translation.z;

					camera->position.y += player->transform.translation.y;
					camera->target.y = player->transform.translation.y + 1.5f;
				}

				{ // :interact

					Entity *target = 0;
					float closest = FLOAT_MAX;

					if (input_key_pressed(KEY_CODE_F)) {
						for (uint32_t entity_index = 0; entity_index < world.entity_count; ++entity_index) {
							Entity *entity = &world.entities[entity_index];
							if (entity_has(entity, ENTITY_FEATURE_INTERACTABLE) == false || entity == player)
								continue;

							float3 offset = float3_subtract(entity->transform.translation, player->transform.translation);
							float dist_sq = float3_dot(offset, offset);

							if (dist_sq <= (entity->interact_radius * entity->interact_radius) && dist_sq < closest) {
								closest = dist_sq;
								target = entity;
							}
						}

						if (target)
							LOG_INFO("interaction with entity %u (%s)!", indexof(world.entities, target), str8_filename(meshid_to_metadata[target->meshid]).text);
					}
				}

			} break;
			default:
				break;
		}
#else
		Rectangle viewport = rect(0, 0, dims.x, dims.y);
		float2 center = { viewport.width / 2.0f, viewport.height / 2.0f };
		push_rect2(batch_2d, viewport, WHITE);

		float2 t_center = { center.x + 50.f, center.y - 100.f };
		float t_size = 80.f;
		float2 triangle_polygon[] = {
			float2_add(t_center, (float2){ 0.0f, -t_size }),
			float2_add(t_center, (float2){ t_size, t_size }),
			float2_add(t_center, (float2){ -t_size, t_size }),
		};

		float2 q_center = float2_subtract(center, float2_splat(100.f));
		float2 q_extent = float2_splat(100.f);
		float2 quad_polygon[] = {
			{ q_center.x - q_extent.x, q_center.y - q_extent.y },
			{ q_center.x + q_extent.x, q_center.y - q_extent.y },
			{ q_center.x - q_extent.x, q_center.y + q_extent.y },
			{ q_center.x + q_extent.x, q_center.y + q_extent.y },
		};

		push_triangle2(batch_line2d, (Triangle2){ triangle_polygon[0], triangle_polygon[1], triangle_polygon[2] }, 3.0f, BLACK);

		float2 rc = quad_polygon[0];
		float2 re = float2_subtract(quad_polygon[3], quad_polygon[0]);
		push_rect2_outline(batch_2d, rect(rc.x, rc.y, re.x, re.y), 3.0f, BLACK);

		float2 *sum_points = arena_push_count(frame_arena, float2, countof(triangle_polygon) * countof(quad_polygon));
		uint32_t point_count = 0;

		for (uint32_t angle = 0; angle < 32; ++angle) {
			float2 direction = { cosf((angle / 32.0f) * TAU), sinf((angle / 32.0f) * TAU) };

			float2 tp = { 0 };
			float triangle_max_distance = -FLOAT_MAX;
			for (uint32_t index = 0; index < countof(triangle_polygon); ++index) {
				float distance = float2_dot(triangle_polygon[index], direction);
				if (distance > triangle_max_distance) {
					triangle_max_distance = distance;
					tp = triangle_polygon[index];
				}
			}

			float2 qp = { 0 };
			float quad_max_distance = -FLOAT_MAX;
			direction = float2_scale(direction, -1.0f);
			for (uint32_t index = 0; index < countof(quad_polygon); ++index) {
				float distance = float2_dot(quad_polygon[index], direction);
				if (distance > quad_max_distance) {
					quad_max_distance = distance;
					qp = quad_polygon[index];
				}
			}

			float2 sum_point = float2_add(center, float2_subtract(qp, tp));
			bool found = false;
			for (uint32_t index = 0; index < point_count; ++index) {
				if (float2_equal(sum_point, sum_points[index])) {
					found = true;
					break;
				}
			}

			if (found == false) {
				sum_points[point_count++] = sum_point;
			}
		}

		for (uint32_t index = 0; index < point_count; ++index) {
			push_line2(batch_line2d, sum_points[index], sum_points[(index + 1) % point_count], 3.0f, BLACK);
		}

#endif

		// Frame resources
		GFX_CommandContext *cmd = gfx_frame_begin(context);
		if (cmd == 0)
			continue;

		// Swapchain image acquisition
		GFX_Image *compute_blit_target = gfx_backbuffer(context, cmd, swapchains[0]);
		if (compute_blit_target) {
			// transition swapchain target & blit src compute image
			gfx_cmd_image_transition(cmd, RESOURCE_USAGE_TRANSFER_DST, compute_blit_target);
			gfx_cmd_image_transition(cmd, RESOURCE_USAGE_COMPUTE_SHADER_WRITE, compute_image);

			gfx_cmd_pipeline_bind(cmd, &c_pipeline);
			gfx_bind(context, &c_pipeline, 0, array_arg(Uniform, storage_images(0, (GFX_Image *[]){ compute_image }, 1)));

			// Dispatch compute & Blit to main window surface
			gfx_cmd_dispatch(cmd, (compute_blit_target->width / 16) + 1, (compute_blit_target->height / 16) + 1, 1);
			gfx_cmd_image_transition(cmd, RESOURCE_USAGE_TRANSFER_SRC, compute_image);
			Rectangle area = rect(0, 0, compute_blit_target->width, compute_blit_target->height);
			gfx_cmd_image_blit(cmd, area, compute_image, area, compute_blit_target);

			// transition swapchain images for presenting
			gfx_cmd_image_transition(cmd, RESOURCE_USAGE_PRESENT, compute_blit_target);
		}

		GFX_Image *main_target = gfx_backbuffer(context, cmd, swapchains[1]);
		if (main_target) {
			// transition swapchain & offscren targets for drawing
			gfx_cmd_image_transition(cmd, RESOURCE_USAGE_COLOR_ATTACHMENT, main_target);
			gfx_cmd_image_transition(cmd, RESOURCE_USAGE_COLOR_ATTACHMENT, offscreen_render);
			static float blink_timer = 0.0f;

			blink_timer += dt;

			// :skinning
			for (uint32_t instance_index = 0; instance_index < world.entity_count; ++instance_index) {
				Entity *instance = &world.entities[instance_index];
				if (entity_has(instance, ENTITY_FEATURE_ANIMATE) == false || animation_counts[instance->meshid] == 0 || instance->skin_matrices == 0)
					continue;

				Mesh *mesh = &meshes[instance->meshid];

				uint64_t matrices_size = mesh->skeleton.bone_count * sizeof(float4x4);
				uint64_t matrices_offset = gfx_cmd_put(cmd, matrices_size, instance->skin_matrices);

				uint64_t skinned_vertices_size = alignup(mesh->total_vertex_count * sizeof(Vertex3), 256);
				instance->skinned_vertices_offset = gfx_cmd_put(cmd, skinned_vertices_size, 0);

				gfx_cmd_pipeline_bind(cmd, &pipeline_skinning);
				struct {
					uint32_t vertex_count;
					uint32_t _pad0;
					uint64_t skinning_matrices_address;
					uint64_t input_address;
					uint64_t skinning_address;
					uint64_t output_address;
				} pc = {
					.vertex_count = mesh->total_vertex_count,
					.skinning_matrices_address = cmd->transient_buffer->address + matrices_offset,
					.input_address = mesh->buffer->address + mesh->buffer_vertex_byte_offset,
					.skinning_address = mesh->buffer->address + mesh->buffer_skinning_data_byte_offset,
					.output_address = cmd->transient_buffer->address + instance->skinned_vertices_offset,
				};
				vkCmdPushConstants(cmd->handle, pipeline_skinning.layout, VK_SHADER_STAGE_ALL, 0, sizeof(pc), &pc);

				gfx_cmd_dispatch(cmd, (mesh->total_vertex_count + 255) / 256, 1, 1);
				gfx_cmd_buffer_barrier(
					cmd,
					RESOURCE_USAGE_COMPUTE_SHADER_WRITE,
					RESOURCE_USAGE_VERTEX_SHADER_READ,
					instance->skinned_vertices_offset,
					skinned_vertices_size, cmd->transient_buffer);
			}

			typedef struct {
				float4x4 view;
				float4x4 proj;
				float4 camera_position;
				float2 viewport;
				float fog_density;
				float ambient_strength;
				float fog_gradient;
				float time;
			} Frame3D;

			float ortho_size = 10.0f;
			lights[light_index].matrix = float4x4_multiply(
				float4x4_orthographic(-ortho_size, ortho_size, -ortho_size, ortho_size, 0.1f, 100.f),
				float4x4_lookat(float3_from_float4(lights[light_index].position), FLOAT3_ZERO, FLOAT3_Y));

			Frame3D frame_data = {
				.viewport = { dims.x, dims.y },
				.ambient_strength = ambient_strength,
				.fog_density = fog_density,
				.fog_gradient = fog_gradient,
				.time = time,
			};

			{ // :shadow
				gfx_cmd_image_transition(cmd, RESOURCE_USAGE_DEPTH_ATTACHMENT, shadow_depthbuffer);
				VkRenderingAttachmentInfo depth_attachment = {
					.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
					.imageView = shadow_depthbuffer->view,
					.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
					.clearValue.depthStencil.depth = 1.0f,
					.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
					.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				};

				VkExtent2D extent = {
					.width = shadow_depthbuffer->width,
					.height = shadow_depthbuffer->height,
				};
				VkRenderingInfo shadowpass_info = {
					.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
					.renderArea.extent = extent,
					.layerCount = 1,
					.colorAttachmentCount = 0,
					.pDepthAttachment = &depth_attachment,
				};
				vkCmdBeginRendering(cmd->handle, &shadowpass_info);

				VkViewport viewport = {
					.width = extent.width,
					.height = extent.height,
					.minDepth = 0.0f,
					.maxDepth = 1.0f,
				};
				vkCmdSetViewport(cmd->handle, 0, 1, &viewport);
				vkCmdSetScissor(cmd->handle, 0, 1, &(VkRect2D){ .extent = extent });

				frame_data.view = lights[light_index].matrix;
				frame_data.proj = float4x4_identity();
				frame_data.camera_position = lights[light_index].position;
				frame_data.proj.elements[5] *= -1;

				gfx_cmd_pipeline_bind(cmd, &pipeline_shadow);
				gfx_bind(context, &pipeline_shadow, 0,
					array_arg(Uniform, uniform_data(0, &frame_data, sizeof(frame_data))) //
				);

				// :shadow
				for (uint32_t instance_index = 0; instance_index < world.entity_count; ++instance_index) {
					Entity *entity = &world.entities[instance_index];
					if (entity_has(entity, ENTITY_FEATURE_CAST_SHADOW) == false)
						continue;

					Mesh *mesh = &meshes[entity->meshid];

					vkCmdBindIndexBuffer(cmd->handle, geometry->handle, mesh->buffer_index_byte_offset, VK_INDEX_TYPE_UINT32);
					for (uint32_t part_index = 0; part_index < mesh->part_count; ++part_index) {
						MeshPart *part = &mesh->parts[part_index];
						Material *material = &mesh->materials[part->material_id];

						float4x4 transform = float4x4_compose_quat(
							entity->transform.translation,
							entity->transform.rotation,
							entity->transform.scale //
						);
						struct {
							float4x4 model;
						} pc = { .model = transform };

						GFX_Buffer *buffer = mesh->buffer;
						uint64_t offset = mesh->buffer_vertex_byte_offset;
						uint64_t size = mesh->total_vertex_count * sizeof(Vertex3);
						if (entity_has(entity, ENTITY_FEATURE_ANIMATE) && animation_counts[entity->meshid] && entity->skin_matrices) {
							buffer = cmd->transient_buffer;
							offset = entity->skinned_vertices_offset;
						}

						gfx_bind(context, &pipeline_shadow, 1,
							array_arg(Uniform, storage_buffers(0, buffer, offset, size)) //
						);

						vkCmdPushConstants(cmd->handle, pipeline_shadow.layout, VK_SHADER_STAGE_ALL, 0, sizeof(pc), &pc);
						vkCmdDrawIndexed(cmd->handle, part->index_count, 1, part->index_offset, part->vertex_offset, 0);
					}
				}

				vkCmdEndRendering(cmd->handle);
			}

			{ // :main
				gfx_cmd_image_transition(cmd, RESOURCE_USAGE_SHADER_READ, shadow_depthbuffer);

				VkRenderingAttachmentInfo color_attachments[] = {
					{
					  .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
					  .imageView = offscreen_render->view,
					  .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					  .resolveImageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
					  .resolveImageView = main_target->view,
					  .resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT,
					  .clearValue.color = { { 1.00f, 1.00f, 0.00f, 1.0f } },
					  .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
					  .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
					}
				};

				gfx_cmd_image_transition(cmd, RESOURCE_USAGE_DEPTH_ATTACHMENT, depthbuffer);
				VkRenderingAttachmentInfo depth_attachment = {
					.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
					.imageView = depthbuffer->view,
					.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
					.clearValue.depthStencil.depth = 1.0f,
					.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
					.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				};

				VkExtent2D extent = {
					.width = main_target->width,
					.height = main_target->height,
				};

				VkRenderingInfo renderpass_info = {
					.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
					.renderArea.extent = extent,
					.layerCount = 1,
					.colorAttachmentCount = countof(color_attachments),
					.pColorAttachments = color_attachments,
					.pDepthAttachment = &depth_attachment,
				};
				vkCmdBeginRendering(cmd->handle, &renderpass_info);

				VkViewport viewport = {
					.width = extent.width,
					.height = extent.height,
					.minDepth = 0.0f,
					.maxDepth = 1.0f,
				};
				vkCmdSetViewport(cmd->handle, 0, 1, &viewport);
				vkCmdSetScissor(cmd->handle, 0, 1, &(VkRect2D){ .extent = extent });

				frame_data.view = float4x4_lookat(camera->position, camera->target, camera->up);
				frame_data.proj = float4x4_perspective(deg_to_rad(45.f), (float)dims.x / (float)dims.y, 0.1f, 500.f);
				frame_data.camera_position = float4_from_float3(camera->position, 0.0f);

				gfx_cmd_pipeline_bind(cmd, &pipeline_3d);

				Uniform uniforms[] = {
					uniform_data(0, &frame_data, sizeof(frame_data)),
					storage_data(1, lights + light_index, sizeof(lights[0])),
					sampler_with_textures(2, (GFX_Image *[]){ shadow_depthbuffer }, 1, shadow_sampler),
					sampler_with_textures(3, (GFX_Image *[]){ skybox.gpu }, 1, linear_sampler[WRAP_MODE_CLAMP]),
				};
				gfx_bind(context, &pipeline_3d, 0, uniforms, countof(uniforms));

				// :draw
				for (uint32_t instance_index = 0; instance_index < world.entity_count; ++instance_index) {
					Entity *entity = &world.entities[instance_index];
					if (entity_has(entity, ENTITY_FEATURE_DRAW_MESH) == false)
						continue;

					Mesh *mesh = &meshes[entity->meshid];

					vkCmdBindIndexBuffer(cmd->handle, geometry->handle, mesh->buffer_index_byte_offset, VK_INDEX_TYPE_UINT32);
					for (uint32_t part_index = 0; part_index < mesh->part_count; ++part_index) {
						MeshPart *part = &mesh->parts[part_index];
						Material *material = &mesh->materials[part->material_id];

						float4x4 transform = float4x4_compose_quat(
							entity->transform.translation,
							entity->transform.rotation,
							entity->transform.scale //
						);
						struct {
							float4x4 model;
							float4 base_color;
							float4 emissive;
							float2 metallic_roughness;
							float2 uv_offset;
							float2 uv_scale;
						} pc = {
							.model = transform,
							.base_color = material->tint,
							.emissive = FLOAT4_ONE,
							.metallic_roughness = { 0.0f, 0.5f },
							.uv_offset = { 0.0f, 0.0f },
							.uv_scale = { 1.0f, 1.0f },
						};

						if (entity->meshid == MESH_TERRAIN_FLAT) {
							pc.uv_scale.x *= 8.f;
							pc.uv_scale.y *= 8.f;
						}

						if (entity->meshid == MESH_HERO_MALE && part_index == 3) { // hero head
							if (blink_timer > 2.0f && blink_timer < 2.1f)
								pc.uv_offset.x = 1.0f / 3.0f;
							else if (blink_timer > 2.1f && blink_timer < 2.3f) {
								pc.uv_offset.x = 2.0f / 3.0f;
							} else if (blink_timer >= 2.3f) {
								blink_timer = 0.0f;
							}
						}

						GFX_Buffer *buffer = mesh->buffer;
						uint64_t offset = mesh->buffer_vertex_byte_offset;
						uint64_t size = mesh->total_vertex_count * sizeof(Vertex3);
						if (entity_has(entity, ENTITY_FEATURE_ANIMATE) && animation_counts[entity->meshid] && entity->skin_matrices) {
							buffer = cmd->transient_buffer;
							offset = entity->skinned_vertices_offset;
						}

						GFX_Image *images[] = {
							[TEXTURE_SLOT_ALBEDO] = material->textures[TEXTURE_SLOT_ALBEDO].gpu,
							[TEXTURE_SLOT_METAL_ROUGHNESS] = material->textures[TEXTURE_SLOT_METAL_ROUGHNESS].gpu,
							[TEXTURE_SLOT_NORMAL] = material->textures[TEXTURE_SLOT_NORMAL].gpu,
							[TEXTURE_SLOT_OCCLUSION] = material->textures[TEXTURE_SLOT_OCCLUSION].gpu,
							[TEXTURE_SLOT_EMISSIVE] = material->textures[TEXTURE_SLOT_EMISSIVE].gpu,
						};

						Uniform uniforms[] = {
							storage_buffers(0, buffer, offset, size),
							sampler_with_textures(1, images, countof(images), linear_sampler[WRAP_MODE_REPEAT])
						};
						gfx_bind(context, &pipeline_3d, 1, uniforms, countof(uniforms));

						vkCmdPushConstants(cmd->handle, pipeline_3d.layout, VK_SHADER_STAGE_ALL, 0, sizeof(pc), &pc);
						vkCmdDrawIndexed(cmd->handle, part->index_count, 1, part->index_offset, part->vertex_offset, 0);
					}
				}

				// :grass
				if (draw_grass) {
					Mesh *mesh = &meshes[MESH_GRASS_BILLBOARD];

					gfx_cmd_pipeline_bind(cmd, &pipeline_grass);
					vkCmdBindIndexBuffer(cmd->handle, geometry->handle, mesh->buffer_index_byte_offset, VK_INDEX_TYPE_UINT32);

					for (uint32_t part_index = 0; part_index < mesh->part_count; ++part_index) {
						MeshPart *part = &mesh->parts[part_index];
						Material *material = &mesh->materials[part->material_id];

						struct {
							float4x4 model;
							float4 base_color;
							float4 emissive;
							float2 metallic_roughness;
							float2 uv_offset;
							float2 uv_scale;
							uint32_t use_heightmap;
						} pc = {
							.model = float4x4_identity(),
							.base_color = material->tint,
							.emissive = FLOAT4_ONE,
							.metallic_roughness = { 0.0f, 0.5f },
							.uv_offset = { 0.0f, 0.0f },
							.uv_scale = { 1.0f, 1.0f },
							.use_heightmap = use_heightmap,
						};

						GFX_Buffer *buffer = mesh->buffer;
						uint64_t offset = mesh->buffer_vertex_byte_offset;
						uint64_t size = mesh->total_vertex_count * sizeof(Vertex3);
						GFX_Image *images[] = {
							[TEXTURE_SLOT_ALBEDO] = material->textures[TEXTURE_SLOT_ALBEDO].gpu,
							[TEXTURE_SLOT_METAL_ROUGHNESS] = material->textures[TEXTURE_SLOT_METAL_ROUGHNESS].gpu,
							[TEXTURE_SLOT_NORMAL] = material->textures[TEXTURE_SLOT_NORMAL].gpu,
							[TEXTURE_SLOT_OCCLUSION] = material->textures[TEXTURE_SLOT_OCCLUSION].gpu,
							[TEXTURE_SLOT_EMISSIVE] = material->textures[TEXTURE_SLOT_EMISSIVE].gpu,
						};

						Uniform uniforms[] = {
							storage_buffers(0, buffer, offset, size),
							sampler_with_textures(1, images, countof(images), nearest_sampler),
							storage_buffers(2, grass_instancing_buffer, 0, grass_instancing_buffer->size),
							sampler_with_textures(3, (GFX_Image *[]){ heightmap.gpu }, 1, linear_sampler[WRAP_MODE_CLAMP]),
						};
						gfx_bind(context, &pipeline_grass, 1, uniforms, countof(uniforms));

						vkCmdPushConstants(cmd->handle, pipeline_grass.layout, VK_SHADER_STAGE_ALL, 0, sizeof(pc), &pc);
						vkCmdDrawIndexed(cmd->handle, part->index_count, map_width * map_depth, part->index_offset, part->vertex_offset, 0);
					}
				}

				{ // :skybox
					gfx_cmd_pipeline_bind(cmd, &pipeline_skybox);
					vkCmdDraw(cmd->handle, 36, 1, 0, 0);
				}

				{ // :overlay
					if (draw_collision_shapes) {
						for (uint32_t instance_index = 0; instance_index < world.entity_count; ++instance_index) {
							Entity *entity = &world.entities[instance_index];
							if (entity_has(entity, ENTITY_FEATURE_COLLIDABLE) == false)
								continue;

							Shape *shape = &entity->shape;
							float thickness = 3.0f;

							switch (shape->kind) {
								case SHAPE_KIND_AABB3: {
									AABB3 a = aabb3_move(shape->as.aabb3, entity->transform.translation);
									push_aabb3_outline(batch_line3d, a, thickness, WHITE);
								} break;
								case SHAPE_KIND_SPHERE: {
									float3 c = float3_add(shape->as.sphere.center, entity->transform.translation);
									float r = shape->as.sphere.radius;
									uint8_t segments = 32;

									push_arc3(batch_line3d, c, r, segments, AXIS_PLANE_UP, TAU, thickness, WHITE);
									push_arc3(batch_line3d, c, r, segments, AXIS_PLANE_RIGHT, TAU, thickness, WHITE);
									push_arc3(batch_line3d, c, r, segments, AXIS_PLANE_FORWARD, TAU, thickness, WHITE);
								} break;
								case SHAPE_KIND_CAPSULE: {
									float r = shape->as.capsule.radius;
									float3 centers[] = {
										float3_add(shape->as.capsule.a, entity->transform.translation),
										float3_add(shape->as.capsule.b, entity->transform.translation),
									};

									uint8_t segments = 32;
									Line *spine_points = arena_push_count(batch_line3d, Line, 8);
									// clang-format off
									Line spine[] = {
										{ {centers[0].x - r, centers[0].y, centers[0].z}, thickness, color_pack(WHITE), FLOAT3_ZERO}, { {centers[0].x - r, centers[1].y, centers[0].z}, thickness, color_pack(WHITE), FLOAT3_ZERO},
										{ {centers[0].x + r, centers[0].y, centers[0].z}, thickness, color_pack(WHITE), FLOAT3_ZERO}, { {centers[0].x + r, centers[1].y, centers[0].z}, thickness, color_pack(WHITE), FLOAT3_ZERO},
										{ {centers[0].x, centers[0].y, centers[0].z - r}, thickness, color_pack(WHITE), FLOAT3_ZERO}, { {centers[0].x, centers[1].y, centers[0].z - r}, thickness, color_pack(WHITE), FLOAT3_ZERO},
										{ {centers[0].x, centers[0].y, centers[0].z + r}, thickness, color_pack(WHITE), FLOAT3_ZERO}, { {centers[0].x, centers[1].y, centers[0].z + r}, thickness, color_pack(WHITE), FLOAT3_ZERO},
									};
									// clang-format on
									memory_copy_array(spine_points, spine);

									for (uint32_t end = 0; end < countof(centers); ++end) {
										float3 c = centers[end];
										float signed_r = (end == 0) ? -r : r;

										push_arc3(batch_line3d, centers[end], r, segments, AXIS_PLANE_UP, TAU, thickness, WHITE);
										push_arc3(batch_line3d, centers[end], signed_r, segments, AXIS_PLANE_RIGHT, C_PIf, thickness, WHITE);
										push_arc3(batch_line3d, centers[end], signed_r, segments, AXIS_PLANE_FORWARD, C_PIf, thickness, WHITE);
									}

								} break;
									break;
								case SHAPE_KIND_PLANE:
									break;
							}
						}
					}

					if (batch_line3d->offset) {
						gfx_cmd_pipeline_bind(cmd, &pipeline_line3d);
						Uniform uniforms[] = {
							storage_data(0, batch_line3d->base, batch_line3d->offset),
						};
						gfx_bind(context, &pipeline_line3d, 1, uniforms, countof(uniforms));
						vkCmdDraw(cmd->handle, (batch_line3d->offset / sizeof(Line)) * 6, 1, 0, 0);
					}
				}

				typedef struct {
					float4x4 view;
					float4x4 projection;
					float2 camera_position;
					float2 viewport;
					float time;
				} Frame2D;

				Frame2D frame_2d = {
					.view = float4x4_identity(),
					.projection = float4x4_orthographic(0.0f, dims.x, 0.0f, dims.y, -50.f, 50.f),
					.viewport = float2_from_uint2(dims),
					.time = time,
				};
				if (batch_2d->offset) { // :canvas

					gfx_cmd_pipeline_bind(cmd, &pipeline_2d);

					GFX_Image *images[32] = { 0 };
					for (uint32_t texture_id = 0; texture_id < 32; ++texture_id)
						images[texture_id] = white_texture;

					Uniform uniforms0[] = {
						uniform_data(0, &frame_2d, sizeof(frame_2d)),
						storage_data(1, batch_2d->base, batch_2d->offset),
					};
					Uniform uniforms1[] = { sampler_with_textures(0, images, countof(images), linear_sampler[WRAP_MODE_CLAMP]) };

					gfx_bind(context, &pipeline_2d, 0, uniforms0, countof(uniforms0));
					gfx_bind(context, &pipeline_2d, 1, uniforms1, countof(uniforms1));

					vkCmdDraw(cmd->handle, batch_2d->offset / sizeof(Vertex2), 1, 0, 0);
				}

				if (batch_line2d->offset) {
					gfx_cmd_pipeline_bind(cmd, &pipeline_line2d);
					Uniform uniforms[] = {
						storage_data(0, batch_line2d->base, batch_line2d->offset),
					};
					gfx_bind(context, &pipeline_line2d, 0, array_arg(Uniform, uniform_data(0, &frame_2d, sizeof(frame_2d))));
					gfx_bind(context, &pipeline_line2d, 1, uniforms, countof(uniforms));
					vkCmdDraw(cmd->handle, (batch_line2d->offset / sizeof(Line)) * 6, 1, 0, 0);
				}

				vkCmdEndRendering(cmd->handle);
			}
			gfx_cmd_image_transition(cmd, RESOURCE_USAGE_PRESENT, main_target);
		} else { //  TODO: resize swapchain
			gfx_swapchain_resize(context, swapchains[1], dims.x, dims.y);
			if (depthbuffer->width != dims.x || depthbuffer->height != dims.y)
				gfx_image_resize(context, depthbuffer, dims.x, dims.y);
			if (offscreen_render->width != dims.x || offscreen_render->height != dims.y)
				gfx_image_resize(context, offscreen_render, dims.x, dims.y);
		}

		if (vkEndCommandBuffer(cmd->handle) != VK_SUCCESS) {
			LOG_INFO("failed to record command buffer.");
			break;
		}

		{ // :submit
			VkSwapchainKHR handles[MAX_SWAPCHAINS] = { 0 };
			VkSemaphore wait_semaphores[MAX_SWAPCHAINS] = { 0 };
			VkSemaphore signal_semaphores[MAX_SWAPCHAINS] = { 0 };
			VkPipelineStageFlags wait_stages[MAX_SWAPCHAINS] = {
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			};

			for (uint32_t swapchain_index = 0; swapchain_index < cmd->swapchain_count; ++swapchain_index) {
				handles[swapchain_index] = cmd->swapchains[swapchain_index]->handle;
				wait_semaphores[swapchain_index] = cmd->swapchains[swapchain_index]->image_available_semaphores[cmd->frame_index];
				signal_semaphores[swapchain_index] = cmd->swapchains[swapchain_index]->render_done_semaphores[cmd->swapchain_image_indices[swapchain_index]];
			}

			VkSubmitInfo submit_info = {
				.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
				.waitSemaphoreCount = cmd->swapchain_count,
				.pWaitSemaphores = wait_semaphores,
				.pWaitDstStageMask = wait_stages,
				.commandBufferCount = 1,
				.pCommandBuffers = &cmd->handle,
				.signalSemaphoreCount = cmd->swapchain_count,
				.pSignalSemaphores = signal_semaphores,
			};

			if (vkQueueSubmit(context->graphics_queue, 1, &submit_info, context->frame_commands[context->current_frame_index].in_flight_fence) != VK_SUCCESS) {
				LOG_ERROR("failed to submit command buffer to queue.");
				break;
			}

			if (cmd->swapchain_count) {
				VkPresentInfoKHR present_info = {
					.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
					.waitSemaphoreCount = cmd->swapchain_count,
					.pWaitSemaphores = signal_semaphores,
					.swapchainCount = cmd->swapchain_count,
					.pSwapchains = handles,
					.pImageIndices = cmd->swapchain_image_indices,
				};
				vkQueuePresentKHR(context->present_queue, &present_info);
			}
		}

		OS_Timestamp current_ts = os_file_last_modified(s("assets/shaders/compute/bin/test.compute.spv"));
		if (compute_ts != current_ts) {
			LOG_INFO("hot-reloading...");
			vkDeviceWaitIdle(context->device.logical); // TODO: Proper synchronization for hot-reload
			pipeline_destroy(context, &c_pipeline);

			ArenaTemp scratch = arena_scratch_begin(NULL);
			String8 compute_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/compute/bin/test.compute.spv"));
			c_pipeline = compute_pipeline_make(context, compute_bytecode);
			arena_scratch_end(scratch);

			compute_ts = current_ts;
		}

		context->current_frame_index = (context->current_frame_index + 1) % MAX_FRAMES_IN_FLIGHT;
		arena_reset(frame_arena);

		/* batch.offset = 0; */
		/* batch.memory = scratch_buffers[context->current_frame].mapped; */
	}

	// :destroy
	vkDeviceWaitIdle(context->device.logical);
	{
		pipeline_destroy(context, &c_pipeline);
		pipeline_destroy(context, &pipeline_skinning);
		pipeline_destroy(context, &pipeline_shadow);
		pipeline_destroy(context, &pipeline_3d);
		pipeline_destroy(context, &pipeline_2d);
		pipeline_destroy(context, &pipeline_skybox);
		pipeline_destroy(context, &pipeline_grass);
		pipeline_destroy(context, &pipeline_line3d);
		pipeline_destroy(context, &pipeline_line2d);
	}

	gfx_shutdown(context);

	os_surface_close(main_render);
	os_surface_close(popup_compute);
	os_display_shutdown();
	return 0;
}

// EXTENSIONS
static VKAPI_ATTR VkResult VKAPI_CALL STUB_vkCreateDebugUtilsMessenger(VkInstance i, const VkDebugUtilsMessengerCreateInfoEXT *c, const VkAllocationCallbacks *a, VkDebugUtilsMessengerEXT *m) {
	return VK_ERROR_EXTENSION_NOT_PRESENT;
}
static VKAPI_ATTR void VKAPI_CALL STUB_vkDestroyDebugUtilsMessenger(VkInstance i, VkDebugUtilsMessengerEXT m, const VkAllocationCallbacks *a) {}
static VKAPI_ATTR VkResult VKAPI_CALL STUB_vkSetDebugUtilsObjectName(VkDevice d, const VkDebugUtilsObjectNameInfoEXT *n) {
	return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL STUB_vkCmdBeginDebugUtilsLabel(VkCommandBuffer c, const VkDebugUtilsLabelEXT *l) {}
static VKAPI_ATTR void VKAPI_CALL STUB_vkCmdEndDebugUtilsLabel(VkCommandBuffer c) {}

PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessenger = STUB_vkCreateDebugUtilsMessenger;
PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessenger = STUB_vkDestroyDebugUtilsMessenger;
PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectName = STUB_vkSetDebugUtilsObjectName;
PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabel = STUB_vkCmdBeginDebugUtilsLabel;
PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabel = STUB_vkCmdEndDebugUtilsLabel;

// :map
static UniformType descriptor_type_to_uniform_type[VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT] = {
	[VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE] = UNIFORM_TYPE_IMAGE,
	[VK_DESCRIPTOR_TYPE_STORAGE_IMAGE] = UNIFORM_TYPE_STORAGE_IMAGE,
	[VK_DESCRIPTOR_TYPE_SAMPLER] = UNIFORM_TYPE_SAMPLER,
	[VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER] = UNIFORM_TYPE_SAMPLER_WITH_IMAGE,

	[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER] = UNIFORM_TYPE_UNIFORM_BUFFER,
	[VK_DESCRIPTOR_TYPE_STORAGE_BUFFER] = UNIFORM_TYPE_STORAGE_BUFFER,

	[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC] = UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC,
	[VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC] = UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC,
};

static VkDescriptorType uniform_type_to_descriptor_type[UNIFORM_TYPE_COUNT] = {
	[UNIFORM_TYPE_IMAGE] = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
	[UNIFORM_TYPE_STORAGE_IMAGE] = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
	[UNIFORM_TYPE_SAMPLER] = VK_DESCRIPTOR_TYPE_SAMPLER,
	[UNIFORM_TYPE_SAMPLER_WITH_IMAGE] = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	[UNIFORM_TYPE_UNIFORM_BUFFER] = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	[UNIFORM_TYPE_STORAGE_BUFFER] = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
	[UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC] = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
	[UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC] = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
};

static const char *uniform_type_to_string[UNIFORM_TYPE_COUNT] = {
	[UNIFORM_TYPE_IMAGE] = "UNIFORM_TYPE_IMAGE",
	[UNIFORM_TYPE_STORAGE_IMAGE] = "UNIFORM_TYPE_STORAGE_IMAGE",
	[UNIFORM_TYPE_SAMPLER] = "UNIFORM_TYPE_SAMPLER",
	[UNIFORM_TYPE_SAMPLER_WITH_IMAGE] = "UNIFORM_TYPE_SAMPLER_WITH_IMAGE",
	[UNIFORM_TYPE_UNIFORM_BUFFER] = "UNIFORM_TYPE_UNIFORM_BUFFER",
	[UNIFORM_TYPE_STORAGE_BUFFER] = "UNIFORM_TYPE_STORAGE_BUFFER",
	[UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC] = "UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC",
	[UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC] = "UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC"
};

VkImageLayout resource_usage_to_image_layout[RESOURCE_USAGE_COUNT] = {
	[RESOURCE_USAGE_UNDEFINED] = VK_IMAGE_LAYOUT_UNDEFINED,

	[RESOURCE_USAGE_TRANSFER_SRC] = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	[RESOURCE_USAGE_SHADER_READ] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	[RESOURCE_USAGE_VERTEX_SHADER_READ] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	[RESOURCE_USAGE_FRAGMENT_SHADER_READ] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	[RESOURCE_USAGE_COMPUTE_SHADER_READ] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	[RESOURCE_USAGE_VERTEX_BUFFER] = VK_IMAGE_LAYOUT_UNDEFINED,
	[RESOURCE_USAGE_INDEX_BUFFER] = VK_IMAGE_LAYOUT_UNDEFINED,
	[RESOURCE_USAGE_PRESENT] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,

	[RESOURCE_USAGE_TRANSFER_DST] = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	[RESOURCE_USAGE_SHADER_WRITE] = VK_IMAGE_LAYOUT_GENERAL,
	[RESOURCE_USAGE_VERTEX_SHADER_WRITE] = VK_IMAGE_LAYOUT_GENERAL,
	[RESOURCE_USAGE_FRAGMENT_SHADER_WRITE] = VK_IMAGE_LAYOUT_GENERAL,
	[RESOURCE_USAGE_COMPUTE_SHADER_WRITE] = VK_IMAGE_LAYOUT_GENERAL,
	[RESOURCE_USAGE_COLOR_ATTACHMENT] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	[RESOURCE_USAGE_DEPTH_ATTACHMENT] = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
};

VkImageUsageFlags gfx__image_options_to_vulkan_usage(ImageOptions opt) {
	VkImageUsageFlags result = 0;
	if (has_flag(opt.usage, IMAGE_USAGE_RENDER)) {
		if (pixel_format_is_depth(opt.format))
			result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		else
			result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}

	if (has_flag(opt.usage, IMAGE_USAGE_SAMPLE))
		result |= VK_IMAGE_USAGE_SAMPLED_BIT;

	if (has_flag(opt.usage, IMAGE_USAGE_STORAGE))
		result |= VK_IMAGE_USAGE_STORAGE_BIT;

	if (has_flag(opt.usage, IMAGE_USAGE_TRANSFER))
		result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

	if (result == 0) { // default
		result |= VK_IMAGE_USAGE_SAMPLED_BIT;
		result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}

	if (opt.pixels && (result & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0)
		result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

	return result;
}

VkBufferUsageFlags gfx__buffer_options_to_vulkan_usage(BufferOptions options) {
	VkBufferUsageFlags result = 0;
	if (has_flag(options.usage, BUFFER_USAGE_UNIFORM))
		result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	if (has_flag(options.usage, BUFFER_USAGE_STORAGE))
		result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	if (has_flag(options.usage, BUFFER_USAGE_VERTEX))
		result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	if (has_flag(options.usage, BUFFER_USAGE_INDEX))
		result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	if (has_flag(options.usage, BUFFER_USAGE_TRANSFER) || options.data)
		result |= VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	return result;
}

VkMemoryPropertyFlags memory_type_to_memory_property_flags[MEMORY_TYPE_COUNT] = {
	[MEMORY_TYPE_GPU] = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
	[MEMORY_TYPE_CPU] = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
};

VkFormat pixel_format_to_vulkan_format[PIXELFORMAT_COUNT] = {
	[PIXELFORMAT_RGBA8_UNORM] = VK_FORMAT_R8G8B8A8_UNORM,
	[PIXELFORMAT_RGBA8_SRGB] = VK_FORMAT_R8G8B8A8_SRGB,
	[PIXELFORMAT_RGBA16_FLOAT] = VK_FORMAT_R16G16B16A16_SFLOAT,
	[PIXELFORMAT_R32_FLOAT] = VK_FORMAT_R32_SFLOAT,
	[PIXELFORMAT_DEPTH] = VK_FORMAT_D32_SFLOAT,
	[PIXELFORMAT_DEPTH_STENCIL] = VK_FORMAT_D24_UNORM_S8_UINT,
	[PIXELFORMAT_BACKBUFFER] = VK_FORMAT_B8G8R8A8_SRGB,
};

uint32_t pixel_format_to_stride[PIXELFORMAT_COUNT] = {
	[PIXELFORMAT_RGBA8_UNORM] = 4,
	[PIXELFORMAT_RGBA8_SRGB] = 4,
	[PIXELFORMAT_RGBA16_FLOAT] = 2 * 4,
	[PIXELFORMAT_R32_FLOAT] = 4,
	[PIXELFORMAT_DEPTH] = 4,
	[PIXELFORMAT_DEPTH_STENCIL] = 4,
	[PIXELFORMAT_BACKBUFFER] = 4,
};

const char *pixel_format_to_string[PIXELFORMAT_COUNT] = {
	[PIXELFORMAT_RGBA8_UNORM] = "RGBA8_UNORM",
	[PIXELFORMAT_RGBA8_SRGB] = "RGBA8_SRGB",
	[PIXELFORMAT_RGBA16_FLOAT] = "RGBA16_FLOAT",
	[PIXELFORMAT_R32_FLOAT] = "R32_FLOAT",
	[PIXELFORMAT_DEPTH] = "DEPTH",
	[PIXELFORMAT_DEPTH_STENCIL] = "DEPTH_STENCIL",
	[PIXELFORMAT_BACKBUFFER] = "BACKBUFFER",
};

VkImageAspectFlags gfx__image_options_to_aspect(ImageOptions options) {
	if (pixel_format_is_depth_stencil(options.format))
		return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	if (pixel_format_is_depth(options.format))
		return VK_IMAGE_ASPECT_DEPTH_BIT;

	return VK_IMAGE_ASPECT_COLOR_BIT;
}

VkSampleCountFlags gfx__usage_to_vk_sample(VkPhysicalDeviceLimits limits, ImageOptions options) {
	VkSampleCountFlags result = MAX((VkSampleCountFlags)options.sample, VK_SAMPLE_COUNT_1_BIT);

	if (has_flag(options.usage, IMAGE_USAGE_SAMPLE)) {
		if (pixel_format_is_depth(options.format)) {
			result = MIN(result, limits.sampledImageDepthSampleCounts);
			result = MIN(result, limits.sampledImageStencilSampleCounts);
		} else {
			result = MIN(result, limits.sampledImageColorSampleCounts);
		}
	}

	if (has_flag(options.usage, IMAGE_USAGE_RENDER)) {
		if (pixel_format_is_depth(options.format)) {
			result = MIN(result, limits.framebufferDepthSampleCounts);
			result = MIN(result, limits.framebufferStencilSampleCounts);
		} else
			result = MIN(result, limits.framebufferColorSampleCounts);
	}

	if (has_flag(options.usage, IMAGE_USAGE_STORAGE))
		result = MIN(result, limits.storageImageSampleCounts);

	if (result < options.sample) {
		LOG_WARN("requested sample size of [%u] is too large, clamping to [%u]", options.sample, result);
	}

	return result;
}

uint32_t gfx__image_options_to_layer_count(ImageOptions opt) {
	return opt.slice_count ? opt.slice_count : opt.type == IMAGE_TYPE_CUBE ? 6
																		   : 1;
}

VkImageCreateFlags gfx__opt_to_vk_image_flags(ImageOptions opt) {
	VkImageCreateFlags result = 0;

	if (opt.type == IMAGE_TYPE_CUBE)
		result = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

	return result;
}

VkImageViewType gfx__opt_to_vk_view_type(ImageOptions opt) {
	VkImageViewType result = VK_IMAGE_VIEW_TYPE_2D;

	if (opt.type == IMAGE_TYPE_3D)
		result = VK_IMAGE_VIEW_TYPE_3D;
	if (opt.type == IMAGE_TYPE_CUBE)
		result = VK_IMAGE_VIEW_TYPE_CUBE;

	return result;
}

VkImageLayout gfx__opt_to_initial_layout(ImageOptions opt) {
	VkImageLayout result = VK_IMAGE_LAYOUT_UNDEFINED;

	if (has_flag(opt.usage, IMAGE_USAGE_SAMPLE))
		result = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	if (has_flag(opt.usage, IMAGE_USAGE_RENDER))
		result = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	return result;
}

uint32_t gfx__find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties memory_properties;
	vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

	for (uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
		if ((type_filter & (1 << index)) && (memory_properties.memoryTypes[index].propertyFlags & properties) == properties) {
			return index;
		}
	}

	LOG_ERROR("Failed to find suitable memory type!");
	ASSERT(false);
	return 0;
}

GFX_Buffer *gfx_buffer_make(GFX_Context *context, uint64_t size, BufferOptions options) {
	GFX_Buffer *result = 0;
	LOG_DEBUG("creating vulkan buffer.");

	bool ok = context && (context->buffer_count < MAX_BUFFERS || context->first_free_buffer);
	const char *name = options.debug_name ? options.debug_name : "<unnamed_buffer>";

	if (ok) { // acquire new buffer
		if (context->first_free_buffer) {
			result = context->first_free_buffer;
			context->first_free_buffer = result->next;
			result->next = 0;

			memory_zero(result, sizeof(*result));
		} else
			result = &context->buffer_pool[context->buffer_count];

		context->buffer_count++;
	}

	VkBufferUsageFlags vk_usage = 0;
	VkMemoryPropertyFlags memory_flags = 0;
	if (ok) {
		result->options = options;

		vk_usage = gfx__buffer_options_to_vulkan_usage(options);
		memory_flags = memory_type_to_memory_property_flags[options.memory];

		ok = vk_usage && memory_flags;
		if (ok == false) {
			LOG_WARN("invalid properties passed, aborting creation of %s", name);
		}
	}

	if (ok) { // create buffer handle
		result->size = size;
		uint32_t family_indices[] = { context->graphics_index, context->transfer_index };
		result->info = (VkBufferCreateInfo){
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = result->size,
			.usage = vk_usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		};

		ok = vkCreateBuffer(context->device.logical, &result->info, 0, &result->handle) == VK_SUCCESS;
	}

	if (ok) { // allocate buffer memory
		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements(context->device.logical, result->handle, &memory_requirements);

		uint32_t memory_type_index = gfx__find_memory_type(context->device.physical, memory_requirements.memoryTypeBits, memory_flags);
		size_t allocation_size = memory_requirements.size;

		VkMemoryAllocateFlagsInfo allocate_flag_info = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
			.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
		};

		VkMemoryAllocateInfo allocate_info = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.pNext = &allocate_flag_info,
			.allocationSize = memory_requirements.size,
			.memoryTypeIndex = memory_type_index,
		};

		ok = vkAllocateMemory(context->device.logical, &allocate_info, 0, &result->memory) == VK_SUCCESS;
	}

	if (ok) { // bind memory & get buffer device address
		ok = vkBindBufferMemory(context->device.logical, result->handle, result->memory, 0) == VK_SUCCESS;

		if (vk_usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
			VkBufferDeviceAddressInfo bda_info = {
				.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
				.buffer = result->handle,
			};
			result->address = vkGetBufferDeviceAddress(context->device.logical, &bda_info);
		}
	}

	if (ok && options.data) { // upload
		GFX_CommandContext *cmd = gfx_transfer_cmd(context);

		gfx_cmd_buffer_upload(cmd, result, 0, size, options.data);
	}

#if DEV_BUILD
	if (ok) { // assign debug name
		VkDebugUtilsObjectNameInfoEXT name_info = {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
			.pObjectName = name,
			.objectType = VK_OBJECT_TYPE_BUFFER,
			.objectHandle = (uint64_t)result->handle,

		};
		vkSetDebugUtilsObjectName(context->device.logical, &name_info);
	}
#endif

	if (ok == false) // remove half-made resources on error
		gfx_buffer_destroy(context, result);

	return result;
}

GFX_Image *gfx_image_make(GFX_Context *context, uint32_t width, uint32_t height, ImageOptions options) {
	GFX_Image *result = 0;
	LOG_DEBUG("creating vulkan image.");

	bool ok = context && (context->image_count < MAX_IMAGES || context->first_free_image);
	const char *name = options.debug_name ? options.debug_name : "<unnamed_image>";

	if (ok) { // acquire new image
		if (context->first_free_image) {
			result = context->first_free_image;
			context->first_free_image = result->next;
			result->next = 0;

			memory_zero(result, sizeof(*result));
		} else
			result = &context->image_pool[context->image_count];

		context->image_count++;
	}

	uint32_t layer_count = gfx__image_options_to_layer_count(options);
	VkFormat vk_format = pixel_format_to_vulkan_format[options.format];

	if (ok) { // make vulkan image handle

		VkImageUsageFlags vk_usage = gfx__image_options_to_vulkan_usage(options);
		VkSampleCountFlags vk_sample = gfx__usage_to_vk_sample(context->device.limits, options);
		VkImageCreateFlags vk_flags = gfx__opt_to_vk_image_flags(options);

		result->width = width, result->height = height;
		result->options = options;
		result->res_usage = RESOURCE_USAGE_UNDEFINED;
		options.max_mip_level = options.max_mip_level ? options.max_mip_level : 1;
		result->miplevels = MIN(options.max_mip_level, (uint32_t)log2(maxf(width, height)) + 1);

		VkImageCreateInfo image_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.flags = vk_flags,
			.format = vk_format,
			.extent = {
			  .width = width,
			  .height = height,
			  .depth = 1,
			},
			.mipLevels = result->miplevels,
			.arrayLayers = layer_count,
			.samples = vk_sample,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = vk_usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.initialLayout = resource_usage_to_image_layout[result->res_usage],
		};

		ok = vkCreateImage(context->device.logical, &image_info, 0, &result->handle) == VK_SUCCESS;
	}

	if (ok) { // allocate memory
		VkMemoryRequirements memory_requirements;
		vkGetImageMemoryRequirements(context->device.logical, result->handle, &memory_requirements);

		VkMemoryAllocateInfo allocate_info = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = memory_requirements.size,
			.memoryTypeIndex = gfx__find_memory_type(context->device.physical, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
		};

		ok = vkAllocateMemory(context->device.logical, &allocate_info, 0, &result->memory) == VK_SUCCESS;
	}

	if (ok) // bind memory to handle
		vkBindImageMemory(context->device.logical, result->handle, result->memory, 0);

	if (ok) { // create image view
		VkImageViewType vk_type = gfx__opt_to_vk_view_type(options);
		VkImageAspectFlags vk_aspect = gfx__image_options_to_aspect(options);

		VkImageViewCreateInfo view_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = result->handle,
			.viewType = vk_type,
			.format = vk_format,
			.components = {
			  .r = VK_COMPONENT_SWIZZLE_IDENTITY,
			  .g = VK_COMPONENT_SWIZZLE_IDENTITY,
			  .b = VK_COMPONENT_SWIZZLE_IDENTITY,
			  .a = VK_COMPONENT_SWIZZLE_IDENTITY,
			},
			.subresourceRange = {
			  .aspectMask = vk_aspect,
			  .baseMipLevel = 0,
			  .levelCount = result->miplevels,
			  .baseArrayLayer = 0,
			  .layerCount = layer_count,
			}
		};

		ok = vkCreateImageView(context->device.logical, &view_info, 0, &result->view) == VK_SUCCESS;
	}

	if (ok && options.pixels) { // upload
		GFX_CommandContext *cmd = gfx_transfer_cmd(context);

		gfx_cmd_image_upload(cmd, result, result->width, result->height, options.pixels);

		VkFormatProperties format_properties;
		vkGetPhysicalDeviceFormatProperties(context->device.physical, vk_format, &format_properties);
		bool blit_support = format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
		if (blit_support == false) {
			LOG_WARN("pixel format (%s) does not support linear blitting.", pixel_format_to_string[options.format]);
		}

		if (result->miplevels > 1 && blit_support) {
			uint32_t mip_width = width, mip_height = height;
			VkImageAspectFlags aspect = gfx__image_options_to_aspect(options);

			for (uint32_t level = 1; level < result->miplevels; ++level) {
				gfx_cmd_image_barrier(cmd, RESOURCE_USAGE_TRANSFER_DST, RESOURCE_USAGE_TRANSFER_SRC, level - 1, 1, result);

				VkImageBlit blit = { 0 };

				VkImageBlit blit_info = {
					.srcOffsets[1] = {
					  .x = mip_width,
					  .y = mip_height,
					  .z = 1,
					},
					.srcSubresource = {
					  .aspectMask = aspect,
					  .baseArrayLayer = 0,
					  .layerCount = layer_count,
					  .mipLevel = level - 1,
					},
					.dstOffsets[1] = {
					  .x = mip_width > 1 ? mip_width / 2 : 1,
					  .y = mip_height > 1 ? mip_height / 2 : 1,
					  .z = 1,
					},
					.dstSubresource = {
					  .aspectMask = aspect,
					  .baseArrayLayer = 0,
					  .layerCount = layer_count,
					  .mipLevel = level,
					},
				};

				vkCmdBlitImage(cmd->handle,
					result->handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					result->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1, &blit_info, VK_FILTER_LINEAR);

				if (mip_width > 1)
					mip_width /= 2;
				if (mip_height > 1)
					mip_height /= 2;
			}
		}
		gfx_cmd_image_barrier(cmd, RESOURCE_USAGE_TRANSFER_DST, RESOURCE_USAGE_TRANSFER_SRC, result->miplevels - 1, 1, result);

		gfx_cmd_image_transition(cmd, RESOURCE_USAGE_SHADER_READ, result);
	}

#if DEV_BUILD
	if (ok) { // assign debug name
		VkDebugUtilsObjectNameInfoEXT name_info = {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
			.pObjectName = name,
			.objectType = VK_OBJECT_TYPE_IMAGE,
			.objectHandle = (uint64_t)result->handle,

		};
		vkSetDebugUtilsObjectName(context->device.logical, &name_info);
	}
#endif

	if (ok == false) // remove half-made resources on error
		gfx_image_destroy(context, result);

	/* LOG_INFO("image loaded successfuly (%ux%u | %s)", indexof(context->image_pool, image), width, height, image_format_to_string[format]); */
	return result;
}

GFX_Sampler *gfx_sampler_make(GFX_Context *context, SamplerOptions opt) {
	GFX_Sampler *result = 0;
	LOG_DEBUG("creating vulkan sampler.");

	bool ok = context && (context->sampler_count < MAX_SAMPLERS || context->first_free_sampler);
	const char *name = opt.debug_name ? opt.debug_name : "<unnamed_sampler>";

	if (ok) { // acquire new sampler
		if (context->first_free_sampler) {
			result = context->first_free_sampler;
			context->first_free_sampler = result->next;
			result->next = 0;

			memory_zero(result, sizeof(*result));
		} else
			result = &context->sampler_pool[context->sampler_count];

		context->sampler_count++;
	}

	if (ok) {
		result->info = (VkSamplerCreateInfo){
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter = (VkFilter)opt.mag_filter,
			.minFilter = (VkFilter)opt.min_filter,
			.mipmapMode = (VkSamplerMipmapMode)opt.mipmap_filter,
			.addressModeU = (VkSamplerAddressMode)opt.address_mode_u,
			.addressModeV = (VkSamplerAddressMode)opt.address_mode_v,
			.addressModeW = (VkSamplerAddressMode)opt.address_mode_w,
			.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
			.anisotropyEnable = VK_TRUE,
			.maxAnisotropy = context->device.limits.maxSamplerAnisotropy,
			.compareEnable = opt.compare_enable,
			.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
			.maxLod = VK_LOD_CLAMP_NONE
		};

		ok = vkCreateSampler(context->device.logical, &result->info, NULL, &result->handle) == VK_SUCCESS;
		if (ok == false) {
			LOG_WARN("failed to create vulkan sampler.");
		}
	}

#if DEV_BUILD
	if (ok) { // assign debug name
		VkDebugUtilsObjectNameInfoEXT name_info = {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
			.pObjectName = name,
			.objectType = VK_OBJECT_TYPE_SAMPLER,
			.objectHandle = (uint64_t)result->handle,

		};
		vkSetDebugUtilsObjectName(context->device.logical, &name_info);
	}
#endif

	if (ok == false) // remove half-made resources on error
		gfx_sampler_destroy(context, result);

	return result;
}

GFX_Swapchain *gfx_swapchain_make(GFX_Context *context, OS_Surface *surface, const char *debug_name) {
	GFX_Swapchain *result = 0;
	ArenaTemp scratch = arena_scratch_begin(0);
	LOG_DEBUG("creating vulkan surface.");

	bool ok = context && (context->swapchain_count < MAX_SWAPCHAINS || context->first_free_swapchain);
	const char *name = debug_name ? debug_name : "<unnamed_swapchain";

	if (ok) { // acquire new swapchain
		if (context->first_free_swapchain) {
			result = context->first_free_swapchain;
			context->first_free_swapchain = result->next;
			result->next = 0;

			memory_zero(result, sizeof(*result));
		} else
			result = &context->swapchain_pool[context->swapchain_count];

		result->native = surface;
		context->swapchain_count++;
	}

	if (ok) { // create surface
		VkXcbSurfaceCreateInfoKHR surface_create_info = {
			.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
			.connection = os_native_display_handle(),
			.window = (uint32_t)(uint64_t)os_native_surface_handle(surface),
		}; // TODO: Have os decide this

		ok = vkCreateXcbSurfaceKHR(context->instance, &surface_create_info, 0, &result->surface) == VK_SUCCESS;
		if (ok == false) {
			LOG_WARN("failed to create vulkan surface.");
		}
	}

	VkSurfaceCapabilitiesKHR capabilities;

	uint32_t surface_format_count = 0;
	VkSurfaceFormatKHR *surface_formats;

	uint32_t present_mode_count = 0;
	VkPresentModeKHR *present_modes;

	if (ok) { // query surface suitability
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->device.physical, result->surface, &capabilities);

		vkGetPhysicalDeviceSurfaceFormatsKHR(context->device.physical, result->surface, &surface_format_count, 0);
		surface_formats = arena_push_count(scratch.arena, VkSurfaceFormatKHR, surface_format_count);
		vkGetPhysicalDeviceSurfaceFormatsKHR(context->device.physical, result->surface, &surface_format_count, surface_formats);

		vkGetPhysicalDeviceSurfacePresentModesKHR(context->device.physical, result->surface, &present_mode_count, 0);
		present_modes = arena_push_count(scratch.arena, VkPresentModeKHR, present_mode_count);
		vkGetPhysicalDeviceSurfacePresentModesKHR(context->device.physical, result->surface, &present_mode_count, present_modes);

		uint32_t queue_family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(context->device.physical, &queue_family_count, 0);
		VkQueueFamilyProperties *queue_family_properties = arena_push_count(scratch.arena, VkQueueFamilyProperties, queue_family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(context->device.physical, &queue_family_count, queue_family_properties);

		if (context->present_index == -1)
			for (uint32_t index = 0; index < queue_family_count; ++index) {
				VkQueueFlags flags = queue_family_properties[index].queueFlags;

				VkBool32 present_support = false;
				vkGetPhysicalDeviceSurfaceSupportKHR(context->device.physical, index, result->surface, &present_support);
				if (present_support && context->present_index == -1) {
					context->present_index = index;
					break;
				}
			}

		ok &= surface_format_count > 0; // valid surface formats available
		ok &= present_mode_count > 0; // valid present mode available
		ok &= context->present_index != -1; // supports present queue
	}

	if (ok) { // create swapchain
		vkGetDeviceQueue(context->device.logical, context->present_index, 0, &context->present_queue);

		VkSurfaceFormatKHR selected_format = surface_formats[0];
		for (uint32_t format_index = 0; format_index < surface_format_count; ++format_index) {
			if (surface_formats[format_index].format == VK_FORMAT_B8G8R8A8_SRGB &&
				surface_formats[format_index].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { // ideal format
				selected_format = surface_formats[format_index];
				break;
			}
		}

		VkPresentModeKHR selected_present_mode = present_modes[0];
		for (uint32_t mode_index = 0; mode_index < present_mode_count; mode_index++) {
			// NOTE: Caps framerate to monitor framerate
			/* selected_present_mode = VK_PRESENT_MODE_FIFO_KHR; */
			/* break; */

			// NOTE: Uncap framerate on XWayland
			/* if (present_modes[mode_index] == VK_PRESENT_MODE_IMMEDIATE_KHR) { */
			/* 	selected_present_mode = present_modes[mode_index]; */
			/* 	break; */
			/* } */
			if (present_modes[mode_index] == VK_PRESENT_MODE_MAILBOX_KHR) // ideal presentation mode
				selected_present_mode = present_modes[mode_index];
		}

		uint32x2 surface_size = os_surface_size(surface);
		float dpi = os_surface_dpi(surface);

		VkExtent2D selected_extents =
			capabilities.currentExtent.width != UINT32_MAX
			? capabilities.currentExtent
			: (VkExtent2D){ .width = (uint32_t)(surface_size.x * dpi), .height = (uint32_t)((float)surface_size.y * dpi) };

		uint32_t queue_family_indices[] = { (uint32_t)context->graphics_index, (uint32_t)context->present_index };

		uint32_t image_count = capabilities.minImageCount + 1;
		if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
			image_count = capabilities.maxImageCount;

		image_count = MIN(image_count, SWAPCHAIN_IMAGE_COUNT);

		result->info = (VkSwapchainCreateInfoKHR){
			.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
			.surface = result->surface,
			.minImageCount = image_count,
			.imageFormat = selected_format.format,
			.imageColorSpace = selected_format.colorSpace,
			.imageExtent = selected_extents,
			.imageArrayLayers = 1,
			.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			.preTransform = capabilities.currentTransform,
			.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			.presentMode = selected_present_mode,
			.clipped = VK_TRUE,
		};

		if (queue_family_indices[0] != queue_family_indices[1]) {
			result->info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			result->info.queueFamilyIndexCount = 2;
			result->info.pQueueFamilyIndices = queue_family_indices;
		} else
			result->info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

		ok = vkCreateSwapchainKHR(context->device.logical, &result->info, NULL, &result->handle) == VK_SUCCESS;
	}

	if (ok) { // get the swapchain images & create image views
		vkGetSwapchainImagesKHR(context->device.logical, result->handle, &result->image_count, NULL);
		vkGetSwapchainImagesKHR(context->device.logical, result->handle, &result->image_count, result->images);

		for (uint32_t image_index = 0; image_index < result->image_count; ++image_index) {
			result->view_infos[image_index] = (VkImageViewCreateInfo){
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = result->images[image_index],
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = result->info.imageFormat,
				.components = {
				  .r = VK_COMPONENT_SWIZZLE_IDENTITY,
				  .g = VK_COMPONENT_SWIZZLE_IDENTITY,
				  .b = VK_COMPONENT_SWIZZLE_IDENTITY,
				  .a = VK_COMPONENT_SWIZZLE_IDENTITY,
				},
				.subresourceRange = {
				  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				  .baseMipLevel = 0,
				  .levelCount = 1,
				  .baseArrayLayer = 0,
				  .layerCount = 1,
				}
			};

			ok &= vkCreateImageView(context->device.logical, result->view_infos + image_index, NULL, &result->views[image_index]) == VK_SUCCESS;
		}
	}

	if (ok) { // fill in defaults for wrapper
		GFX_Image *wrapper = &result->wrapper;

		wrapper->options = (ImageOptions){
			.debug_name = name,
			.format = PIXELFORMAT_BACKBUFFER,
			.sample = SAMPLE_COUNT_1,
			.type = IMAGE_TYPE_2D,
			.usage = IMAGE_USAGE_RENDER | IMAGE_USAGE_TRANSFER,
		};
		wrapper->miplevels = 1;
		wrapper->width = result->info.imageExtent.width;
		wrapper->height = result->info.imageExtent.height;
	}

	if (ok) { // create semaphores
		VkSemaphoreCreateInfo s_create_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		};

		for (uint32_t index = 0; index < countof(result->image_available_semaphores); ++index)
			ok &= vkCreateSemaphore(context->device.logical, &s_create_info, 0, result->image_available_semaphores + index) == VK_SUCCESS;
		for (uint32_t index = 0; index < countof(result->render_done_semaphores); ++index)
			ok &= vkCreateSemaphore(context->device.logical, &s_create_info, 0, result->render_done_semaphores + index) == VK_SUCCESS;
	}

#if DEV_BUILD
	if (ok) { // assign debug name
		VkDebugUtilsObjectNameInfoEXT name_info = {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
			.pObjectName = name,
			.objectType = VK_OBJECT_TYPE_SWAPCHAIN_KHR,
			.objectHandle = (uint64_t)result->handle,

		};
		vkSetDebugUtilsObjectName(context->device.logical, &name_info);
	}
#endif

	if (ok == false) { // remove half-made resources on error
		gfx_swapchain_destroy(context, result);
		LOG_ERROR("failed to create graphics surface.");
	}

	arena_scratch_end(scratch);
	return result;
}

bool gfx__uniforms_to_descriptor_bindings(Uniform *uniforms, uint32_t uniform_count, VkDescriptorSetLayoutBinding bindings[32], VkShaderStageFlagBits stages) {
	bool ok = uniforms && uniform_count > 0 && bindings;

	if (ok) {
		for (uint32_t index = 0; index < uniform_count; ++index) {
			Uniform *uniform = &uniforms[index];

			bindings[index] = (VkDescriptorSetLayoutBinding){
				.binding = uniform->binding,
				.descriptorCount = uniform->count,
				.descriptorType = uniform_type_to_descriptor_type[uniform->type],
				.stageFlags = stages
			};
		}
	}

	return ok;
}

static inline bool gfx__reflect_shader_uniforms(String8 bytecode, UniformSet sets[MAX_UNIFORM_SETS]) {
	bool ok = sets;

	ArenaTemp scratch = arena_scratch_begin(0);

	SpvReflectShaderModule module = { 0 };
	if (ok) { // reflect shader module
		ok = spvReflectCreateShaderModule(bytecode.length, bytecode.text, &module) == SPV_REFLECT_RESULT_SUCCESS;

		if (ok == false)
			LOG_ERROR("failed to reflect shader bytecode.");
	}

	uint32_t set_count = 0;
	SpvReflectDescriptorSet *reflect_sets = 0;
	if (ok) { // enumerate descriptor sets
		ok = spvReflectEnumerateDescriptorSets(&module, &set_count, NULL) == SPV_REFLECT_RESULT_SUCCESS;

		if (ok) {
			reflect_sets = arena_push_count(scratch.arena, SpvReflectDescriptorSet, set_count);
			ok = spvReflectEnumerateDescriptorSets(&module, &set_count, &reflect_sets) == SPV_REFLECT_RESULT_SUCCESS;
		}
	}

	if (ok) { // populate uniform metadata
		ASSERT(set_count <= MAX_UNIFORM_SETS);

		for (uint32_t set_index = 0; set_index < MIN(set_count, 3); ++set_index) {
			SpvReflectDescriptorSet *spv_set = &reflect_sets[set_index];
			UniformSet *set = &sets[spv_set->set];

			for (uint32_t binding_index = 0; binding_index < spv_set->binding_count; ++binding_index) {
				SpvReflectDescriptorBinding *spv_binding = spv_set->bindings[binding_index];

				int32_t existing_index = -1;
				for (uint32_t search_index = 0; search_index < set->uniform_count; ++search_index) {
					if (set->uniforms[search_index].binding == spv_binding->binding) {
						existing_index = search_index;
						break;
					}
				}

				Uniform *uniform = &set->uniforms[existing_index != -1 ? (uint32_t)existing_index : set->uniform_count++];

				memory_copy(uniform->name, spv_binding->name, MIN(sizeof(uniform->name), str8_wrap(spv_binding->name).length));
				if (spv_binding->descriptor_type >= SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER &&
					spv_binding->descriptor_type <= SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC &&
					spv_binding->name[0] == 0) {
					String8 name = str8_wrap(spv_binding->block.member_count == 1 ? spv_binding->block.members[0].name : spv_binding->block.name);
					memory_copy(uniform->name, name.text, MIN(sizeof(uniform->name) - 1, name.length));
				}
				ASSERT(uniform->name[0] != 0);

				uniform->binding = spv_binding->binding;
				uniform->count = spv_binding->count;

				ASSERT(spv_binding->descriptor_type < countof(descriptor_type_to_uniform_type));
				uniform->type = descriptor_type_to_uniform_type[(VkDescriptorType)spv_binding->descriptor_type];
			}
		}
	}

	arena_scratch_end(scratch);
	spvReflectDestroyShaderModule(&module);

	return ok;
}

GFX_Pipeline compute_pipeline_make(GFX_Context *context, String8 compute_bytecode) {
	LOG_DEBUG("creating vulkan compute pipeline.");
	GFX_Pipeline result = { 0 };
	const char *name = result.options.debug_name = "<unnamed_compute>";

	bool ok = compute_bytecode.text && compute_bytecode.length > 0;
	if (ok == false)
		LOG_WARN("invalid shader bytecode passed.");

	if (ok) { // create shader module
		VkShaderModuleCreateInfo csm_create_info = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.pCode = (void *)compute_bytecode.text,
			.codeSize = compute_bytecode.length,
		};

		ok = vkCreateShaderModule(context->device.logical, &csm_create_info, NULL, &result.shaders[SHADER_STAGE_COMPUTE]) == VK_SUCCESS;
		if (ok == false) {
			LOG_WARN("failed to create compute pipeline shader module.");
		}
	}

	uint32_t set_count = 0;
	if (ok) { // create descriptor set layouts
		gfx__reflect_shader_uniforms(compute_bytecode, result.set_infos);

		VkDescriptorSetLayoutBinding bindings[32] = { 0 };
		for (uint32_t set_index = 0; set_index < MAX_UNIFORM_SETS; ++set_index) {
			UniformSet *set = &result.set_infos[set_count];
			if (set->uniform_count == 0)
				continue;
			gfx__uniforms_to_descriptor_bindings(set->uniforms, set->uniform_count, bindings, VK_SHADER_STAGE_COMPUTE_BIT);

			VkDescriptorSetLayoutCreateInfo dsl_create_info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = set->uniform_count,
				.pBindings = bindings,
			};
			ok &= vkCreateDescriptorSetLayout(context->device.logical, &dsl_create_info, 0, &result.set_layouts[set_count]) == VK_SUCCESS;

			set_count = ok ? set_count + 1 : set_count;
		}
	}

	if (ok) { // create pipeline layout
		VkPipelineLayoutCreateInfo pl_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = set_count,
			.pSetLayouts = result.set_layouts,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = &context->global_range,
		};

		ok = vkCreatePipelineLayout(context->device.logical, &pl_create_info, NULL, &result.layout) == VK_SUCCESS;
		if (ok == false) {
			LOG_WARN("failed to create compute pipeline layout.");
		}
	}

	if (ok) { // create compute pipeline
		VkPipelineShaderStageCreateInfo compute_stage = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = result.shaders[SHADER_STAGE_COMPUTE],
			.pName = "main",
		};

		VkComputePipelineCreateInfo cp_create_info = {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = compute_stage,
			.layout = result.layout,
		};

		ok = vkCreateComputePipelines(context->device.logical, 0, 1, &cp_create_info, NULL, &result.handle) == VK_SUCCESS;
		if (ok == false) {
			LOG_WARN("failed to create compute pipeline.");
		}
	}

	if (ok == false) // remove half-made resources on error
		pipeline_destroy(context, &result);

	return result;
}

GFX_Pipeline graphics_pipeline_make(GFX_Context *context, String8 vertex_bytecode, String8 fragment_bytecode, PipelineOptions options) {
	LOG_DEBUG("creating vulkan grahpics pipeline.");
	GFX_Pipeline result = { 0 };
	result.options = options;
	const char *name = result.options.debug_name ? result.options.debug_name : "<unnamed_raster>";
	result.options.debug_name = name;

	bool ok = true;

	if (ok) { // check validitiy of shader code
		ok &= vertex_bytecode.text && vertex_bytecode.length > 0;
		ok &= fragment_bytecode.text && fragment_bytecode.length > 0;

		if (ok == false) {
			LOG_WARN("invalid shader bytecode passed.");
		}
	}

	if (ok) { // create shader module
		VkShaderModuleCreateInfo vsm_create_info = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.pCode = (void *)vertex_bytecode.text,
			.codeSize = vertex_bytecode.length,
		};

		VkShaderModuleCreateInfo fsm_create_info = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.pCode = (void *)fragment_bytecode.text,
			.codeSize = fragment_bytecode.length,
		};

		ok &= vkCreateShaderModule(context->device.logical, &vsm_create_info, NULL, &result.shaders[SHADER_STAGE_VERTEX]) == VK_SUCCESS;
		ok &= vkCreateShaderModule(context->device.logical, &fsm_create_info, NULL, &result.shaders[SHADER_STAGE_FRAGMENT]) == VK_SUCCESS;
		if (ok == false) {
			LOG_WARN("failed to create vertex/fragment shader module.");
		}
	}

	uint32_t set_count = 0;
	if (ok) { // create descriptor set layouts

		gfx__reflect_shader_uniforms(fragment_bytecode, result.set_infos);
		gfx__reflect_shader_uniforms(vertex_bytecode, result.set_infos);

		VkDescriptorSetLayoutBinding bindings[32] = { 0 };
		for (uint32_t set_index = 0; set_index < MAX_UNIFORM_SETS; ++set_index) {
			UniformSet *set = &result.set_infos[set_count];
			if (set->uniform_count == 0)
				continue;
			gfx__uniforms_to_descriptor_bindings(set->uniforms, set->uniform_count, bindings, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT);

			VkDescriptorSetLayoutCreateInfo dsl_create_info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = set->uniform_count,
				.pBindings = bindings,
			};
			ok &= vkCreateDescriptorSetLayout(context->device.logical, &dsl_create_info, 0, &result.set_layouts[set_count]) == VK_SUCCESS;

			set_count = ok ? set_count + 1 : set_count;
		}
	}

	if (ok) { // create pipeline layout
		VkPipelineLayoutCreateInfo pl_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = set_count,
			.pSetLayouts = result.set_layouts,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = &context->global_range,
		};

		ok = vkCreatePipelineLayout(context->device.logical, &pl_create_info, NULL, &result.layout) == VK_SUCCESS;
		if (ok == false) {
			LOG_WARN("failed to create pipeline layout.");
		}
	}

	if (ok) { // create graphics pipeline
		VkPipelineShaderStageCreateInfo shader_stages[] = {
			{
			  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			  .stage = VK_SHADER_STAGE_VERTEX_BIT,
			  .module = result.shaders[SHADER_STAGE_VERTEX],
			  .pName = "main",
			},
			{
			  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			  .module = result.shaders[SHADER_STAGE_FRAGMENT],
			  .pName = "main",
			}
		};

		VkDynamicState dynamic_states[] = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo ds_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = countof(dynamic_states),
			.pDynamicStates = dynamic_states
		};

		VkPipelineVertexInputStateCreateInfo vis_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		};

		VkPipelineInputAssemblyStateCreateInfo ias_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			.primitiveRestartEnable = VK_FALSE
		};

		VkPipelineViewportStateCreateInfo vps_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
			.viewportCount = 1,
			.scissorCount = 1,
		};

		VkPipelineRasterizationStateCreateInfo rs_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.depthClampEnable = VK_FALSE,
			.rasterizerDiscardEnable = VK_FALSE,
			.polygonMode = VK_POLYGON_MODE_FILL,
			.lineWidth = 1.0f,
			.cullMode = options.cull_mode,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
			.depthBiasEnable = VK_FALSE,
		};

		VkPipelineMultisampleStateCreateInfo mss_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.sampleShadingEnable = VK_FALSE,
			.rasterizationSamples = options.sample_count ? (VkSampleCountFlags)options.sample_count : VK_SAMPLE_COUNT_1_BIT,
			.alphaToCoverageEnable = options.color_attachment_count ? VK_TRUE : VK_FALSE,
			.minSampleShading = 1.0f,
		};

		VkPipelineDepthStencilStateCreateInfo depth_stencil_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
			.depthTestEnable = options.disable_depth_test == false,
			.depthWriteEnable = options.disable_depth_write == false,
			.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
			.depthBoundsTestEnable = VK_FALSE,
			.minDepthBounds = 0.0f,
			.maxDepthBounds = 1.0f
		};

		VkPipelineColorBlendAttachmentState color_attachment_blends[MAX_COLOR_ATTACHMENTS] = { 0 };
		VkPipelineColorBlendStateCreateInfo cbs_create_info = { 0 };

		VkFormat color_attachment_formats[MAX_COLOR_ATTACHMENTS] = { 0 };
		VkPipelineRenderingCreateInfo r_create_info = { 0 };

		{ // attachment state
			VkPipelineColorBlendAttachmentState color_attachment_blend_default = {
				.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
				.blendEnable = VK_TRUE,

				// Color: result = src.rgb * src.a + dst.rgb * (1 - src.a)
				.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
				.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
				.colorBlendOp = VK_BLEND_OP_ADD,

				// Alpha: result = src.a * 1 + dst.a * (1 - src.a)
				.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
				.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
				.alphaBlendOp = VK_BLEND_OP_ADD,
			};
			for (uint32_t index = 0; index < options.color_attachment_count; ++index)
				memory_copy(color_attachment_blends + index, &color_attachment_blend_default, sizeof(VkPipelineColorBlendAttachmentState));

			cbs_create_info = (VkPipelineColorBlendStateCreateInfo){
				.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
				.logicOpEnable = VK_FALSE,
				.attachmentCount = options.color_attachment_count,
				.pAttachments = color_attachment_blends,
			};

			for (uint32_t attachment_index = 0; attachment_index < options.color_attachment_count; ++attachment_index)
				color_attachment_formats[attachment_index] = pixel_format_to_vulkan_format[options.color_attachments[attachment_index]];

			r_create_info = (VkPipelineRenderingCreateInfo){
				.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
				.colorAttachmentCount = options.color_attachment_count,
				.pColorAttachmentFormats = color_attachment_formats,
				.depthAttachmentFormat = pixel_format_is_depth_stencil(options.depth_attachment) ? pixel_format_to_vulkan_format[options.depth_attachment] : VK_FORMAT_D32_SFLOAT,
			};
		}

		VkGraphicsPipelineCreateInfo gp_create_info = {
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.pNext = &r_create_info,
			.stageCount = countof(shader_stages),
			.pStages = shader_stages,
			.pVertexInputState = &vis_create_info,
			.pInputAssemblyState = &ias_create_info,
			.pViewportState = &vps_create_info,
			.pRasterizationState = &rs_create_info,
			.pMultisampleState = &mss_create_info,
			.pDepthStencilState = &depth_stencil_create_info,
			.pColorBlendState = &cbs_create_info,
			.pDynamicState = &ds_create_info,
			.layout = result.layout,
		};

		ok = vkCreateGraphicsPipelines(context->device.logical, 0, 1, &gp_create_info, NULL, &result.handle) == VK_SUCCESS;
		if (ok == false) {
			LOG_WARN("failed to create compute pipeline.");
		}
	}

	if (ok == false) // remove half-made resources on error
		pipeline_destroy(context, &result);

	return result;
}

bool gfx_buffer_destroy(GFX_Context *context, GFX_Buffer *buffer) {
	bool ok = context && buffer;
	ok &= buffer >= context->buffer_pool && buffer < context->buffer_pool + MAX_BUFFERS; // valid pool member

	if (ok) {
		if (buffer->memory) {
			if (buffer->mapped)
				vkUnmapMemory(context->device.logical, buffer->memory);
			vkFreeMemory(context->device.logical, buffer->memory, 0);
		}
		if (buffer->handle)
			vkDestroyBuffer(context->device.logical, buffer->handle, 0);
	}

	ok &= buffer->next == 0 && context->first_free_buffer != buffer; // not free currently
	if (ok) {
		context->buffer_count--;
		buffer->next = context->first_free_buffer;
		context->first_free_buffer = buffer;

		memory_zero(buffer, sizeof(*buffer));
	}

	return ok;
}

bool gfx_image_destroy(GFX_Context *context, GFX_Image *image) {
	bool ok = context && image;
	ok &= image >= context->image_pool && image < context->image_pool + MAX_IMAGES; // valid pool member

	if (ok) {
		if (image->view)
			vkDestroyImageView(context->device.logical, image->view, 0);
		if (image->memory)
			vkFreeMemory(context->device.logical, image->memory, 0);
		if (image->handle)
			vkDestroyImage(context->device.logical, image->handle, 0);
	}

	ok &= image->next == 0 && context->first_free_image != image; // not free currently
	if (ok) {
		context->image_count--;
		image->next = context->first_free_image;
		context->first_free_image = image;

		memory_zero(image, sizeof(*image));
	}

	return ok;
}

bool gfx_sampler_destroy(GFX_Context *context, GFX_Sampler *sampler) {
	bool ok = context && sampler;
	ok &= sampler >= context->sampler_pool && sampler < context->sampler_pool + MAX_SAMPLERS; // valid pool member

	if (ok) {
		if (sampler->handle)
			vkDestroySampler(context->device.logical, sampler->handle, NULL);
	}

	ok &= sampler->next == 0 && context->first_free_sampler != sampler; // not free currently
	if (ok) {
		context->sampler_count--;
		sampler->next = context->first_free_sampler;
		context->first_free_sampler = sampler;

		memory_zero(sampler, sizeof(*sampler));
	}

	return ok;
}

bool gfx_swapchain_destroy(GFX_Context *context, GFX_Swapchain *swapchain) {
	bool ok = context && swapchain;
	ok &= swapchain >= context->swapchain_pool && swapchain < context->swapchain_pool + MAX_SWAPCHAINS; // valid pool member

	if (ok) {
		for (uint32_t semaphore_index = 0; semaphore_index < countof(swapchain->image_available_semaphores); ++semaphore_index)
			if (swapchain->image_available_semaphores[semaphore_index])
				vkDestroySemaphore(context->device.logical, swapchain->image_available_semaphores[semaphore_index], NULL);
		for (uint32_t semaphore_index = 0; semaphore_index < countof(swapchain->render_done_semaphores); ++semaphore_index)
			if (swapchain->render_done_semaphores[semaphore_index])
				vkDestroySemaphore(context->device.logical, swapchain->render_done_semaphores[semaphore_index], NULL);

		for (uint32_t image_index = 0; image_index < swapchain->image_count; ++image_index)
			if (swapchain->views[image_index])
				vkDestroyImageView(context->device.logical, swapchain->views[image_index], NULL);

		if (swapchain->handle)
			vkDestroySwapchainKHR(context->device.logical, swapchain->handle, NULL);
		if (swapchain->surface)
			vkDestroySurfaceKHR(context->instance, swapchain->surface, NULL);
	}

	ok &= swapchain->next == 0 && context->first_free_swapchain != swapchain; // not free currently
	if (ok) {
		context->swapchain_count--;
		swapchain->next = context->first_free_swapchain;
		context->first_free_swapchain = swapchain;

		memory_zero(swapchain, sizeof(*swapchain));
	}

	return ok;
}

void pipeline_destroy(GFX_Context *context, GFX_Pipeline *pipeline) {
	bool ok = context && pipeline;

	if (ok) {
		for (uint32_t index = 0; index < countof(pipeline->shaders); ++index)
			if (pipeline->shaders[index])
				vkDestroyShaderModule(context->device.logical, pipeline->shaders[index], NULL);
		for (uint32_t set_index = 0; set_index < countof(pipeline->set_layouts); ++set_index)
			if (pipeline->set_layouts[set_index])
				vkDestroyDescriptorSetLayout(context->device.logical, pipeline->set_layouts[set_index], NULL);

		if (pipeline->layout)
			vkDestroyPipelineLayout(context->device.logical, pipeline->layout, NULL);
		if (pipeline->handle)
			vkDestroyPipeline(context->device.logical, pipeline->handle, NULL);

		memory_zero(pipeline, sizeof(*pipeline));
	}
}

GFX_Image *gfx_backbuffer(GFX_Context *context, GFX_CommandContext *cmd, GFX_Swapchain *swapchain) {
	GFX_Image *result = 0;
	bool ok = true;

	uint32_t swapchain_index = cmd->swapchain_count;
	uint32_t image_index = -1;
	if (ok) { // acquire swapchain image
		VkResult result = vkAcquireNextImageKHR(
			context->device.logical,
			swapchain->handle,
			UINT64_MAX,
			swapchain->image_available_semaphores[context->current_frame_index],
			VK_NULL_HANDLE, &cmd->swapchain_image_indices[swapchain_index]);
		image_index = cmd->swapchain_image_indices[swapchain_index];

		ok = (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) && image_index != (uint32_t)-1;
	}

	if (ok) { // wrap swapchain
		cmd->swapchain_count++;
		cmd->swapchains[swapchain_index] = swapchain;

		result = &swapchain->wrapper;

		result->handle = swapchain->images[image_index];
		result->view = swapchain->views[image_index];
		result->width = swapchain->info.imageExtent.width;
		result->res_usage = RESOURCE_USAGE_UNDEFINED;
		result->height = swapchain->info.imageExtent.height;
	}

	if (ok == false)
		result = 0;

	return result;
}

bool gfx_swapchain_resize(GFX_Context *context, GFX_Swapchain *swapchain, uint32_t new_width, uint32_t new_height) {
	bool ok = context && swapchain;

	if (ok) {
		vkDeviceWaitIdle(context->device.logical); // TODO: Queue free instead
		OS_Surface *native = swapchain->native;
		gfx_swapchain_destroy(context, swapchain);
		gfx_swapchain_make(context, native, 0);
	}

	return ok;
}

bool gfx_image_resize(GFX_Context *context, GFX_Image *image, uint32_t new_width, uint32_t new_height) {
	bool ok = context && image;

	ImageOptions options = image->options;
	if (ok) {
		ok = gfx_image_destroy(context, image);
		if (ok == false)
			LOG_ERROR("failed to destroy image for resizing.");
	}

	if (ok) {
		ok = gfx_image_make(context, new_width, new_height, options);
		if (ok == false)
			LOG_ERROR("failed to recreate image for resizing.");
	}

	return ok;
}

static inline void gfx__load_debug_extensions(GFX_Context *device);
static inline VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
	void *pUserData);

bool gfx__validate_extensions(const char *required[], uint32_t required_count, VkExtensionProperties *available, uint32_t available_count) {
	bool result = true;

	for (uint32_t required_index = 0; required_index < required_count; ++required_index) {
		bool found = false;
		for (uint32_t available_index = 0; available_index < available_count; ++available_index) {
			if (strcmp(available[available_index].extensionName, required[required_index]) == 0) {
				found = true;
				break;
			}
		}
		if (found == false) {
			LOG_ERROR("required extension '%s' not found, aborting", required[required_index]);
			result = false;
			break;
		}
	}

	return result;
}

VkDebugUtilsMessengerCreateInfoEXT debug_utils_create_info = {
	.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
	.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
	.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
	.pfnUserCallback = vulkan_debug_callback,
	.pUserData = 0
};

bool gfx__vk_instance_make(GFX_Context *context);
bool gfx__vk_device_make(GFX_Context *context);
bool gfx__frame_resources_make(GFX_Context *context);

bool gfx_startup(GFX_Context *context) {
	bool ok = true;

	if (ok)
		ok = gfx__vk_instance_make(context);

	if (ok)
		ok = gfx__vk_device_make(context);

	if (ok)
		ok = gfx__frame_resources_make(context);

	if (ok)
		context->global_range = (VkPushConstantRange){
			.stageFlags = VK_SHADER_STAGE_ALL,
			.offset = 0,
			.size = 128
		};

	if (ok) {
		context->arena[0] = arena_make(MiB(32));
		context->buffer_pool = arena_push_count(context->arena, GFX_Buffer, MAX_BUFFERS);
		context->image_pool = arena_push_count(context->arena, GFX_Image, MAX_IMAGES);
		context->sampler_pool = arena_push_count(context->arena, GFX_Sampler, MAX_SAMPLERS);
		context->shader_pool = arena_push_count(context->arena, GFX_Pipeline, MAX_SHADERS);
		context->swapchain_pool = arena_push_count(context->arena, GFX_Swapchain, MAX_SWAPCHAINS);
	}

	if (ok) {
		context->frame_staging_buffer_slice_size = MiB(64);
		context->frame_staging_buffer =
			gfx_buffer_make(
				context,
				context->frame_staging_buffer_slice_size * MAX_FRAMES_IN_FLIGHT,
				(BufferOptions){
				  .debug_name = "frame_staging_buffer",
				  .memory = MEMORY_TYPE_CPU,
				  .usage = BUFFER_USAGE_TRANSFER | BUFFER_USAGE_UNIFORM | BUFFER_USAGE_STORAGE,
				});

		ok = context->frame_staging_buffer;

		context->transfer_staging_buffer_slice_size = MiB(256);
		context->transfer_staging_buffer =
			gfx_buffer_make(
				context,
				context->transfer_staging_buffer_slice_size * MAX_TRANSFERS_IN_FLIGHT,
				(BufferOptions){
				  .debug_name = "transfer_staging_buffer",
				  .memory = MEMORY_TYPE_CPU,
				  .usage = BUFFER_USAGE_TRANSFER | BUFFER_USAGE_UNIFORM | BUFFER_USAGE_STORAGE,
				});
		ok = context->transfer_staging_buffer;

		if (ok == false) {
			LOG_ERROR("failed to create context staging buffers.");
		}
	}
	if (ok) {
		vkMapMemory(context->device.logical, context->frame_staging_buffer->memory, 0, context->frame_staging_buffer->size, 0, (void **)&context->frame_staging_buffer->mapped);
		vkMapMemory(context->device.logical, context->transfer_staging_buffer->memory, 0, context->transfer_staging_buffer->size, 0, (void **)&context->transfer_staging_buffer->mapped);
	}

	if (ok) {
		context->initialized = true;
	} else {
		LOG_WARN("failed to initialize vulkan context.");
	}

	return ok;
}

void gfx_shutdown(GFX_Context *context) {
	for (uint32_t index = 0; index < MAX_BUFFERS; ++index) {
		GFX_Buffer *buffer = &context->buffer_pool[index];
		gfx_buffer_destroy(context, buffer);

		if (context->buffer_count == 0)
			break;
	}

	for (uint32_t index = 0; index < MAX_IMAGES; ++index) {
		GFX_Image *image = &context->image_pool[index];
		gfx_image_destroy(context, image);

		if (context->image_count == 0)
			break;
	}

	for (uint32_t index = 0; index < MAX_SAMPLERS; ++index) {
		GFX_Sampler *sampler = &context->sampler_pool[index];
		gfx_sampler_destroy(context, sampler);

		if (context->sampler_count == 0)
			break;
	}

	for (uint32_t index = 0; index < MAX_SWAPCHAINS; ++index) {
		GFX_Swapchain *swapchain = &context->swapchain_pool[index];
		gfx_swapchain_destroy(context, swapchain);

		if (context->swapchain_count == 0)
			break;
	}

	for (uint32_t index = 0; index < countof(context->frame_commands); ++index) {
		GFX_CommandContext *cmd_buffer = &context->frame_commands[index];

		if (cmd_buffer->descriptor_pool)
			vkDestroyDescriptorPool(context->device.logical, cmd_buffer->descriptor_pool, 0);
		if (cmd_buffer->in_flight_fence)
			vkDestroyFence(context->device.logical, cmd_buffer->in_flight_fence, 0);
	}

	for (uint32_t index = 0; index < countof(context->transfer_commands); ++index) {
		GFX_CommandContext *cmd_buffer = &context->transfer_commands[index];

		if (cmd_buffer->in_flight_fence)
			vkDestroyFence(context->device.logical, cmd_buffer->in_flight_fence, 0);
	}

	if (context->graphics_command_pool)
		vkDestroyCommandPool(context->device.logical, context->graphics_command_pool, 0);

	/* if (context->transfer_command_pool) */
	/* 	vkDestroyCommandPool(context->device.logical, context->transfer_command_pool, 0); */

#ifdef DEV_BUILD
	if (context->debug_messenger)
		vkDestroyDebugUtilsMessenger(context->instance, context->debug_messenger, 0);
#endif
	if (context->device.logical)
		vkDestroyDevice(context->device.logical, 0);
	if (context->instance)
		vkDestroyInstance(context->instance, 0);

	memory_zero(context, sizeof(GFX_Context));
}

VkPipelineStageFlags gfx__usage_to_pipeline_stage(ResourceUsage usage) {
	switch (usage) {
		case RESOURCE_USAGE_UNDEFINED:
			return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

		case RESOURCE_USAGE_TRANSFER_SRC:
		case RESOURCE_USAGE_TRANSFER_DST:
			return VK_PIPELINE_STAGE_TRANSFER_BIT;

		case RESOURCE_USAGE_SHADER_READ:
		case RESOURCE_USAGE_SHADER_WRITE:
			return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

		case RESOURCE_USAGE_VERTEX_BUFFER:
		case RESOURCE_USAGE_INDEX_BUFFER:
			return VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;

		case RESOURCE_USAGE_VERTEX_SHADER_READ:
		case RESOURCE_USAGE_VERTEX_SHADER_WRITE:
			return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;

		case RESOURCE_USAGE_FRAGMENT_SHADER_READ:
		case RESOURCE_USAGE_FRAGMENT_SHADER_WRITE:
			return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

		case RESOURCE_USAGE_COMPUTE_SHADER_READ:
		case RESOURCE_USAGE_COMPUTE_SHADER_WRITE:
			return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

		case RESOURCE_USAGE_COLOR_ATTACHMENT:
			return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		case RESOURCE_USAGE_DEPTH_ATTACHMENT:
			return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

		case RESOURCE_USAGE_PRESENT:
			return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		case RESOURCE_USAGE_COUNT:
			break;
	}

	ASSERT_FORMAT(false, "unhandled/invalid ResourceUsage [%u] in %s.", usage, __func__);
	return 0;
}

VkAccessFlags gfx__usage_to_access(ResourceUsage usage) {
	switch (usage) {
		case RESOURCE_USAGE_UNDEFINED:
			return 0;

		case RESOURCE_USAGE_TRANSFER_SRC:
			return VK_ACCESS_TRANSFER_READ_BIT;
		case RESOURCE_USAGE_TRANSFER_DST:
			return VK_ACCESS_TRANSFER_WRITE_BIT;

		case RESOURCE_USAGE_SHADER_READ:
		case RESOURCE_USAGE_VERTEX_SHADER_READ:
		case RESOURCE_USAGE_FRAGMENT_SHADER_READ:
		case RESOURCE_USAGE_COMPUTE_SHADER_READ:
			return VK_ACCESS_SHADER_READ_BIT;

		case RESOURCE_USAGE_SHADER_WRITE:
		case RESOURCE_USAGE_VERTEX_SHADER_WRITE:
		case RESOURCE_USAGE_FRAGMENT_SHADER_WRITE:
		case RESOURCE_USAGE_COMPUTE_SHADER_WRITE:
			return VK_ACCESS_SHADER_WRITE_BIT;

		case RESOURCE_USAGE_VERTEX_BUFFER:
			return VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
		case RESOURCE_USAGE_INDEX_BUFFER:
			return VK_ACCESS_INDEX_READ_BIT;

		case RESOURCE_USAGE_COLOR_ATTACHMENT:
			return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
			return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		case RESOURCE_USAGE_DEPTH_ATTACHMENT:
			return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		case RESOURCE_USAGE_PRESENT:
			return 0;
		case RESOURCE_USAGE_COUNT:
			break;
	}

	ASSERT_FORMAT(false, "unhandled/invalid ResourceUsage [%u] in %s.", usage, __func__);
	return 0;
}

GFX_CommandContext *gfx_frame_begin(GFX_Context *context) {
	GFX_CommandContext *result = 0;

	bool ok = context;

	if (ok) {
		result = &context->frame_commands[context->current_frame_index];
		result->frame_index = context->current_frame_index;
		memory_zero_array(result->swapchains);
		memory_zero_array(result->swapchain_image_indices);
		result->swapchain_count = 0;

		// Wait for frame resource availability
		ok = vkWaitForFences(context->device.logical, 1, &result->in_flight_fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
	}

	if (ok) {
		// reset frame resources
		vkResetCommandBuffer(result->handle, 0);
		vkResetFences(context->device.logical, 1, &result->in_flight_fence);
		vkResetDescriptorPool(context->device.logical, result->descriptor_pool, 0);

		result->transient_buffer = context->frame_staging_buffer;
		result->transient_arena[0] = arena_wrap(context->frame_staging_buffer->mapped, context->frame_staging_buffer->size);
		result->transient_arena->offset = context->current_frame_index * context->frame_staging_buffer_slice_size;

		// Begin command recording
		VkCommandBufferBeginInfo cb_begin_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		};

		ok = vkBeginCommandBuffer(result->handle, &cb_begin_info) == VK_SUCCESS;

		if (ok == false) {
			LOG_WARN("failed to begin command buffer recording.");
		}
	}

	if (ok) // submit transfer batch
		gfx_transfer_flush(context);

	if (ok == false)
		result = 0;

	return result;
}

GFX_CommandContext *gfx_transfer_cmd(GFX_Context *context) {
	GFX_CommandContext *result = 0;

	bool ok = context;
	if (ok) {
		result = &context->transfer_commands[context->current_transfer_index];

		if (result->recording == 0)
			ok = vkWaitForFences(context->device.logical, 1, &result->in_flight_fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
	}

	if (ok && result->recording == 0) {
		// reset resources
		vkResetCommandBuffer(result->handle, 0);
		vkResetFences(context->device.logical, 1, &result->in_flight_fence);

		result->transient_buffer = context->transfer_staging_buffer;
		result->transient_arena[0] = arena_wrap(context->transfer_staging_buffer->mapped, context->transfer_staging_buffer->size);
		result->transient_arena->offset = context->current_transfer_index * context->transfer_staging_buffer_slice_size;

		// Begin command recording
		VkCommandBufferBeginInfo cb_begin_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		};
		ok = vkBeginCommandBuffer(result->handle, &cb_begin_info) == VK_SUCCESS;

		if (ok == false) {
			LOG_WARN("failed to begin command buffer recording.");
		}
	}

	if (ok)
		result->recording = 1;

	if (ok == false)
		result = 0;

	return result;
}

bool gfx_transfer_flush(GFX_Context *context) {
	bool ok = context;

	GFX_CommandContext *cmd = 0;
	if (ok) { // check if recording
		cmd = &context->transfer_commands[context->current_transfer_index];

		ok = cmd->recording;
	}

	if (ok) { // end recording
		ok = vkEndCommandBuffer(cmd->handle) == VK_SUCCESS;
		if (ok == false) {
			LOG_INFO("failed to record transfer command buffer.");
		}

		cmd->recording = 0;
	}

	if (ok) { // submit to queue
		VkSubmitInfo submit_info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &cmd->handle,
		};

		ok = vkQueueSubmit(context->graphics_queue, 1, &submit_info, cmd->in_flight_fence) == VK_SUCCESS;
		if (ok == false)
			LOG_ERROR("failed to submit transfer command buffer to queue.");

		context->current_transfer_index = (context->current_transfer_index + 1) % MAX_TRANSFERS_IN_FLIGHT;
	}

	return ok;
}

GFX_CommandContext gfx_transfer_batch_begin(GFX_Context *context) {
	GFX_CommandContext result = { 0 };
	VkCommandBuffer cmd = 0;

	bool ok = context;
	if (ok) {
		VkCommandBufferAllocateInfo alloc_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = context->graphics_command_pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};
		vkAllocateCommandBuffers(context->device.logical, &alloc_info, &cmd);

		VkCommandBufferBeginInfo begin_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};
		ok = vkBeginCommandBuffer(cmd, &begin_info) == VK_SUCCESS;
	}

	if (ok) {
		result.handle = cmd;
		result.transient_arena[0] = arena_wrap(
			context->frame_staging_buffer->mapped + context->current_frame_index * context->frame_staging_buffer_slice_size,
			context->frame_staging_buffer_slice_size); // TODO: staging buffer pool
		result.transient_buffer = context->frame_staging_buffer;
	}

	return result;
}

bool gfx_transfer_batch_submit(GFX_Context *context, GFX_CommandContext *batch) {
	bool ok = context && batch;

	if (ok) {
		vkEndCommandBuffer(batch->handle);

		VkSubmitInfo submit_info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.pCommandBuffers = &batch->handle,
			.commandBufferCount = 1,
		};

		ok = vkQueueSubmit(context->graphics_queue, 1, &submit_info, NULL) == VK_SUCCESS;
		if (ok == false) {
			LOG_WARN("failed to submit transfer commands.");
		}
		vkQueueWaitIdle(context->graphics_queue); // TODO: Handle staging buffer chunking
	}

	return ok;
}

VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
	VkDebugUtilsMessageTypeFlagsEXT message_type,
	const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
	void *pUserData) {
	switch (message_severity) {
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: {
			LOG_TRACE("%s", callback_data->pMessage);
			return VK_FALSE;
		} break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: {
			LOG_INFO("%s", callback_data->pMessage);
			return VK_FALSE;
		} break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: {
			LOG_WARN("%s", callback_data->pMessage);
			return VK_FALSE;
		} break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: {
			LOG_ERROR("%s", callback_data->pMessage);
			return VK_FALSE;
		} break;
		default: {
			return VK_FALSE;
		} break;
	}
}

void gfx__load_debug_extensions(GFX_Context *device) {
#define LOAD_EXTENSION(instance, name)                                    \
	name = (PFN_##name##EXT)vkGetInstanceProcAddr(instance, #name "EXT"); \
	if (!name) {                                                          \
		LOG_ERROR("Failed to load extension: " #name "EXT");              \
		name = STUB_##name;                                               \
	}
	LOAD_EXTENSION(device->instance, vkCreateDebugUtilsMessenger);
	LOAD_EXTENSION(device->instance, vkDestroyDebugUtilsMessenger);
	LOAD_EXTENSION(device->instance, vkSetDebugUtilsObjectName);
	LOAD_EXTENSION(device->instance, vkCmdBeginDebugUtilsLabel);
	LOAD_EXTENSION(device->instance, vkCmdEndDebugUtilsLabel);

#undef LOAD_EXTENSION
}

bool gfx__vk_instance_make(GFX_Context *context) {
	LOG_DEBUG("initializing vulkan instance.");
	ArenaTemp scratch = arena_scratch_begin(0);

	uint32_t required_extension_count = 0;
	const char **required_extensions = os_surface_vulkan_extensions(&required_extension_count);

	bool ok = true;
	if (ok) { // validate if required extensions are present
#ifdef DEV_BUILD
		const char **debug_extensions = arena_push_count(scratch.arena, const char *, required_extension_count + 1);
		memory_copy(debug_extensions, required_extensions, sizeof(*required_extensions) * required_extension_count);
		debug_extensions[required_extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
		required_extensions = debug_extensions;
#endif

		uint32_t available_extension_count = 0;
		vkEnumerateInstanceExtensionProperties(0, &available_extension_count, 0);

		VkExtensionProperties *available_extensions = arena_push_count(scratch.arena, VkExtensionProperties, available_extension_count);
		vkEnumerateInstanceExtensionProperties(0, &available_extension_count, available_extensions);
		ok = gfx__validate_extensions(required_extensions, required_extension_count, available_extensions, available_extension_count);
	}
	static const char *requested_layers[] = {
#ifdef DEV_BUILD
		"VK_LAYER_KHRONOS_validation",
#endif
	};
	uint32_t requested_layer_count = countof(requested_layers);

	if (ok) { // validate if required layers are present
		uint32_t available_layer_count;
		vkEnumerateInstanceLayerProperties(&available_layer_count, 0);

		VkLayerProperties *available_layers = arena_push_count(scratch.arena, VkLayerProperties, available_layer_count);
		vkEnumerateInstanceLayerProperties(&available_layer_count, available_layers);

		for (uint32_t request_index = 0; request_index < requested_layer_count; ++request_index) {
			bool found = false;
			for (uint32_t layer_index = 0; layer_index < available_layer_count; ++layer_index) {
				if (strcmp(available_layers[layer_index].layerName, requested_layers[request_index]) == 0) {
					found = true;
					break;
				}
			}

			if (found == false) {
				LOG_ERROR("layer '%s' not found, aborting", requested_layers[request_index]);
				ok = false;
			}
		}
	}

	if (ok) { // create vulkan instance
		VkApplicationInfo app_info = {
			.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
			.pApplicationName = "unnamed",
			.applicationVersion = 1,
			.pEngineName = "unnamed",
			.engineVersion = 1,
			.apiVersion = VK_MAKE_VERSION(1, 3, 0)
		};

		VkInstanceCreateInfo instance_info = {
			.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			.pApplicationInfo = &app_info,
			.enabledExtensionCount = required_extension_count,
			.ppEnabledExtensionNames = required_extensions,
			.ppEnabledLayerNames = requested_layers,
			.enabledLayerCount = requested_layer_count,

#ifdef DEV_BUILD
			.pNext = &debug_utils_create_info,
#endif
		};

		ok = vkCreateInstance(&instance_info, 0, &context->instance) == VK_SUCCESS;
	}

	if (ok) { // load debug extension pointers & craete debug util
#ifdef DEV_BUILD
		gfx__load_debug_extensions(context);
		vkCreateDebugUtilsMessenger(context->instance, &debug_utils_create_info, 0, &context->debug_messenger);
#endif
	}

	arena_scratch_end(scratch);
	return ok;
}

const char *required_device_extensions[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

bool gfx__device_suitable(GFX_Context *context, VkPhysicalDevice device) {
	ArenaTemp scratch = arena_scratch_begin(0);

	bool ok = true;
	if (ok) { // check if vulkan 1.3 is supported
		vkGetPhysicalDeviceProperties(device, &context->device.properties);
		context->device.limits = context->device.properties.limits;

		ok = context->device.properties.apiVersion >= VK_API_VERSION_1_3;
	}

	if (ok) {
	}

	/* if (ok) { // check for present support  */
	/* 	uint32_t queue_family_count = 0; */
	/* 	vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, 0); */

	/* 	VkQueueFamilyProperties *queue_family_properties = arena_push_count(scratch.arena, VkQueueFamilyProperties, queue_family_count); */
	/* 	vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_family_properties); */

	/* 	VkBool32 supports_present = VK_FALSE; */
	/* 	for (uint32_t queue_family_index = 0; queue_family_index < queue_family_count; ++queue_family_index) { */
	/* 		vkGetPhysicalDeviceSurfaceSupportKHR(device, queue_family_index, context->surface.handle, &supports_present); */

	/* 		if ((queue_family_properties[queue_family_index].queueFlags & VK_QUEUE_GRAPHICS_BIT) && supports_present) */
	/* 			break; */
	/* 	} */

	/* 	if (supports_present == false) */
	/* 		ok = false; */
	/* } */

	if (ok) { // validate if required device extensions are present
		uint32_t available_extension_count = 0;
		vkEnumerateDeviceExtensionProperties(device, 0, &available_extension_count, 0);

		VkExtensionProperties *available_extensions = arena_push_count(scratch.arena, VkExtensionProperties, available_extension_count);
		vkEnumerateDeviceExtensionProperties(device, 0, &available_extension_count, available_extensions);

		ok = gfx__validate_extensions(required_device_extensions, countof(required_device_extensions), available_extensions, available_extension_count);
	}

	if (ok) { // validate if desired features are present
		VkPhysicalDeviceVulkan13Features vk13_features = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		};
		VkPhysicalDeviceVulkan12Features vk12_features = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
			.pNext = &vk13_features,
		};
		VkPhysicalDeviceFeatures2 query_features = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
			.pNext = &vk12_features,
		};
		vkGetPhysicalDeviceFeatures2(device, &query_features);

		if (vk13_features.dynamicRendering == false || vk12_features.bufferDeviceAddress == false)
			ok = false;
	}

	arena_scratch_end(scratch);
	return ok;
}

bool gfx__vk_device_make(GFX_Context *context) {
	LOG_DEBUG("initializing vulkan device.");
	ArenaTemp scratch = arena_scratch_begin(0);

	bool ok = true;
	if (ok) { // select physical device

		uint32_t physical_device_count = 0;
		vkEnumeratePhysicalDevices(context->instance, &physical_device_count, 0);

		VkPhysicalDevice *physical_devices = arena_push_count(scratch.arena, VkPhysicalDevice, physical_device_count);
		vkEnumeratePhysicalDevices(context->instance, &physical_device_count, physical_devices);

		for (uint32_t physical_device_index = 0; physical_device_index < physical_device_count; ++physical_device_index) {
			if (gfx__device_suitable(context, physical_devices[physical_device_index])) {
				context->device.physical = physical_devices[physical_device_index];
				break;
			}
		}

		if (context->device.physical == 0) {
			LOG_ERROR("failed to find suitable graphics card with Vulkan 1.3 support.");
			ok = false;
		}
	}

	if (ok) { // find queue indices
		uint32_t queue_family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(context->device.physical, &queue_family_count, 0);

		VkQueueFamilyProperties *queue_family_properties = arena_push_count(scratch.arena, VkQueueFamilyProperties, queue_family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(context->device.physical, &queue_family_count, queue_family_properties);

		context->graphics_index = -1, context->present_index = -1,
		context->transfer_index = -1, context->compute_index = -1;

		for (uint32_t index = 0; index < queue_family_count; ++index) {
			VkQueueFlags flags = queue_family_properties[index].queueFlags;

			/* VkBool32 present_support = false; */
			/* vkGetPhysicalDeviceSurfaceSupportKHR(context->device.physical, index, context->surface.handle, &present_support); */

			if ((flags & VK_QUEUE_GRAPHICS_BIT) && context->graphics_index == -1) {
				context->graphics_index = index;
				/* ASSERT(present_support && "grahpics index does not support presenting"); */
			}

			/* if (present_support && context->present_index == -1) */
			/* 	context->present_index = index; */

			if ((flags & VK_QUEUE_GRAPHICS_BIT) == false && (flags & VK_QUEUE_TRANSFER_BIT) && context->transfer_index == -1) // dedicated transfer
				context->transfer_index = index;

			if ((flags & VK_QUEUE_GRAPHICS_BIT) == false && (flags & VK_QUEUE_COMPUTE_BIT) && context->compute_index == -1) // dedicated compute
				context->compute_index = index;
		}

		if (context->graphics_index == -1) {
			LOG_ERROR("failed to find graphics queue.");
			ok = false;
		}
	}

	if (ok) { // default to grahpics if compute/transfer queues aren't available
		if (context->transfer_index == -1)
			context->transfer_index = context->graphics_index;
		if (context->compute_index == -1)
			context->compute_index = context->graphics_index;
	}

	if (ok) { // create vulkan device
		float queue_priortiy = 0.5f;

		VkDeviceQueueCreateInfo queue_infos[] = {
			{
			  // grahpics queue
			  .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			  .queueFamilyIndex = context->graphics_index,
			  .queueCount = 1,
			  .pQueuePriorities = &queue_priortiy,
			},
			{
			  // transfer queue
			  .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			  .queueFamilyIndex = context->transfer_index,
			  .queueCount = 1,
			  .pQueuePriorities = &queue_priortiy,
			},
			{
			  // compute queue
			  .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			  .queueFamilyIndex = context->compute_index,
			  .queueCount = 1,
			  .pQueuePriorities = &queue_priortiy,
			}
		};

		VkPhysicalDeviceVulkan13Features vk13_features = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
			.dynamicRendering = VK_TRUE,
		};
		VkPhysicalDeviceVulkan12Features vk12_features = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
			.pNext = &vk13_features,
			.runtimeDescriptorArray = VK_TRUE,
			.descriptorBindingPartiallyBound = VK_TRUE,
			.shaderSampledImageArrayNonUniformIndexing = VK_TRUE, // NOTE: Works perfectly fine without
			.descriptorIndexing = VK_TRUE,
			.bufferDeviceAddress = VK_TRUE,
		};
		VkPhysicalDeviceFeatures2 enable_features = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
			.pNext = &vk12_features,
			.features = { .samplerAnisotropy = true, .fillModeNonSolid = true },
		};

		VkDeviceCreateInfo device_info = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.pNext = &enable_features,
			.queueCreateInfoCount = countof(queue_infos),
			.pQueueCreateInfos = queue_infos,
			.enabledExtensionCount = countof(required_device_extensions),
			.ppEnabledExtensionNames = required_device_extensions,
		};

		ok = vkCreateDevice(context->device.physical, &device_info, 0, &context->device.logical) == VK_SUCCESS;
	}

	if (ok) { // get the queue handles from indices
		vkGetDeviceQueue(context->device.logical, context->graphics_index, 0, &context->graphics_queue);
		vkGetDeviceQueue(context->device.logical, context->transfer_index, 0, &context->transfer_queue);
		vkGetDeviceQueue(context->device.logical, context->compute_index, 0, &context->compute_queue);
	}

	arena_scratch_end(scratch);
	return ok;
}

bool gfx__frame_resources_make(GFX_Context *context) {
	LOG_DEBUG("initializing vulkan frame resources.");

	bool ok = true;
	if (ok) { // make graphics command pool
		VkCommandPoolCreateInfo cp_create_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = context->graphics_index
		};
		ok = vkCreateCommandPool(context->device.logical, &cp_create_info, 0, &context->graphics_command_pool) == VK_SUCCESS;
	}

	if (ok) { // allocate frame command buffers
		VkCommandBuffer buffers[MAX_FRAMES_IN_FLIGHT];
		VkCommandBufferAllocateInfo cb_allocate_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = context->graphics_command_pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = countof(buffers)
		};
		ok = vkAllocateCommandBuffers(context->device.logical, &cb_allocate_info, buffers) == VK_SUCCESS;

		for (uint32_t frame_index = 0; frame_index < countof(buffers); ++frame_index)
			context->frame_commands[frame_index].handle = buffers[frame_index];
	}

	if (ok) { // allocate transfer command buffers
		VkCommandBuffer buffers[MAX_TRANSFERS_IN_FLIGHT];
		VkCommandBufferAllocateInfo cb_allocate_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = context->graphics_command_pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = countof(buffers)
		};
		ok = vkAllocateCommandBuffers(context->device.logical, &cb_allocate_info, buffers) == VK_SUCCESS;

		for (uint32_t transfer_index = 0; transfer_index < countof(buffers); ++transfer_index)
			context->transfer_commands[transfer_index].handle = buffers[transfer_index];
	}

	if (ok) { // allocate fences
		VkFenceCreateInfo f_create_info = {
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT
		};

		for (uint32_t frame_index = 0; frame_index < countof(context->frame_commands); ++frame_index)
			ok &= vkCreateFence(context->device.logical, &f_create_info, 0, &context->frame_commands[frame_index].in_flight_fence) == VK_SUCCESS;
		for (uint32_t transfer_index = 0; transfer_index < countof(context->transfer_commands); ++transfer_index)
			ok &= vkCreateFence(context->device.logical, &f_create_info, 0, &context->transfer_commands[transfer_index].in_flight_fence) == VK_SUCCESS;
	}

	if (ok) { // allocate frame descriptor pools
		VkDescriptorPoolSize sizes[] = {
			{
			  .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			  .descriptorCount = 1000,
			},
			{
			  .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			  .descriptorCount = 1000,
			},
			{
			  .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			  .descriptorCount = 1000,
			},
			{
			  .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			  .descriptorCount = 1000,
			},
			{
			  .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
			  .descriptorCount = 1000,
			},
			{
			  .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			  .descriptorCount = 1000,
			},
		};

		VkDescriptorPoolCreateInfo dp_create_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.poolSizeCount = countof(sizes),
			.pPoolSizes = sizes,
			.maxSets = 1000,
		};

		for (uint32_t frame_index = 0; frame_index < MAX_FRAMES_IN_FLIGHT; ++frame_index)
			ok &= vkCreateDescriptorPool(context->device.logical, &dp_create_info, 0, &context->frame_commands[frame_index].descriptor_pool) == VK_SUCCESS;
	}
	return ok;
}

bool gfx_bind(GFX_Context *context, GFX_Pipeline *pipeline, uint32_t set_index, Uniform *uniforms, uint32_t uniform_count) {
	bool ok = context && pipeline && uniforms && uniform_count > 0;

	GFX_CommandContext *cmd = 0;
	VkDescriptorSet set = 0;

	// TODO: cache descriptor sets instead?
	if (ok) { // allocate descriptor set
		cmd = &context->frame_commands[context->current_frame_index];

		VkDescriptorSetAllocateInfo alloc_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = cmd->descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &pipeline->set_layouts[set_index],
		};
		ok = vkAllocateDescriptorSets(context->device.logical, &alloc_info, &set) == VK_SUCCESS;
	}

	if (ok) { // write uniforms
		ArenaTemp scratch = arena_scratch_begin(0);
		for (uint32_t uniform_index = 0; uniform_index < uniform_count; ++uniform_index) {
			Uniform *uniform = &uniforms[uniform_index];

			UniformSet *pipeline_set = &pipeline->set_infos[set_index];

			int32_t found_index = -1;
			for (uint32_t search_index = 0; search_index < pipeline_set->uniform_count; ++search_index) {
				if (pipeline_set->uniforms[search_index].binding == uniform->binding) {
					found_index = search_index;
					break;
				}
			}

			if (found_index == -1) {
				LOG_ERROR("shader '%s' has no uniform at set = %u, binding = %u.", pipeline->options.debug_name, set_index, uniform->binding);
				ASSERT(false);
			}

			Uniform *reflected_uniform = &pipeline->set_infos[set_index].uniforms[found_index];

			ASSERT_FORMAT(uniform->type == reflected_uniform->type,
				"uniform binding '%s' (set = %u, binding = %u) of shader '%s' supplied as: %s, expected: %s.",
				reflected_uniform->name, set_index, reflected_uniform->binding, pipeline->options.debug_name, uniform_type_to_string[uniform->type], uniform_type_to_string[pipeline_set->uniforms[found_index].type]);

			switch (uniform->type) {
				case UNIFORM_TYPE_STORAGE_IMAGE:
				case UNIFORM_TYPE_SAMPLER:
				case UNIFORM_TYPE_IMAGE:
				case UNIFORM_TYPE_SAMPLER_WITH_IMAGE: {
					VkDescriptorImageInfo *image_infos = arena_push_count(scratch.arena, VkDescriptorImageInfo, uniform->count);

					VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					if (uniform->type == UNIFORM_TYPE_STORAGE_IMAGE)
						layout = VK_IMAGE_LAYOUT_GENERAL;

					GFX_Sampler *sampler = uniform->resource.sampler_with_textures.sampler;
					for (uint32_t image_index = 0; image_index < uniform->count; ++image_index) {
						GFX_Image *image = uniform->resource.sampler_with_textures.images[image_index];

						image_infos[image_index] = (VkDescriptorImageInfo){
							.imageLayout = image ? layout : 0,
							.imageView = image ? image->view : 0,
							.sampler = sampler ? sampler->handle : 0,
						};
					}
					VkWriteDescriptorSet write = {
						.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						.dstSet = set,
						.dstBinding = uniform->binding,
						.dstArrayElement = 0,
						.descriptorCount = uniform->count,
						.descriptorType = uniform_type_to_descriptor_type[uniform->type],
						.pImageInfo = image_infos,
					};
					vkUpdateDescriptorSets(context->device.logical, 1, &write, 0, 0);
				} break;

				case UNIFORM_TYPE_STORAGE_BUFFER:
				case UNIFORM_TYPE_UNIFORM_BUFFER: {
					GFX_Buffer *buffer = 0;
					uint64_t offset = 0;
					uint64_t size = 0;
					if (uniform->resource.buffer.handle) {
						buffer = uniform->resource.buffer.handle;
						offset = uniform->resource.buffer.offset;
						size = uniform->resource.buffer.size;
					} else if (uniform->resource.buffer.data) {
						buffer = cmd->transient_buffer;
						offset = alignup(cmd->transient_arena->offset, 256);
						size = uniform->resource.buffer.size;
						memory_copy(
							arena_push(cmd->transient_arena, alignup(size, 256), 256, 0),
							uniform->resource.buffer.data,
							size);
					} else
						ASSERT(false); // TODO: Handle fallback

					VkDescriptorBufferInfo buffer_info = {
						.buffer = buffer->handle,
						.offset = offset,
						.range = size,
					};
					VkWriteDescriptorSet write = {
						.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						.dstSet = set,
						.dstBinding = uniform->binding,
						.dstArrayElement = 0,
						.descriptorCount = uniform->count,
						.descriptorType = uniform_type_to_descriptor_type[uniform->type],
						.pBufferInfo = &buffer_info,
					};
					vkUpdateDescriptorSets(context->device.logical, 1, &write, 0, 0);
				} break;

				default:
					ASSERT(false);
					break;
			}
		}

		arena_scratch_end(scratch);
	}

	if (ok) {
		VkPipelineBindPoint bind_point = pipeline->shaders[SHADER_STAGE_COMPUTE] ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
		vkCmdBindDescriptorSets(cmd->handle, bind_point, pipeline->layout, set_index, 1, &set, 0, 0);
	}

	return ok;
}

uint64_t gfx_cmd_put(GFX_CommandContext *cmd, uint64_t size, void *src) {
	uint64_t result = alignup(cmd->transient_arena->offset, 256);
	void *dst = arena_push(cmd->transient_arena, alignup(size, 256), 256, 0);
	if (src)
		memory_copy(dst, src, size);

	return result;
}

void gfx_cmd_buffer_to_buffer(GFX_CommandContext *cmd, GFX_Buffer *dst, GFX_Buffer *src, uint64_t dst_offset, uint64_t src_offset, uint64_t size) {
	LOG_TRACE("Copying region of %llu from %p to %p", size, src, dst);
	VkBufferCopy copy_region = { .srcOffset = src_offset, .dstOffset = dst_offset, .size = size };
	vkCmdCopyBuffer(cmd->handle, src->handle, dst->handle, 1, &copy_region);
}

void gfx_cmd_buffer_to_image(GFX_CommandContext *cmd, GFX_Image *dst, GFX_Buffer *src, uint64_t src_offset, uint32_t width, uint32_t height) {
	ArenaTemp scratch = arena_scratch_begin(NULL);
	bool ok = cmd && cmd->handle && dst && dst->handle;
	if (ok == false) {
		LOG_WARN("%s - invalid parameter '%s' passed", __func__, cmd == 0 || cmd->handle == 0 ? "GFX_CommandContext" : "GFX_Image");
	}

	if (ok) {
		gfx_cmd_image_transition(cmd, RESOURCE_USAGE_TRANSFER_DST, dst);

		uint32_t layer_count = gfx__image_options_to_layer_count(dst->options);
		VkBufferImageCopy *regions = arena_push_count(scratch.arena, VkBufferImageCopy, layer_count);

		for (uint32_t layer_index = 0; layer_index < layer_count; ++layer_index) {
			regions[layer_index] = (VkBufferImageCopy){
				.bufferOffset = src_offset + (layer_index * width * height * pixel_format_to_stride[dst->options.format]),
				.bufferRowLength = 0,
				.bufferImageHeight = 0,
				.imageSubresource = {
				  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				  .mipLevel = 0,
				  .baseArrayLayer = layer_index,
				  .layerCount = 1,
				},
				.imageOffset = { 0 },
				.imageExtent = { .width = width, .height = height, .depth = 1 },
			};
		}
		vkCmdCopyBufferToImage(cmd->handle, src->handle, dst->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, layer_count, regions);
	}

	arena_scratch_end(scratch);
}

void gfx_cmd_buffer_barrier(GFX_CommandContext *cmd, ResourceUsage src, ResourceUsage dst, uint64_t offset, uint64_t size, GFX_Buffer *target) {
	bool ok = cmd && target;

	if (ok) {
		VkPipelineStageFlags src_stage = gfx__usage_to_pipeline_stage(src);
		VkPipelineStageFlags dst_stage = gfx__usage_to_pipeline_stage(dst);
		VkAccessFlags src_access = gfx__usage_to_access(src);
		VkAccessFlags dst_access = gfx__usage_to_access(dst);

		VkBufferMemoryBarrier buffer_barrier = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.buffer = target->handle,
			.srcAccessMask = src_access,
			.dstAccessMask = dst_access,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.offset = offset,
			.size = size,
		};
		vkCmdPipelineBarrier(cmd->handle, src_stage, dst_stage, 0, 0, 0, 1, &buffer_barrier, 0, 0);
	}
}

bool gfx_cmd_image_barrier(GFX_CommandContext *cmd, ResourceUsage src, ResourceUsage dst, uint32_t base_miplevel, uint32_t level_count, GFX_Image *target) {
	bool ok = cmd && target;

	if (ok) {
		target->res_usage = dst;

		ok = dst;
	}

	if (ok) {
		VkPipelineStageFlags src_stage = gfx__usage_to_pipeline_stage(src);
		VkPipelineStageFlags dst_stage = gfx__usage_to_pipeline_stage(dst);
		VkAccessFlags src_access = gfx__usage_to_access(src);
		VkAccessFlags dst_access = gfx__usage_to_access(dst);
		VkImageLayout src_layout = resource_usage_to_image_layout[src];
		VkImageLayout dst_layout = resource_usage_to_image_layout[dst];

		VkImageMemoryBarrier image_barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = src_access,
			.dstAccessMask = dst_access,
			.oldLayout = src_layout,
			.newLayout = dst_layout,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = target->handle,
			.subresourceRange = {
			  .aspectMask = gfx__image_options_to_aspect(target->options),
			  .baseArrayLayer = 0,
			  .baseMipLevel = base_miplevel,
			  .levelCount = level_count,
			  .layerCount = gfx__image_options_to_layer_count(target->options),
			},
		};

		vkCmdPipelineBarrier(
			cmd->handle,
			src_stage, dst_stage,
			0,
			0, NULL,
			0, NULL,
			1, &image_barrier);
	}

	return ok;
}

bool gfx_cmd_image_transition(GFX_CommandContext *cmd, ResourceUsage dst, GFX_Image *target) {
	return gfx_cmd_image_barrier(cmd, target->res_usage, dst, 0, target->miplevels, target);
}

void gfx_cmd_image_blit(GFX_CommandContext *cmd, Rectangle source_rect, GFX_Image *source, Rectangle target_rect, GFX_Image *target) {
	VkImageBlit blit_info = {
		.srcOffsets[1] = {
		  .x = source_rect.width,
		  .y = source_rect.height,
		  .z = 1,
		},
		.srcSubresource = {
		  .aspectMask = gfx__image_options_to_aspect(source->options),
		  .baseArrayLayer = 0,
		  .layerCount = gfx__image_options_to_layer_count(source->options),
		  .mipLevel = 0,
		},
		.dstOffsets[1] = {
		  .x = target_rect.width,
		  .y = target_rect.height,
		  .z = 1,
		},
		.dstSubresource = {
		  .aspectMask = gfx__image_options_to_aspect(target->options),
		  .baseArrayLayer = 0,
		  .layerCount = gfx__image_options_to_layer_count(target->options),
		  .mipLevel = 0,
		},
	};
	vkCmdBlitImage(cmd->handle,
		source->handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		target->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &blit_info, 0);
}

void gfx_cmd_image_upload(GFX_CommandContext *cmd, GFX_Image *image, uint32_t width, uint32_t height, void *pixels) {
	uint32_t stride = pixel_format_to_stride[image->options.format];
	uint64_t size = width * height * stride * gfx__image_options_to_layer_count(image->options);
	uint64_t start_offset = gfx_cmd_put(cmd, size, pixels);
	gfx_cmd_buffer_to_image(cmd, image, cmd->transient_buffer, start_offset, width, height);
}

void gfx_cmd_buffer_upload(GFX_CommandContext *cmd, GFX_Buffer *buffer, uint64_t offset, uint64_t size, void *data) {
	uint64_t staging_offset = gfx_cmd_put(cmd, size, data);
	gfx_cmd_buffer_to_buffer(cmd, buffer, cmd->transient_buffer, offset, staging_offset, size);
}

void gfx_cmd_pipeline_bind(GFX_CommandContext *cmd, GFX_Pipeline *pipeline) {
	VkPipelineBindPoint bind_point = pipeline->shaders[SHADER_STAGE_COMPUTE] ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
	vkCmdBindPipeline(cmd->handle, bind_point, pipeline->handle);
}

void gfx_cmd_dispatch(GFX_CommandContext *cmd, uint32_t x, uint32_t y, uint32_t z) {
	vkCmdDispatch(cmd->handle, x, y, z);
}

Image2D load_image(Arena *arena, String8 path) {
	Image2D result = { .format = PIXELFORMAT_RGBA8_UNORM };

	bool ok = arena && path.length;

	uint8_t *pixels = 0;
	int32_t channels = 0;
	if (ok) {
		pixels = stbi_load((char *)path.text, (int32_t *)&result.width, (int32_t *)&result.height, &channels, 4);

		ok = pixels != 0;
		if (ok == false) {
			LOG_WARN("[%s] failed to load", path.text);
			static uint8_t magenta[] = { 255, 0, 255, 255 };
			result.width = result.height = 1;
			result.pixels = magenta;
		}
	}

	if (ok) {
		uint32_t pixel_buffer_size = result.width * result.height * channels;
		result.pixels = arena_push_count(arena, uint8_t, pixel_buffer_size);
		memory_copy(result.pixels, pixels, pixel_buffer_size);
		stbi_image_free(pixels);
	}

	if (ok) {
		String8 filename = str8_filename(path);
		LOG_INFO("'%.*s' loaded sucessfully (%ux%u, %s)", filename.length, filename.text, result.width, result.height, channels == 4 ? "RGBA8" : "RGB8");
	}

	return result;
}

Image2D load_cubemap(Arena *arena, String8 paths[], uint32_t count) {
	Image2D result = { 0 };

	Image2D images[AXIS_PLANE_COUNT];
	for (uint32_t face_index = 0; face_index < AXIS_PLANE_COUNT; ++face_index) {
		images[face_index] = load_image(arena, paths[face_index]);

		if (face_index > 0) {
			ASSERT(images[face_index - 1].width == images[face_index].width && images[face_index - 1].height == images[face_index].height && "all images in cubemap must be equally sized.");
		}
	}

	result = images[0];
	result.type = IMAGE_TYPE_CUBE;
	return result;
}

Image2D load_gltf_image(Arena *arena, String8 directory, cgltf_image *image) {
	ArenaTemp scratch = arena_scratch_begin(arena);
	Image2D result = { 0 };

	bool ok = arena && image;

	if (ok) {
		if (image->uri) {
			String8 image_path = str8_filepath_join(scratch.arena, directory, str8_wrap(image->uri));
			result = load_image(arena, image_path);
		} else if (image->buffer_view) {
			const uint8_t *buffer_data = cgltf_buffer_view_data(image->buffer_view);
			uint32_t channels = 0;
			result.pixels = stbi_load_from_memory(buffer_data, image->buffer_view->size, (int32_t *)&result.width, (int32_t *)&result.height, (int32_t *)&channels, 4);
			result.format = PIXELFORMAT_RGBA8_SRGB;
		}
	}

	arena_scratch_end(scratch);
	return result;
}

Mesh load_gltf(Arena *arena, String8 path) {
	LOG_INFO("loading [%s]", path.text);

	Mesh result = { 0 };
	cgltf_options options = { 0 };
	cgltf_data *data = 0;

	bool ok = cgltf_parse_file(&options, (char *)path.text, &data) == cgltf_result_success;
	if (ok == false)
		LOG_ERROR("%s - failed to open file", path.text);

	if (ok) {
		ok &= cgltf_load_buffers(&options, data, (char *)path.text) == cgltf_result_success;
		ok &= cgltf_validate(data) == cgltf_result_success;
	}

	String8 directory = str8_directory(path);

	if (ok) { // load materials
		result.material_count = data->materials_count + 1;
		result.materials = arena_push_count(arena, Material, result.material_count);
		result.materials[0] = (Material){
			.tint = FLOAT4_ONE,
		};

		for (uint32_t material_index = 0; material_index < data->materials_count; ++material_index) {
			cgltf_material *material = &data->materials[material_index];
			Material *out = &result.materials[material_index + 1];
			out->tint = (float4){ 1.0f, 1.0f, 1.0f, 1.0f };

			if (material->has_pbr_metallic_roughness) {
				cgltf_pbr_metallic_roughness *pbr = &material->pbr_metallic_roughness;

				out->tint = float4_wrap(pbr->base_color_factor);
				out->metallic_roughness = (float2){
					.x = pbr->metallic_factor,
					.y = pbr->roughness_factor,
				};

				if (pbr->base_color_texture.texture) {
					cgltf_image *image = pbr->base_color_texture.texture->image;
					out->textures[TEXTURE_SLOT_ALBEDO] = load_gltf_image(arena, directory, image);
					out->textures[TEXTURE_SLOT_ALBEDO].format = PIXELFORMAT_RGBA8_SRGB;
				}

				if (pbr->metallic_roughness_texture.texture) {
					cgltf_image *image = pbr->metallic_roughness_texture.texture->image;
					out->textures[TEXTURE_SLOT_METAL_ROUGHNESS] = load_gltf_image(arena, directory, image);
					out->textures[TEXTURE_SLOT_METAL_ROUGHNESS].format = PIXELFORMAT_RGBA8_UNORM;
				}
			}

			if (material->normal_texture.texture) {
				cgltf_image *image = material->normal_texture.texture->image;
				out->textures[TEXTURE_SLOT_NORMAL] = load_gltf_image(arena, directory, image);
				out->textures[TEXTURE_SLOT_NORMAL].format = PIXELFORMAT_RGBA8_UNORM;
			}
		}
	}

	if (ok) { // load geometry
		for (uint32_t node_index = 0; node_index < data->nodes_count; ++node_index) {
			cgltf_node *node = &data->nodes[node_index];
			if (node->mesh == 0)
				continue;

			for (uint32_t primitive_index = 0; primitive_index < node->mesh->primitives_count; ++primitive_index) {
				cgltf_primitive *primitive = &node->mesh->primitives[primitive_index];
				if (primitive->type == cgltf_primitive_type_triangles) {
					result.part_count++;
					result.total_vertex_count += primitive->attributes[0].data->count;
					result.total_index_count += primitive->indices->count;
				}
			}
		}

		result.parts = arena_push_count(arena, MeshPart, result.part_count);
		result.vertices = arena_push_count(arena, Vertex3, result.total_vertex_count);
		result.indices = arena_push_count(arena, uint32_t, result.total_index_count);
		result.skinning = data->skins_count > 0 ? arena_push_count(arena, SkinningData, result.total_vertex_count) : 0;
		result.bounds = aabb3_empty();

		uint32_t part_offset = 0;
		uint64_t vertex_offset = 0, index_offset = 0;
		for (uint32_t node_index = 0; node_index < data->nodes_count; ++node_index) {
			cgltf_node *node = &data->nodes[node_index];
			if (node->mesh == 0)
				continue;

			float4x4 transform = float4x4_identity();
			if (node->skin == 0)
				cgltf_node_transform_world(node, transform.elements);
			bool has_transform = float4x4_equal(float4x4_identity(), transform) == false;

			for (uint32_t primitive_index = 0; primitive_index < node->mesh->primitives_count; ++primitive_index) {
				cgltf_primitive *primitive = &node->mesh->primitives[primitive_index];
				MeshPart *part = &result.parts[part_offset++];
				part->bounds = aabb3_empty();

				part->vertex_count = primitive->attributes[0].data->count;
				part->vertex_offset = vertex_offset;

				part->index_count = primitive->indices->count;
				part->index_offset = index_offset;
				cgltf_accessor_unpack_indices(primitive->indices, result.indices + index_offset, 4, part->index_count);

				uint8_t skinned = 0;
				for (uint32_t attribute_index = 0; attribute_index < primitive->attributes_count; ++attribute_index) {
					cgltf_attribute *attribute = &primitive->attributes[attribute_index];
					cgltf_accessor *accessor = attribute->data;
					ASSERT(accessor->count == part->vertex_count && "expect all attributes to have same number of vertices");

					uint32_t offset = 0;
					switch (attribute->type) {
						case cgltf_attribute_type_position:
							offset = offsetof(Vertex3, position);
							if (has_transform == false) {
								part->bounds.min = float3_wrap(accessor->min);
								part->bounds.max = float3_wrap(accessor->max);
							}
							break;
						case cgltf_attribute_type_normal:
							offset = offsetof(Vertex3, normal);
							break;
						case cgltf_attribute_type_tangent:
							offset = offsetof(Vertex3, tangent);
							break;
						case cgltf_attribute_type_texcoord:
							offset = offsetof(Vertex3, uv);
							break;
						case cgltf_attribute_type_weights:
							offset = offsetof(SkinningData, weights);
							skinned++;
							break;
						case cgltf_attribute_type_joints:
							offset = offsetof(SkinningData, bone_ids);
							skinned++;
							break;
						default:
							continue;
					}

					Vertex3 *mesh_vertices = result.vertices + vertex_offset;
					SkinningData *mesh_skinning = result.skinning + vertex_offset;

					for (uint32_t vertex_index = 0; vertex_index < part->vertex_count; ++vertex_index) {
						if (attribute->type == cgltf_attribute_type_weights)
							cgltf_accessor_read_float(accessor, vertex_index, (void *)((uint8_t *)(mesh_skinning + vertex_index) + offset), cgltf_num_components(accessor->type));
						else if (attribute->type == cgltf_attribute_type_joints)
							cgltf_accessor_read_uint(accessor, vertex_index, (void *)((uint8_t *)(mesh_skinning + vertex_index) + offset), cgltf_num_components(accessor->type));
						else {
							void *dst = (void *)((uint8_t *)(mesh_vertices + vertex_index) + offset);
							cgltf_accessor_read_float(accessor, vertex_index, dst, cgltf_num_components(accessor->type));
							if (has_transform == false)
								continue;

							if (attribute->type == cgltf_attribute_type_position) {
								float3 *pos = (float3 *)dst;
								float3 new_pos = float4x4_transform(transform, float4_from_float3(*pos, 1.0f));
								pos->x = new_pos.x;
								pos->y = new_pos.y;
								pos->z = new_pos.z;

								aabb3_expand(&part->bounds, new_pos);
							} else if (attribute->type == cgltf_attribute_type_normal) {
								float3 *norm = (float3 *)dst;
								float3 new_norm = float4x4_transform(transform, float4_from_float3(*norm, 0.0f));
								*norm = float3_normalize(new_norm);
							} else if (attribute->type == cgltf_attribute_type_tangent) {
								float4 *tan = (float4 *)dst;
								float3 new_tan = float4x4_transform(transform, (float4){ tan->x, tan->y, tan->z, 0.0f });
								float3 norm_tan = float3_normalize(new_tan);
								tan->x = norm_tan.x;
								tan->y = norm_tan.y;
								tan->z = norm_tan.z;
							}
						}
					}
				}

				result.bounds.min = float3_min(result.bounds.min, part->bounds.min);
				result.bounds.max = float3_max(result.bounds.max, part->bounds.max);

				if (data->skins_count > 0 && skinned == 0 && node->parent && node->parent->mesh == 0) { // add dummy skinned data
					int32_t parent_bone = -1;
					for (uint32_t joint = 0; joint < data->skins[0].joints_count; joint++) {
						if (data->skins[0].joints[joint] == node->parent) {
							parent_bone = joint;
							break;
						}
					}

					if (parent_bone >= 0) {
						SkinningData *mesh_skinning = result.skinning + vertex_offset;
						for (uint32_t vertex_index = 0; vertex_index < part->vertex_count; ++vertex_index) {
							mesh_skinning[vertex_index].bone_ids = (uint32x4){ parent_bone, 0, 0, 0 };
							mesh_skinning[vertex_index].weights = (float4){ 1.0f, 0.0f, 0.0f, 0.0f };
						}
					}
				}

				if (primitive->material && primitive->material->has_pbr_metallic_roughness) {
					cgltf_material *material = primitive->material;
					cgltf_pbr_metallic_roughness *pbr = &material->pbr_metallic_roughness;

					if (material)
						part->material_id = cgltf_material_index(data, material) + 1;
				}

				vertex_offset += part->vertex_count;
				index_offset += part->index_count;
			}
		}
	}

	/* if (ok) { // load materials */
	/* 	for (uint32_t material_index = 0; material_index < data->materials_count; ++material_index) { */
	/* 		cgltf_material *material = &data->materials[material_index]; */
	/* 		if (material->has_pbr_metallic_roughness == false) { */
	/* 			LOG_WARN("expect material to use metallic roughness."); */
	/* 			continue; */
	/* 		} */

	/* 		cgltf_pbr_metallic_roughness *pbr = &material->pbr_metallic_roughness; */
	/* 	} */
	/* } */

	if (ok) { // load skeleton
		ASSERT(data->skins_count <= 1 && "expect single skinned mesh per file");
		for (uint32_t skin_index = 0; skin_index < data->skins_count; ++skin_index) {
			cgltf_skin *skin = &data->skins[0];
			cgltf_accessor *accessor = skin->inverse_bind_matrices;
			ASSERT(skin->joints_count == accessor->count && "expect joint count to match matrix count");

			result.skeleton.bone_count = skin->joints_count;
			result.skeleton.bones = arena_push_count(arena, Bone, result.skeleton.bone_count);
			result.skeleton.inverse_rest_matrices = arena_push_count(arena, float4x4, result.skeleton.bone_count);
			result.skeleton.bind_pose_matrices = arena_push_count(arena, float4x4, result.skeleton.bone_count);

			cgltf_accessor_unpack_floats(accessor, (float *)result.skeleton.inverse_rest_matrices, accessor->count * cgltf_num_components(accessor->type));

			for (uint32_t joint_index = 0; joint_index < skin->joints_count; ++joint_index) {
				cgltf_node *joint = skin->joints[joint_index];
				cgltf_node_transform_world(joint, result.skeleton.bind_pose_matrices[joint_index].elements);

				memory_copy(
					result.skeleton.bones[joint_index].name,
					joint->name,
					MIN(str8_wrap(joint->name).length, sizeof_member(Bone, name)));

				bool found = false;
				for (uint32_t search_index = 0; search_index < skin->joints_count; ++search_index)
					if (skin->joints[search_index] == joint->parent) {
						result.skeleton.bones[joint_index].parent = search_index;
						found = true;
					}

				if (found == false)
					result.skeleton.bones[joint_index].parent = -1;
			}
		}
	}

	cgltf_free(data);
	return result;
}

static inline float3 face_orient(float3 v, AxisPlane face) {
	float3 result = { 0 };
	switch (face) {
		case AXIS_PLANE_UP:
			result = (float3){ v.x, v.y, v.z };
			break;
		case AXIS_PLANE_DOWN:
			result = (float3){ v.x, -v.y, -v.z };
			break;
		case AXIS_PLANE_RIGHT:
			result = (float3){ v.y, v.z, v.x };
			break;
		case AXIS_PLANE_LEFT:
			result = (float3){ -v.y, v.z, -v.x };
			break;
		case AXIS_PLANE_FORWARD:
			result = (float3){ v.x, v.z, -v.y };
			break;
		case AXIS_PLANE_BACKWARD:
			result = (float3){ -v.x, v.z, v.y };
			break;
		default:
			ASSERT(!"invalid orientation passed.");
			break;
	}

	return result;
}

Mesh generate_plane_from_heightmap(Arena *arena, AxisPlane orientation, float w, float h, Image2D heightmap) {
	Mesh result = { 0 };
	result.bounds.min = (float3){ .x = -w * 0.5f, .y = 0.0f, .y = -h * 0.5f };
	result.bounds.max = (float3){ .x = w * 0.5f, .y = 0.0f, .y = h * 0.5f };

	bool ok = arena;

	if (ok) {
		result.total_vertex_count = heightmap.width * heightmap.height;

		result.vertices = arena_push_count(arena, Vertex3, result.total_vertex_count);
		for (uint32_t z = 0; z < heightmap.height; ++z) {
			for (uint32_t x = 0; x < heightmap.width; ++x) {
				uint32_t index = x + z * heightmap.width;

				float3 local = {
					.x = (((float)x / (heightmap.width - 1)) - 0.5f) * w,
					.y = ((heightmap.pixels[index * 4] / 255.f) - 0.5f) * 40.f,
					.z = (((float)z / (heightmap.height - 1)) - 0.5f) * h,
				};
				result.vertices[index] = (Vertex3){
					.position = face_orient(local, orientation),
					.normal = { 0.0f, 1.0f, 0.0f },
					.uv = { (float)x / (heightmap.width - 1), (float)z / (heightmap.height - 1) },
				};
			}
		}

		uint32_t face_count = (heightmap.width - 1) * (heightmap.height - 1);

		result.total_index_count = face_count * 6;
		result.indices = arena_push_count(arena, uint32_t, result.total_index_count);

		uint32_t cursor = 0;
		for (uint32_t face = 0; face < face_count; ++face) {
			uint32_t index = face + face / (heightmap.width - 1);

			result.indices[cursor++] = index;
			result.indices[cursor++] = index + heightmap.width;
			result.indices[cursor++] = index + 1;

			result.indices[cursor++] = index + 1;
			result.indices[cursor++] = index + heightmap.width;
			result.indices[cursor++] = index + heightmap.width + 1;
		}

		result.part_count = 1;
		result.parts = arena_push_count(arena, MeshPart, result.part_count);

		result.parts[0].vertex_count = result.total_vertex_count;
		result.parts[0].index_count = result.total_index_count;

		result.material_count = 1;
		result.materials = arena_push_count(arena, Material, result.material_count);

		result.materials[0] = (Material){
			.tint = { 0.8f, 0.8f, 0.8f, 1.0f },
			.emissive = FLOAT4_ONE,
			.metallic_roughness = { 0.0f, 0.5f },
		};
	}

	return result;
}

Mesh generate_plane(Arena *arena, AxisPlane orientation, float width, float height, uint32_t subdivision_x, uint32_t subdivision_z) {
	Mesh result = { 0 };
	result.bounds.min = (float3){ .x = -width * 0.5f, .y = 0.0f, .y = -height * 0.5f };
	result.bounds.max = (float3){ .x = width * 0.5f, .y = 0.0f, .y = height * 0.5f };

	bool ok = arena;

	if (ok) {
		subdivision_x += 2;
		subdivision_z += 2;
		result.total_vertex_count = subdivision_x * subdivision_z;

		result.vertices = arena_push_count(arena, Vertex3, result.total_vertex_count);
		for (uint32_t z = 0; z < subdivision_z; ++z) {
			for (uint32_t x = 0; x < subdivision_x; ++x) {
				uint32_t index = x + z * subdivision_x;

				float3 local = {
					.x = (((float)x / (subdivision_x - 1)) - 0.5f) * width,
					.y = 0.0f,
					.z = (((float)z / (subdivision_z - 1)) - 0.5f) * height,
				};
				result.vertices[index] = (Vertex3){
					.position = face_orient(local, orientation),
					.normal = { 0.0f, 1.0f, 0.0f },
					.uv = { (float)x / (subdivision_x - 1), (float)z / (subdivision_z - 1) },
				};
			}
		}

		uint32_t face_count = (subdivision_x - 1) * (subdivision_z - 1);

		result.total_index_count = face_count * 6;
		result.indices = arena_push_count(arena, uint32_t, result.total_index_count);

		uint32_t cursor = 0;
		for (uint32_t face = 0; face < face_count; ++face) {
			uint32_t index = face + face / (subdivision_x - 1);

			result.indices[cursor++] = index;
			result.indices[cursor++] = index + subdivision_x;
			result.indices[cursor++] = index + 1;

			result.indices[cursor++] = index + 1;
			result.indices[cursor++] = index + subdivision_x;
			result.indices[cursor++] = index + subdivision_x + 1;
		}

		result.part_count = 1;
		result.parts = arena_push_count(arena, MeshPart, result.part_count);

		result.parts[0].vertex_count = result.total_vertex_count;
		result.parts[0].index_count = result.total_index_count;

		result.material_count = 1;
		result.materials = arena_push_count(arena, Material, result.material_count);

		result.materials[0] = (Material){
			.tint = { 0.8f, 0.8f, 0.8f, 1.0f },
			.emissive = FLOAT4_ONE,
			.metallic_roughness = { 0.0f, 0.5f },
		};
	}

	return result;
}

AnimationClip *load_gltf_animations(Arena *arena, String8 path, uint32_t *count) {
	LOG_INFO("loading [%s]", path.text);

	AnimationClip *result = 0;
	cgltf_options options = { 0 };
	cgltf_data *data = 0;

	bool ok = cgltf_parse_file(&options, (char *)path.text, &data) == cgltf_result_success;
	if (ok == false)
		LOG_ERROR("%s - failed to open file", path.text);

	if (ok) {
		ok &= cgltf_load_buffers(&options, data, (char *)path.text) == cgltf_result_success;
		ok &= cgltf_validate(data) == cgltf_result_success;
		ok &= data->animations_count > 0;
	}

	if (ok) { // load animations
		result = arena_push_count(arena, AnimationClip, data->animations_count);
		*count = data->animations_count;

		cgltf_skin *skin = &data->skins[0];
		for (uint32_t anim_index = 0; anim_index < data->animations_count; ++anim_index) {
			cgltf_animation *anim = &data->animations[anim_index];
			AnimationClip *out_anim = result + anim_index;

			uint32_t max_keyframe_count = 0, min_keyframe_count = UINT32_MAX;
			for (uint32_t sampler_index = 0; sampler_index < anim->samplers_count; ++sampler_index) {
				cgltf_animation_sampler *sampler = &anim->samplers[sampler_index];

				max_keyframe_count = MAX(sampler->input->count, max_keyframe_count);
				min_keyframe_count = MIN(sampler->input->count, min_keyframe_count);
				out_anim->keyframe_count = MAX(sampler->input->count, out_anim->keyframe_count);

				float t = 0.0f;
				cgltf_accessor_read_float(sampler->input, sampler->input->count - 1, &t, cgltf_component_size(sampler->input->component_type));

				out_anim->duration = MAX(out_anim->duration, t);
			}
			ASSERT(min_keyframe_count == max_keyframe_count && "expect all sampler timings to match");
			out_anim->keyframes = arena_push_count(arena, Transform3 *, out_anim->keyframe_count);
			out_anim->timings = arena_push_count(arena, float, out_anim->keyframe_count);
			out_anim->bone_count = data->skins[0].joints_count;
			memory_copy(out_anim->name, anim->name, MIN(sizeof(out_anim->name) - 1, str8_wrap(anim->name).length));

			for (uint32_t keyframe = 0; keyframe < out_anim->keyframe_count; ++keyframe) {
				Transform3 *pose = out_anim->keyframes[keyframe];
				out_anim->keyframes[keyframe] = arena_push_count(arena, Transform3, out_anim->bone_count);
			}

			ASSERT(out_anim->bone_count == anim->channels_count / 3 && "expect all channels to be written");
			for (uint32_t channel_index = 0; channel_index < anim->channels_count; ++channel_index) {
				cgltf_animation_channel *channel = &anim->channels[channel_index];
				cgltf_animation_sampler *sampler = channel->sampler;
				ASSERT(sampler->interpolation == cgltf_interpolation_type_linear && "expect all animations to interpolate linearly");

				uint32_t bone_index = -1;
				for (uint32_t index = 0; index < out_anim->bone_count; ++index) {
					if (channel->target_node == skin->joints[index]) {
						bone_index = index;
						break;
					}
				}
				if (bone_index == (uint32_t)-1)
					continue;

				for (uint32_t keyframe = 0; keyframe < sampler->input->count; ++keyframe) {
					cgltf_accessor_read_float(sampler->input, keyframe, &out_anim->timings[keyframe], 1);
					Transform3 *transforms = out_anim->keyframes[keyframe];

					if (channel->target_path == cgltf_animation_path_type_translation)
						cgltf_accessor_read_float(sampler->output, keyframe, (float *)&transforms[bone_index].translation, 3);
					else if (channel->target_path == cgltf_animation_path_type_rotation)
						cgltf_accessor_read_float(sampler->output, keyframe, (float *)&transforms[bone_index].rotation, 4);
					else if (channel->target_path == cgltf_animation_path_type_scale)
						cgltf_accessor_read_float(sampler->output, keyframe, (float *)&transforms[bone_index].scale, 3);
				}
			}
		}
	}
	cgltf_free(data);

	return result;
}

void push_rect2(Arena *arena, Rectangle rect, Color color) {
	float4 f_color = {
		.x = color.r / 255.f,
		.y = color.g / 255.f,
		.z = color.b / 255.f,
		.w = color.a / 255.f,
	};

	float x0 = rect.x;
	float y0 = rect.y;
	float x1 = rect.x + rect.width;
	float y1 = rect.y + rect.height;

	/* float u0 = src.x / image.width; */
	/* float v0 = src.y / image.height; */
	/* float u1 = (src.x + src.width) / image.width; */
	/* float v1 = (src.y + src.height) / image.height; */

	// clang-format off
    Vertex2 quad[] = {
        // pos      // tex
        (Vertex2){.position = {x0, y1}, .uv = {0.0f, 1.0f}, .color = f_color }, // , .image_id = image_index},
        (Vertex2){.position = {x1, y0}, .uv = {1.0f, 0.0f}, .color = f_color }, // , .image_id = image_index},
        (Vertex2){.position = {x0, y0}, .uv = {0.0f, 0.0f}, .color = f_color }, // , .image_id = image_index}, 

        (Vertex2){.position = {x0, y1}, .uv = {0.0f, 1.0f}, .color = f_color }, // , .image_id = image_index},
        (Vertex2){.position = {x1, y1}, .uv = {1.0f, 1.0f}, .color = f_color }, // , .image_id = image_index},
        (Vertex2){.position = {x1, y0}, .uv = {1.0f, 0.0f}, .color = f_color }, // , .image_id = image_index}
    };
	// clang-format on

	memory_copy(arena_push_count(arena, Vertex2, 6), quad, sizeof(quad));
}

void push_rect2_outline(Arena *arena, Rectangle rect, float thickness, Color color) {
	push_rect2(arena, rect(rect.x, rect.y, rect.width, thickness), color);
	push_rect2(arena, rect(rect.x, rect.y, thickness, rect.height), color);
	push_rect2(arena, rect(rect.x + rect.width, rect.y, thickness, rect.height + thickness), color);
	push_rect2(arena, rect(rect.x, rect.y + rect.height, rect.width + thickness, thickness), color);
}

void push_triangle2(Arena *arena, Triangle2 t, float thickness, Color color) {
	push_line2(arena, t.a, t.b, thickness, color);
	push_line2(arena, t.b, t.c, thickness, color);
	push_line2(arena, t.c, t.a, thickness, color);
}

void push_line2(Arena *arena, float2 start, float2 end, float thickness, Color color) {
	*arena_push_count(arena, Line, 1) = (Line){
		.position = float3_from_float2(start),
		.thickness = thickness,
		.color = color_pack(color),
	};
	*arena_push_count(arena, Line, 1) = (Line){
		.position = float3_from_float2(end),
		.thickness = thickness,
		.color = color_pack(color),
	};
}

void imgui_frame_begin(Arena *arena) {
	imgui_state.batch_arena = arena;
	imgui_state.mouse_position = float2_from_double2(input_mouse_position());
	imgui_state.mouse_pressed = input_mouse_pressed(MOUSE_BUTTON_LEFT);
	imgui_state.mouse_released = input_mouse_released(MOUSE_BUTTON_LEFT);
}

void imgui_frame_end(void) {
	if (imgui_state.mouse_pressed && imgui_state.active_id == 0)
		imgui_state.active_id = -1;
	if (imgui_state.mouse_released)
		imgui_state.active_id = 0;

	imgui_state.batch_arena = 0;
	imgui_state.hovered_id = 0;
	imgui_state.mouse_position = (float2){ 0 };
	imgui_state.mouse_pressed = 0;
	imgui_state.mouse_released = 0;
}

ImguiInteraction imgui_interact(uint64_t id, Rectangle rect) {
	ImguiInteraction result = { 0 };

	float2 mouse = imgui_state.mouse_position;
	result.hovering = rect_contains(rect, mouse.x, mouse.y);

	if (result.hovering) {
		imgui_state.hovered_id = id;

		if (imgui_state.active_id == 0 && imgui_state.mouse_pressed)
			imgui_state.active_id = id;
	}

	result.held = imgui_state.active_id == id;
	result.clicked = result.hovering && result.held && imgui_state.mouse_released;

	return result;
}

ImguiInteraction imgui_button(uint64_t id, float x, float y, float w, float h) {
	Rectangle rect = rect(x, y, w, h);
	ImguiInteraction result = imgui_interact(id, rect);

	bool ok = imgui_state.batch_arena;
	if (ok) {
		y = result.held ? y + 1 : y;

		push_rect2(imgui_state.batch_arena, rect(x, y, w, h), result.hovering ? rgb(100, 100, 100) : rgb(50, 50, 50));
	}

	return result;
}

float imgui_slidervf(uint64_t id, Rectangle bounds, float min, float max, float t) {
	const float pad = bounds.width * 0.8f;
	push_rect2(imgui_state.batch_arena, bounds, WHITE);

	t = clampf(t, min, max);

	float thumb_w = bounds.width - pad * 2.0f;
	float thumb_h = thumb_w;

	float start = bounds.y + pad;
	float travel = bounds.height - (pad * 2 + thumb_h);

	float t_norm = max - min != 0.0f ? (t - min) / (max - min) : 0.0f;
	Rectangle thumb = rect(bounds.x + pad, start + t_norm * travel, thumb_w, thumb_h);

	ImguiInteraction interct = imgui_interact(id, bounds);
	if (interct.held) {
		float2 mouse = imgui_state.mouse_position;
		mouse.y -= start + thumb.height * 0.5f;
		mouse.y = clampf(mouse.y, 0.0f, travel);

		float mouse_ratio = (travel != 0.0f) ? (mouse.y / travel) : 0.0f;
		t = min + (mouse_ratio * (max - min));
	}

	push_rect2(imgui_state.batch_arena, thumb, interct.held ? BLUE : interct.hovering ? GRAY
																					  : DARK_GRAY);

	return t;
}

float imgui_sliderhf(uint64_t id, Rectangle bounds, float min, float max, float t) {
	const float pad = bounds.height * 0.8f;
	push_rect2(imgui_state.batch_arena, bounds, WHITE);

	t = clampf(t, min, max);

	float thumb_h = bounds.height - pad * 2.0f;
	float thumb_w = thumb_h;

	float start = bounds.x + pad;
	float travel = bounds.width - (pad * 2 + thumb_w);

	float t_norm = max - min != 0.0f ? (t - min) / (max - min) : 0.0f;
	Rectangle thumb = rect(start + t_norm * travel, bounds.y + pad, thumb_w, thumb_h);

	ImguiInteraction interct = imgui_interact(id, bounds);
	if (interct.held) {
		float2 mouse = imgui_state.mouse_position;
		mouse.x -= start + thumb.width * 0.5f;
		mouse.x = clampf(mouse.x, 0.0f, travel);

		float mouse_ratio = (travel != 0.0f) ? (mouse.x / travel) : 0.0f;
		t = min + (mouse_ratio * (max - min));
	}

	push_rect2(imgui_state.batch_arena, thumb, interct.held ? BLUE : interct.hovering ? GRAY
																					  : DARK_GRAY);

	return t;
}

void push_arc3(Arena *arena, float3 center, float radius, uint8_t segments, AxisPlane plane, float angle_span, float thickness, Color color) {
	for (uint32_t i = 0; i < segments; ++i) {
		float a = ((float)i / segments) * angle_span;
		float an = ((float)(i + 1) / segments) * angle_span;
		float ca = cosf(a), sa = sinf(a);
		float can = cosf(an), san = sinf(an);

		Line p0, p1;
		switch (plane) {
			case AXIS_PLANE_DOWN:
			case AXIS_PLANE_UP:
				p0 = (Line){ { center.x + ca * radius, center.y, center.z + sa * radius }, thickness, color_pack(color), FLOAT3_ZERO };
				p1 = (Line){ { center.x + can * radius, center.y, center.z + san * radius }, thickness, color_pack(color), FLOAT3_ZERO };
				break;
			case AXIS_PLANE_LEFT:
			case AXIS_PLANE_RIGHT:
				p0 = (Line){ { center.x, center.y + sa * radius, center.z + ca * radius }, thickness, color_pack(color), FLOAT3_ZERO };
				p1 = (Line){ { center.x, center.y + san * radius, center.z + can * radius }, thickness, color_pack(color), FLOAT3_ZERO };
				break;
			case AXIS_PLANE_BACKWARD:
			case AXIS_PLANE_FORWARD:
				p0 = (Line){ { center.x + ca * radius, center.y + sa * radius, center.z }, thickness, color_pack(color), FLOAT3_ZERO };
				p1 = (Line){ { center.x + can * radius, center.y + san * radius, center.z }, thickness, color_pack(color), FLOAT3_ZERO };
				break;
			case AXIS_PLANE_COUNT:
				ASSERT(false);
				break;
		}

		*arena_push_count(arena, Line, 1) = p0;
		*arena_push_count(arena, Line, 1) = p1;
	}
}

void push_aabb3_outline(Arena *arena, AABB3 aabb3, float thickness, Color color) {
	float3 min = aabb3.min;
	float3 max = aabb3.max;
	float3 bounding_box_size = float3_subtract(max, min);

	Line outline[] = {
		{ { min.x, min.y, min.z }, thickness, color_pack(color), FLOAT3_ZERO },
		{ { min.x, max.y, min.z }, thickness, color_pack(color), FLOAT3_ZERO },

		{ { min.x, min.y, max.z }, thickness, color_pack(color), FLOAT3_ZERO },
		{ { min.x, max.y, max.z }, thickness, color_pack(color), FLOAT3_ZERO },

		{ { max.x, min.y, min.z }, thickness, color_pack(color), FLOAT3_ZERO },
		{ { max.x, max.y, min.z }, thickness, color_pack(color), FLOAT3_ZERO },

		{ { max.x, min.y, max.z }, thickness, color_pack(color), FLOAT3_ZERO },
		{ { max.x, max.y, max.z }, thickness, color_pack(color), FLOAT3_ZERO },

		{ { min.x, min.y, min.z }, thickness, color_pack(color), FLOAT3_ZERO },
		{ { min.x, min.y, max.z }, thickness, color_pack(color), FLOAT3_ZERO },

		{ { min.x, min.y, min.z }, thickness, color_pack(color), FLOAT3_ZERO },
		{ { max.x, min.y, min.z }, thickness, color_pack(color), FLOAT3_ZERO },

		{ { max.x, min.y, max.z }, thickness, color_pack(color), FLOAT3_ZERO },
		{ { max.x, min.y, min.z }, thickness, color_pack(color), FLOAT3_ZERO },

		{ { max.x, min.y, max.z }, thickness, color_pack(color), FLOAT3_ZERO },
		{ { min.x, min.y, max.z }, thickness, color_pack(color), FLOAT3_ZERO },

		{ { min.x, max.y, min.z }, thickness, color_pack(color), FLOAT3_ZERO },
		{ { min.x, max.y, max.z }, thickness, color_pack(color), FLOAT3_ZERO },

		{ { min.x, max.y, min.z }, thickness, color_pack(color), FLOAT3_ZERO },
		{ { max.x, max.y, min.z }, thickness, color_pack(color), FLOAT3_ZERO },

		{ { max.x, max.y, max.z }, thickness, color_pack(color), FLOAT3_ZERO },
		{ { max.x, max.y, min.z }, thickness, color_pack(color), FLOAT3_ZERO },

		{ { max.x, max.y, max.z }, thickness, color_pack(color), FLOAT3_ZERO },
		{ { min.x, max.y, max.z }, thickness, color_pack(color), FLOAT3_ZERO },
	};

	Line *points = arena_push_count(arena, Line, countof(outline));
	memory_copy_array(points, outline);
}

void push_line3(Arena *arena, float3 start, float3 end, float thickness, Color color) {
	*arena_push_count(arena, Line, 1) = (Line){
		.position = start,
		.thickness = thickness,
		.color = color_pack(color),
	};
	*arena_push_count(arena, Line, 1) = (Line){
		.position = end,
		.thickness = thickness,
		.color = color_pack(color),
	};
}

void push_triangle3(Arena *arena, Triangle3 t, float thickness, Color color) {
	push_line3(arena, t.a, t.b, thickness, color);
	push_line3(arena, t.b, t.c, thickness, color);
	push_line3(arena, t.c, t.a, thickness, color);
}

void push_rect3_outline(Arena *arena, Plane plane, float width, float height, float thickness, Color color) {
	float3 right = { 0 }, up = { 0 };
	float dot = float3_dot(plane.normal, FLOAT3_Y);
	if (fabsf(dot) >= 0.99f) {
		right.x = dot > 0 ? 1.0f : -1.0f;
		up.z = -1.0f;
	} else {
		right = float3_normalize_safe(float3_cross(plane.normal, (float3){ 0.0f, 1.0f, 0.0f }), EPSILON);
		up = float3_normalize_safe(float3_cross(plane.normal, right), EPSILON);
	}

	float3 center = float3_scale(plane.normal, plane.distance);
	float3 h = float3_scale(right, width * 0.5f);
	float3 v = float3_scale(up, height * 0.5f);

	float3 corners[] = {
		float3_add(float3_subtract(center, h), v),
		float3_add(float3_add(center, h), v),
		float3_subtract(float3_add(center, h), v),
		float3_subtract(float3_subtract(center, h), v),
	};

	for (uint32_t index = 0; index < countof(corners); ++index)
		push_line3(arena, corners[index], corners[(index + 1) % countof(corners)], thickness, color);
}
