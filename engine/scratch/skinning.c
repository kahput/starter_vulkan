#include "common.h"
#include "core/arena.h"
#include "core/cmath.h"
#include "core/input_types.h"
#include "core/logger.h"
#include "core/debug.h"

#include "core/strings.h"

#include "input.h"
#include "os.h"
#include "anim.h"
#include "gfx/gfx_types.h"
#include "scene.h"

#include <math.h>

#include <vulkan/vulkan.h>
#include <cgltf/cgltf.h>
#include <stb/stb_image.h>
#include <vulkan/vulkan_core.h>

#define MAX_FRAMES_IN_FLIGHT 2

typedef struct GFX_Buffer GFX_Buffer;
struct GFX_Buffer {
	GFX_Buffer *next;

	VkBuffer handle;
	VkDeviceMemory memory;
	uint8_t *mapped;
	VkDeviceAddress address;

	uint64_t size;
	VkBufferCreateInfo info;

	BufferMemory memory_type;
	BufferUsage usage;
};

typedef struct GFX_Image GFX_Image;
struct GFX_Image {
	GFX_Image *next;

	VkImage handle;
	VkImageView view;
	VkDeviceMemory memory;

	uint32_t width, height;

	PixelFormat format;
	ResourceUsage usage;

	VkImageCreateInfo image_info;
	VkImageViewCreateInfo view_info;
};

typedef struct GFX_Sampler GFX_Sampler;
struct GFX_Sampler {
	GFX_Sampler *next;

	VkSampler handle;
	VkSamplerCreateInfo info;
};

#define MAX_DESCRIPTOR_SETS 4
typedef struct Shader Shader;
struct Shader {
	Shader *next;

	VkPipeline handle;
	VkPipelineLayout layout;
	VkDescriptorSetLayout set_layouts[MAX_DESCRIPTOR_SETS];
	VkShaderModule shaders[SHADER_STAGE_COUNT];
};

#define SWAPCHAIN_IMAGE_COUNT 3
typedef struct Swapchain GFX_Swapchain;
struct Swapchain {
	GFX_Swapchain *next;

	VkSwapchainKHR handle;
	VkSurfaceKHR surface;

	VkSwapchainCreateInfoKHR info;

	VkImage images[SWAPCHAIN_IMAGE_COUNT];
	VkImageView views[SWAPCHAIN_IMAGE_COUNT];
	VkImageViewCreateInfo view_infos[SWAPCHAIN_IMAGE_COUNT];
	uint32_t image_count;

	VkSemaphore image_available_semaphores[MAX_FRAMES_IN_FLIGHT]; // has to be MAX_FRAMES_IN_FLIGHT, as you need one for each frame index
	VkSemaphore render_done_semaphores[SWAPCHAIN_IMAGE_COUNT]; // has to be SWAPCHAIN_IMAGE_COUNT, as you need one for each swapchain image
};

typedef struct {
	struct {
		uint64_t offset, size;
		uint32_t buffer_id;
	} buffers[8];
	struct {
		uint32_t image_id, sampler_id;
	} textures[8];
} GFX_Bindings;

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

	uint32_t frame_index;
} GFX_CommandContext;

typedef struct {
	Arena arena[1];

	VkInstance instance;
	VulkanDevice device;

	// Engine globals
	VkCommandPool graphics_command_pool;
	VkPushConstantRange global_range;

	GFX_Buffer *staging_buffer;
	uint64_t staging_buffer_frame_size;

	VkQueue graphics_queue, present_queue;
	VkQueue transfer_queue, compute_queue;

	int32_t graphics_index, present_index;
	int32_t transfer_index, compute_index;

	// Transfer
	/* GFX_CommandBuffer *transfer_buffers[2]; */
	/* uint32_t current_transfer_index; */
	uint64_t transfer_pending_generation; // generation currently accumulating, not yet submitted
	uint64_t transfer_submitted_generation; // highest generation actually handed to the queue

	// Frame resources
	GFX_CommandContext cmd_buffers[MAX_FRAMES_IN_FLIGHT];
	uint32_t current_frame_index;

	// Resources
	GFX_Buffer *buffers;
	uint32_t buffer_count;

	GFX_Image *images;
	uint32_t image_count;

	GFX_Sampler *samplers;
	uint32_t sampler_count;

	Shader *shaders;
	uint32_t shader_count;

	GFX_Swapchain *swapchains;
	uint32_t swapchain_count;

	GFX_Buffer *first_free_buffer;
	GFX_Image *first_free_image;
	GFX_Sampler *first_free_sampler;
	Shader *first_free_shader;
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

GFX_CommandContext gfx_transfer_batch_begin(GFX_Context *context);
bool gfx_transfer_batch_submit(GFX_Context *context, GFX_CommandContext *batch);

typedef struct {
	float2 position, uv;
	float4 color;
} Vertex2;

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

GFX_Buffer *gfx_buffer_make(GFX_Context *context, uint64_t size, BufferMemory memory, BufferUsage usage, const char *debug_name);
GFX_Image *gfx_image_make(GFX_Context *context, uint32_t width, uint32_t height, ImageOptions options);
GFX_Sampler *gfx_sampler_make(GFX_Context *context, SamplerOptions opt);
GFX_Swapchain *gfx_swapchain_make(GFX_Context *context, OS_Surface *surface, const char *debug_name);

Shader compute_pipeline_make(GFX_Context *context, String8 compute_bytecode, VkDescriptorSetLayout *layouts, uint32_t layout_count);
Shader graphics_pipeline_make(
	GFX_Context *context,
	String8 vertex_bytecode,
	String8 fragment_bytecode,
	VkDescriptorSetLayout *layouts,
	uint32_t layout_count, PipelineOptions options);

bool gfx_buffer_destroy(GFX_Context *context, GFX_Buffer *buffer);
bool gfx_image_destroy(GFX_Context *context, GFX_Image *image);
bool gfx_sampler_destroy(GFX_Context *context, GFX_Sampler *sampler);
bool swapchain_destroy(GFX_Context *context, GFX_Swapchain *surface);
void pipeline_destroy(GFX_Context *context, Shader *pipeline);

GFX_Image gfx_swapchain_backbuffer(GFX_Context *context, GFX_CommandContext *cmd, GFX_Swapchain *surface);

// TODO: double buffered transfers
/* void gfx_upload_buffer(GFX_Context *context, GFX_Buffer *dst, uint64_t size, void *data, ResourceUsage final_usage); */
/* void gfx_upload_image(GFX_Context *context, GFX_Image *image, uint32_t width, uint32_t height, void *pixels, ResourceUsage final_usage); */

void gfx_cmd_buffer_to_buffer(GFX_CommandContext *cmd, GFX_Buffer *dst, GFX_Buffer *src, uint64_t dst_offset, uint64_t src_offset, uint64_t size);
void gfx_cmd_buffer_to_image(GFX_CommandContext *cmd, GFX_Image *dst, GFX_Buffer *src, uint64_t src_offset, uint32_t width, uint32_t height);
void gfx_cmd_buffer_barrier(GFX_CommandContext *cmd, ResourceUsage src, ResourceUsage dst, uint64_t offset, uint64_t size, GFX_Buffer *target);
void gfx_cmd_image_barrier(GFX_CommandContext *cmd, ResourceUsage src, ResourceUsage dst, GFX_Image *target);
void gfx_cmd_image_transition(GFX_CommandContext *cmd, ResourceUsage dst, GFX_Image *target);
void gfx_cmd_image_blit(GFX_CommandContext *cmd, Rectangle source_rect, GFX_Image *source, Rectangle target_rect, GFX_Image *target);
void gfx_cmd_image_upload(GFX_CommandContext *cmd, GFX_Image *image, uint32_t width, uint32_t height, void *pixels);
void gfx_cmd_buffer_copy(GFX_CommandContext *cmd, GFX_Buffer *dst, GFX_Buffer *src, uint64_t dst_offset, uint64_t src_offset, uint64_t size);
/* VkDescriptorSet gfx_cmd_bindset_push(GFX_CommandBuffer *cmd); */

VkImageLayout gfx__usage_to_image_layout(ResourceUsage usage);

typedef enum {
	TEXTURE_SLOT_ALEBDO,
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
	GFX_Image *gpu_image;

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
} Mesh;

typedef enum {
	MESH_HERO_MALE,
	MESH_GDBOT,
	MESH_MAGE,
	MESH_BARREL,
	MESH_ROOM,

	MESH_ROOM_LARGE,
	MESH_TERRAIN,
	MESH_GRASS_BILLBOARD,

	MESH_COUNT,
} MeshID;

typedef struct {
	MeshID id;
	Transform3 transform;
	uint64_t skinned_vertices_offset;
	bool cast_shadow;
} MeshInstance;

String8 meshid_to_path[MESH_COUNT] = {
	[MESH_HERO_MALE] = str_comp("assets/models/hero_male.glb"),
	[MESH_GDBOT] = str_comp("assets/models/gdbot.glb"),
	[MESH_MAGE] = str_comp("assets/models/mage.glb"),
	[MESH_BARREL] = str_comp("assets/models/barrel.glb"),
	[MESH_ROOM] = str_comp("assets/models/room.glb"),
	[MESH_ROOM_LARGE] = str_comp("assets/models/room-large.glb"),
	[MESH_GRASS_BILLBOARD] = str_comp("assets/models/grass.glb"),
};

typedef struct {
	GFX_Buffer vertex_buffer, index_buffer;
	uint32_t vertex_cursor, index_cursor;

	Mesh meshes[MESH_COUNT];
} RES_Cache;

typedef enum {
	FACE_RIGHT,
	FACE_LEFT,
	FACE_UP,
	FACE_DOWN,
	FACE_FORWARD,
	FACE_BACKWARD,

	FACE_COUNT,
} Face;

Image2D load_image(Arena *arena, String8 path);
Image2D load_cubemap(Arena *arena, String8 paths[FACE_COUNT]);
Mesh load_gltf(Arena *arena, String8 path);

Mesh generate_plane(Arena *arena, Face orientation, float width, float height, uint32_t subdivision_x, uint32_t subdivision_z);
AnimationClip *load_gltf_animations(Arena *arena, String8 path, uint32_t *count);

AnimationClip *find_animation(AnimationClip *clips, uint32_t count, String8 target) {
	for (uint32_t anim_index = 0; anim_index < count; ++anim_index)
		if (str8_equals(str8_wrap(clips[anim_index].name), target))
			return clips + anim_index;

	return NULL;
}

int main(void) {
	logger_set_level(LOG_LEVEL_DEBUG);

	os_display_startup();
	OS_Surface *main_render = os_surface_open(1280, 720, s("main_render"), OS_SURFACE_FLAG_RESIZEABLE);
	OS_Surface *popup_compute = os_surface_open(640, 360, s("popup_compute"), 0);

	uint2 dims = os_surface_size(main_render);

	uint64_t start_time = os_time_ns();

	InputState input_state = { 0 };
	input_set_context(&input_state);
	Camera3 camera = {
		.projection = CAMERA_PROJECTION_PERSPECTIVE,
		.position = { 0.0f, 1.5f, 20.f },
		.target = { 0.0f, 1.5f, 0.0f },
		.up = FLOAT3_Y,
		.fovy = 45.f,
	};

	Arena arena[1] = { arena_make(MiB(256)) };

	GFX_Context context[] = { 0 };
	gfx_startup(context);
	GFX_Swapchain *swapchains[] = { gfx_swapchain_make(context, popup_compute, "popup"), gfx_swapchain_make(context, main_render, "main") };

	// :targets
	GFX_Image *compute_image = gfx_image_make(context, 640, 360, (ImageOptions){ .format = PIXELFORMAT_RGBA16_FLOAT, .usage = IMAGE_USAGE_STORAGE | IMAGE_USAGE_TRANSFER });
	GFX_Image *offscreen_render = gfx_image_make(context, 948, 1044, (ImageOptions){ .format = PIXELFORMAT_BACKBUFFER, .usage = IMAGE_USAGE_RENDER, .sample = SAMPLE_COUNT_8 });
	GFX_Image *depthbuffer = gfx_image_make(context, 948, 1044, (ImageOptions){ .format = PIXELFORMAT_DEPTH, .usage = IMAGE_USAGE_RENDER, .sample = SAMPLE_COUNT_8 });
	GFX_Image *shadow_depthbuffer = gfx_image_make(context, 2048, 2048, (ImageOptions){ .format = PIXELFORMAT_DEPTH, .usage = IMAGE_USAGE_RENDER | IMAGE_USAGE_SAMPLE });

#define array_arg(T, ...) \
	(T[]) { __VA_ARGS__, (T){ 0 } }
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
	skybox.gpu_image = gfx_image_make(context, skybox.width, skybox.height, (ImageOptions){ .format = PIXELFORMAT_RGBA8_SRGB, .type = IMAGE_TYPE_CUBE });

	Image2D terrain_texture = load_image(arena, s("assets/textures/base_grass.png"));
	/* Image2D grass_billboard_texture = load_image(arena, s("assets/textures/grass.png")); */

	GFX_Sampler *linear_sampler = gfx_sampler_make(context, sampler_opt(FILTER_LINEAR, WRAP_MODE_REPEAT));
	SamplerOptions shadow_opt = sampler_opt(FILTER_LINEAR, WRAP_MODE_CLAMP_BORDER);
	shadow_opt.debug_name = "shadow_sampler";
	/* shadow_opt.compare_enable = true; */
	GFX_Sampler *shadow_sampler = gfx_sampler_make(context, shadow_opt);
	GFX_Image *white_texture = gfx_image_make(context, 1, 1, (ImageOptions){ .format = PIXELFORMAT_RGBA8_UNORM });

	// create compute pipeline
	OS_Timestamp compute_ts = os_file_last_modified(s("assets/shaders/compute/bin/test.compute.spv"));
	VkDescriptorSetLayout test_compute_descriptor_layout = 0;
	Shader c_pipeline = { 0 };
	{
		ArenaTemp scratch = arena_scratch_begin(NULL);

		String8 compute_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/compute/bin/test.compute.spv"));
		VkDescriptorSetLayoutBinding bindings[] = {
			[0] = {
			  .binding = 0,
			  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			  .descriptorCount = 1,
			  .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
		};

		VkDescriptorSetLayoutCreateInfo dsl_create_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = countof(bindings),
			.pBindings = bindings,
		};
		vkCreateDescriptorSetLayout(context->device.logical, &dsl_create_info, NULL, &test_compute_descriptor_layout);
		c_pipeline = compute_pipeline_make(context, compute_bytecode, &test_compute_descriptor_layout, 1);
		arena_scratch_end(scratch);
	}

	Shader pipeline_skinning = { 0 };
	{ // :skinning
		ArenaTemp scratch = arena_scratch_begin(NULL);
		String8 compute_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/compute/bin/skinning.compute.spv"));
		pipeline_skinning = compute_pipeline_make(context, compute_bytecode, NULL, 0);
		arena_scratch_end(scratch);
	}

	Shader pipeline_shadow = { 0 };
	{
		ArenaTemp scratch = arena_scratch_begin(NULL);
		String8 vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/shadow.vertex.spv"));
		String8 fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/blank.fragment.spv"));

		VkDescriptorSetLayout layouts[2] = { 0 };
		{ // set 0
			VkDescriptorSetLayoutBinding bindings[] = {
				[0] = {
				  .binding = 0,
				  .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				  .descriptorCount = 1,
				  .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				},
			};

			VkDescriptorSetLayoutCreateInfo dsl_create_info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = countof(bindings),
				.pBindings = bindings,
			};
			vkCreateDescriptorSetLayout(context->device.logical, &dsl_create_info, NULL, &layouts[0]);
		}

		{ // set 1
			VkDescriptorSetLayoutBinding bindings[] = {
				[0] = {
				  .binding = 0,
				  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				  .descriptorCount = 1,
				  .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
				},
			};

			VkDescriptorSetLayoutCreateInfo dsl_create_info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = countof(bindings),
				.pBindings = bindings,
			};
			vkCreateDescriptorSetLayout(context->device.logical, &dsl_create_info, NULL, &layouts[1]);
		}

		pipeline_shadow = graphics_pipeline_make(context, vertex_bytecode, fragment_bytecode, layouts, countof(layouts), (PipelineOptions){ 0 });
		arena_scratch_end(scratch);
	}

	Shader pipeline_3d = { 0 };
	Shader pipeline_skybox = { 0 };
	Shader pipeline_grass = { 0 };
	{ // create 3d graphics pipeline
		ArenaTemp scratch = arena_scratch_begin(NULL);
		String8 vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/batch3d.vertex.spv"));
		String8 fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/phong.fragment.spv"));

		VkDescriptorSetLayout layouts[2] = { 0 };
		{ // set 0
			VkDescriptorSetLayoutBinding bindings[] = {
				[0] = {
				  .binding = 0,
				  .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				  .descriptorCount = 1,
				  .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				},
				[1] = {
				  .binding = 1,
				  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				  .descriptorCount = 1,
				  .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				},
				[2] = {
				  .binding = 2,
				  .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				  .descriptorCount = 1,
				  .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				},
				[3] = {
				  .binding = 3,
				  .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				  .descriptorCount = 1,
				  .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				},
			};

			VkDescriptorSetLayoutCreateInfo dsl_create_info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = countof(bindings),
				.pBindings = bindings,
			};
			vkCreateDescriptorSetLayout(context->device.logical, &dsl_create_info, NULL, &layouts[0]);
		}

		{ // set 1
			VkDescriptorSetLayoutBinding bindings[] = {
				[0] = {
				  .binding = 0,
				  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				  .descriptorCount = 1,
				  .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
				},
				[1] = {
				  .binding = 1,
				  .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				  .descriptorCount = 5,
				  .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				},
			};

			VkDescriptorSetLayoutCreateInfo dsl_create_info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = countof(bindings),
				.pBindings = bindings,
			};
			vkCreateDescriptorSetLayout(context->device.logical, &dsl_create_info, NULL, &layouts[1]);
		}

		pipeline_3d = graphics_pipeline_make(context, vertex_bytecode, fragment_bytecode, layouts, countof(layouts),
			(PipelineOptions){
			  .color_attachment_count = 1,
			  .color_attachments = { PIXELFORMAT_BACKBUFFER },
			  .sample_count = SAMPLE_COUNT_8,
			  .cull_mode = CULL_NONE,
			});

		{ // set 1
			VkDescriptorSetLayoutBinding bindings[] = {
				[0] = {
				  .binding = 0,
				  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				  .descriptorCount = 1,
				  .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
				},
				[1] = {
				  .binding = 1,
				  .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				  .descriptorCount = 5,
				  .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				},
				[2] = {
				  .binding = 2,
				  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				  .descriptorCount = 1,
				  .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
				},
			};

			VkDescriptorSetLayoutCreateInfo dsl_create_info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = countof(bindings),
				.pBindings = bindings,
			};

			vkCreateDescriptorSetLayout(context->device.logical, &dsl_create_info, NULL, &layouts[1]);
		}

		vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/grass.vertex.spv"));
		fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/grass.fragment.spv"));
		pipeline_grass = graphics_pipeline_make(context, vertex_bytecode, fragment_bytecode, layouts, countof(layouts),
			(PipelineOptions){
			  .color_attachment_count = 1,
			  .color_attachments = { PIXELFORMAT_BACKBUFFER },
			  .sample_count = SAMPLE_COUNT_8,
			});

		vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/skybox.vertex.spv"));
		fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/skybox.fragment.spv"));
		pipeline_skybox = graphics_pipeline_make(context, vertex_bytecode, fragment_bytecode, layouts, 1,
			(PipelineOptions){
			  .color_attachment_count = 1,
			  .color_attachments = { PIXELFORMAT_BACKBUFFER },
			  .sample_count = SAMPLE_COUNT_8,
			});

		arena_scratch_end(scratch);
	}

	GFX_Buffer *geometry = gfx_buffer_make(context, MiB(256), BUFFER_MEMORY_LOCAL, BUFFER_USAGE_STORAGE | BUFFER_USAGE_INDEX | BUFFER_USAGE_TRANSFER, "geometry");
	GFX_Buffer *grass_instancing_buffer = gfx_buffer_make(context, MiB(128), BUFFER_MEMORY_LOCAL, BUFFER_USAGE_STORAGE | BUFFER_USAGE_TRANSFER, "grass_instancing");

	const uint32_t map_width = 256;
	const uint32_t map_depth = 256;

	Mesh meshes[MESH_COUNT] = { 0 };
	meshes[MESH_TERRAIN] = generate_plane(arena, FACE_UP, map_width, map_depth, 4, 4);
	meshes[MESH_TERRAIN].materials[0].textures[TEXTURE_SLOT_ALEBDO] = terrain_texture;
	uint32_t animation_counts[MESH_COUNT] = { 0 };
	AnimationClip *animations[MESH_COUNT] = { 0 };

	for (uint32_t meshid = 0; meshid < MESH_COUNT; ++meshid) {
		if (meshid_to_path[meshid].length == 0)
			continue;

		meshes[meshid] = load_gltf(arena, meshid_to_path[meshid]);
		if (meshes[meshid].skeleton.bone_count == 0 || meshid == MESH_MAGE)
			continue;
		animations[meshid] = load_gltf_animations(arena, meshid_to_path[meshid], &animation_counts[meshid]);
	}

	{ // :upload
		GFX_CommandContext cmd = gfx_transfer_batch_begin(context);
		if (cmd.handle) {
			gfx_cmd_image_upload(&cmd, white_texture, 1, 1, &(uint32_t){ 0xffffffff });
			gfx_cmd_image_transition(&cmd, RESOURCE_USAGE_SHADER_READ, white_texture);

			gfx_cmd_image_upload(&cmd, skybox.gpu_image, skybox.width, skybox.height, skybox.pixels);
			gfx_cmd_image_transition(&cmd, RESOURCE_USAGE_SHADER_READ, skybox.gpu_image);

			for (uint32_t mesh_index = 0; mesh_index < countof(meshes); ++mesh_index) {
				Mesh *mesh = &meshes[mesh_index];
				for (uint32_t material_index = 0; material_index < mesh->material_count; ++material_index) {
					Material *material = &mesh->materials[material_index];

					for (uint32_t texture_slot = 0; texture_slot < TEXTURE_SLOT_COUNT; ++texture_slot) {
						Image2D *img = &material->textures[texture_slot];
						if (img->pixels) {
							img->gpu_image = gfx_image_make(context, img->width, img->height, (ImageOptions){ .format = img->format });
							gfx_cmd_image_upload(&cmd, img->gpu_image, img->width, img->height, img->pixels);
							gfx_cmd_image_transition(&cmd, RESOURCE_USAGE_SHADER_READ, img->gpu_image);
						} else {
							img->width = img->height = 1;
							img->format = PIXELFORMAT_RGBA8_SRGB;
							img->gpu_image = white_texture;
						}
					}
				}
			}

			uint64_t grass_upload_offset = arena_mark(cmd.transient_arena);
			// vertex_count = 512 * 512 * 3 * 6 = 4.718.592
			for (uint32_t z = 0; z < map_depth; ++z) {
				for (uint32_t x = 0; x < map_width; ++x) {
					float3 pos = {
						.x = x - (map_width * 0.5f) + ((float)(rand() % 10) / 20),
						.y = 0.0f,
						.z = z - (map_depth * 0.5f) + ((float)(rand() % 10) / 20),
					};
					pos = float3_scale(pos, 1.f / 2.f);
					*arena_push_count(cmd.transient_arena, float4, 1) = float4_from_float3(pos, 1.0f);
				}
			}
			gfx_cmd_buffer_to_buffer(&cmd, grass_instancing_buffer, cmd.transient_buffer, 0, grass_upload_offset, sizeof(float4) * map_width * map_depth);
			gfx_cmd_buffer_barrier(&cmd, RESOURCE_USAGE_TRANSFER_DST, RESOURCE_USAGE_SHADER_READ, 0, sizeof(float4) * map_width * map_depth, grass_instancing_buffer);

			uint64_t transient_upload_start_offset = cmd.transient_arena->offset;
			uint64_t geometry_upload_cursor = 0;

			for (uint32_t mesh_index = 0; mesh_index < countof(meshes); ++mesh_index) {
				Mesh *mesh = &meshes[mesh_index];
				mesh->buffer = geometry;

				uint64_t total_vertex_buffer_size = alignup(mesh->total_vertex_count * sizeof(Vertex3), 256);
				uint64_t total_index_buffer_size = alignup(mesh->total_index_count * sizeof(uint32_t), 256);
				uint64_t total_skinning_buffer_size = alignup(mesh->total_vertex_count * sizeof(SkinningData), 256);

				// Vertices
				mesh->buffer_vertex_byte_offset = geometry_upload_cursor;
				memory_copy(arena_push(cmd.transient_arena, total_vertex_buffer_size, 1, false), mesh->vertices, mesh->total_vertex_count * sizeof(Vertex3));
				geometry_upload_cursor += total_vertex_buffer_size;

				// Indices
				mesh->buffer_index_byte_offset = geometry_upload_cursor;
				memory_copy(arena_push(cmd.transient_arena, total_index_buffer_size, 1, false), mesh->indices, mesh->total_index_count * sizeof(uint32_t));
				geometry_upload_cursor += total_index_buffer_size;

				// Skinning
				if (mesh->skeleton.bone_count) {
					mesh->buffer_skinning_data_byte_offset = geometry_upload_cursor;
					memory_copy(arena_push(cmd.transient_arena, total_skinning_buffer_size, 1, false), mesh->skinning, mesh->total_vertex_count * sizeof(SkinningData));
					geometry_upload_cursor += total_skinning_buffer_size;
				}
			}

			gfx_cmd_buffer_to_buffer(&cmd, geometry, cmd.transient_buffer, 0, transient_upload_start_offset, geometry_upload_cursor);

			gfx_cmd_buffer_barrier(&cmd, RESOURCE_USAGE_TRANSFER_DST, RESOURCE_USAGE_SHADER_READ, 0, geometry_upload_cursor, geometry);
			gfx_cmd_buffer_barrier(&cmd, RESOURCE_USAGE_TRANSFER_DST, RESOURCE_USAGE_INDEX_BUFFER, 0, geometry_upload_cursor, geometry);

			gfx_transfer_batch_submit(context, &cmd);
		}
	}

	GFX_Buffer *scratch_buffers[MAX_FRAMES_IN_FLIGHT];
	for (uint32_t index = 0; index < countof(scratch_buffers); ++index) {
		scratch_buffers[index] = gfx_buffer_make(context, MiB(1), BUFFER_MEMORY_SHARED, BUFFER_USAGE_STORAGE | BUFFER_USAGE_UNIFORM | BUFFER_USAGE_TRANSFER, "scratch_buffer");
		vkMapMemory(context->device.logical, scratch_buffers[index]->memory, 0, scratch_buffers[index]->size, 0, (void **)&scratch_buffers[index]->mapped);
	}

	bool is_open = true;

	float dt = 0.0f;
	float last_frame = 0.0f;

	Arena frame_arena[] = { arena_make(MiB(1)) };
	memory_zero(context->staging_buffer->mapped, context->staging_buffer_frame_size);

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

		if (input_mouse_down(MOUSE_BUTTON_MIDDLE))
			os_cursor_capture(main_render, true);
		else
			os_cursor_capture(main_render, false);

		uint2 dims = os_surface_size(main_render);

		// Frame resources
		GFX_CommandContext *cmd = gfx_frame_begin(context);
		if (cmd == 0)
			break;
		VkCommandBuffer command_buffer = cmd->handle;
		VkDescriptorPool descriptor_pool = cmd->descriptor_pool;

		// Swapchain image acquisition
		GFX_Image compute_blit_target = gfx_swapchain_backbuffer(context, cmd, swapchains[0]);
		if (compute_blit_target.handle) {
			// transition swapchain target & blit src compute image
			gfx_cmd_image_transition(
				cmd,
				RESOURCE_USAGE_TRANSFER_DST,
				&compute_blit_target);
			gfx_cmd_image_transition(
				cmd,
				RESOURCE_USAGE_COMPUTE_SHADER_WRITE,
				compute_image);

			{ // Bind compute pipeline & descriptor set
				VkDescriptorSetAllocateInfo alloc_info = {
					.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
					.descriptorPool = descriptor_pool,
					.descriptorSetCount = 1,
					.pSetLayouts = c_pipeline.set_layouts + 0,
				};

				VkDescriptorSet compute_set = 0;
				vkAllocateDescriptorSets(context->device.logical, &alloc_info, &compute_set);
				VkDescriptorImageInfo image_info = {
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					.imageView = compute_image->view,
				};
				VkWriteDescriptorSet write = {
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = compute_set,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.pImageInfo = &image_info,
				};
				vkUpdateDescriptorSets(context->device.logical, 1, &write, 0, 0);

				vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, c_pipeline.handle);
				vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, c_pipeline.layout, 0, 1, &compute_set, 0, 0);
			}

			// Dispatch compute & Blit to main window surface
			vkCmdDispatch(command_buffer, 40, 23, 1);
			gfx_cmd_image_barrier(cmd, RESOURCE_USAGE_COMPUTE_SHADER_WRITE, RESOURCE_USAGE_TRANSFER_SRC, compute_image);
			gfx_cmd_image_blit(cmd, (Rectangle){ .width = 640, .height = 360 }, compute_image, (Rectangle){ .width = 640, .height = 360 }, &compute_blit_target);

			// transition swapchain images for presenting
			gfx_cmd_image_barrier(
				cmd,
				RESOURCE_USAGE_TRANSFER_DST,
				RESOURCE_USAGE_PRESENT,
				&compute_blit_target);

		} else //  TODO: resize swapchain
			break;

		GFX_Image main_target = gfx_swapchain_backbuffer(context, cmd, swapchains[1]);
		if (main_target.handle) {
			// transition swapchain & offscren targets for drawing
			gfx_cmd_image_transition(
				cmd,
				RESOURCE_USAGE_COLOR_ATTACHMENT,
				&main_target);
			gfx_cmd_image_transition(
				cmd,
				RESOURCE_USAGE_COLOR_ATTACHMENT,
				offscreen_render);

			static float anim_t = 0.0f;
			static float blink_timer = 0.0f;
			static uint32_t anim_index = 3;

			if (input_key_pressed(KEY_CODE_SPACE))
				anim_index++;
			anim_t += dt;
			blink_timer += dt;

			MeshInstance instances[] = {
				{ MESH_HERO_MALE, { FLOAT3_ZERO, FLOAT4_ZERO, FLOAT3_ONE }, 0, true },
				{ MESH_HERO_MALE, { { 0.0f, 0.0f, -3.0f }, FLOAT4_ZERO, FLOAT3_ONE }, 0, true },
				{ MESH_GDBOT, { { 3.0f, 0.0f, 0.0f }, FLOAT4_ZERO, FLOAT3_ONE }, 0, true },
				{ MESH_GDBOT, { { 3.0f, 0.0f, -3.0f }, FLOAT4_ZERO, FLOAT3_ONE }, 0, true },
				{ MESH_MAGE, { { -3.0f, 0.0f, 0.0f }, FLOAT4_ZERO, FLOAT3_ONE }, 0, true },

				{ MESH_TERRAIN, { { 0.0f, 0.0f, 0.0f }, FLOAT4_ZERO, FLOAT3_ONE }, 0, true },
			};

			uint64_t scratch_cursor = 0;
			for (uint32_t instance_index = 0; instance_index < countof(instances); ++instance_index) {
				MeshInstance *instance = &instances[instance_index];
				Mesh *mesh = &meshes[instance->id];
				if (mesh->skeleton.bone_count == 0 || animation_counts[instance->id] == 0)
					continue;

				uint32_t instance_anim = (anim_index + instance_index) % animation_counts[instance->id];

				uint64_t matrices_offset = scratch_cursor;
				uint64_t matrices_size = alignup(mesh->skeleton.bone_count * sizeof(float4x4), 256);
				scratch_cursor += matrices_size;

				instance->skinned_vertices_offset = scratch_cursor;
				uint64_t skinned_vertices_size = alignup(mesh->total_vertex_count * sizeof(Vertex3), 256);
				scratch_cursor += skinned_vertices_size;

				Pose pose = anim_pose_sample(frame_arena, &animations[instance->id][instance_anim], fmodf(anim_t, animations[instance->id][instance_anim].duration));
				float4x4 *skin_matrices = anim_pose_skinning_matrices(frame_arena, anim_pose_local_to_model(frame_arena, &pose, &mesh->skeleton), &mesh->skeleton);

				memory_copy(scratch_buffers[context->current_frame_index]->mapped + matrices_offset, skin_matrices, matrices_size);

				{ // :skinning
					vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_skinning.handle);
					struct {
						uint32_t vertex_count;
						uint32_t _pad0;
						uint64_t skinning_matrices_address;
						uint64_t input_address;
						uint64_t skinning_address;
						uint64_t output_address;
					} pc = {
						.vertex_count = mesh->total_vertex_count,
						.skinning_matrices_address = scratch_buffers[context->current_frame_index]->address + matrices_offset,
						.input_address = mesh->buffer->address + mesh->buffer_vertex_byte_offset,
						.skinning_address = mesh->buffer->address + mesh->buffer_skinning_data_byte_offset,
						.output_address = scratch_buffers[context->current_frame_index]->address + instance->skinned_vertices_offset,
					};
					vkCmdPushConstants(command_buffer, pipeline_skinning.layout, VK_SHADER_STAGE_ALL, 0, sizeof(pc), &pc);

					vkCmdDispatch(command_buffer, (mesh->total_vertex_count + 255) / 256, 1, 1);

					gfx_cmd_buffer_barrier(
						cmd,
						RESOURCE_USAGE_COMPUTE_SHADER_WRITE,
						RESOURCE_USAGE_VERTEX_SHADER_READ,
						instance->skinned_vertices_offset,
						skinned_vertices_size, scratch_buffers[context->current_frame_index]);
				}
			}

			float2 mouse_delta = float2_from_double2(input_mouse_delta());
			mouse_delta.x /= dims.x;
			mouse_delta.y /= dims.y;
			scene_camera_orbit(&camera, mouse_delta);

			typedef struct {
				float4x4 view;
				float4x4 proj;
				float4 camera_position;
				float fog_density;
				float fog_gradient;
				float time;
			} FrameData;

			typedef struct {
				float4 position;
				float4 color;
				float4x4 matrix;
			} Light;

			Light lights[] = {
				{ .position = { 0.0f, 20.0f, -30.0f, 1.0f }, (float4){ 1.0f, 1.0f, 1.0f, 1.0f }, float4x4_identity() },
				{ .position = { 0.0f, 20.0f, -30.0f, 1.0f }, (float4){ 1.0f, 1.0f, 1.0f, 1.0f }, float4x4_identity() },
			};
			float ortho_size = 10.0f;
			lights[0].matrix = float4x4_multiply(
				float4x4_orthographic(-ortho_size, ortho_size, -ortho_size, ortho_size, 0.1f, 100.f),
				float4x4_lookat(float3_from_float4(lights[0].position), FLOAT3_ZERO, FLOAT3_Y));

			FrameData frame_data = {
				.fog_density = 0.02f,
				.fog_gradient = 5.0f,
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
				vkCmdBeginRendering(command_buffer, &shadowpass_info);

				VkViewport viewport = {
					.width = extent.width,
					.height = extent.height,
					.minDepth = 0.0f,
					.maxDepth = 1.0f,
				};
				vkCmdSetViewport(command_buffer, 0, 1, &viewport);
				vkCmdSetScissor(command_buffer, 0, 1, &(VkRect2D){ .extent = extent });

				VkDescriptorSet frame_set = 0;
				{ // allocate & write frame set
					frame_data.view = lights[0].matrix;
					frame_data.proj = float4x4_identity();
					frame_data.camera_position = lights[0].position;
					frame_data.proj.elements[5] *= -1;

					memory_copy(scratch_buffers[context->current_frame_index]->mapped + scratch_cursor, &frame_data, sizeof(frame_data));
					uint64_t frame_data_offset = scratch_cursor;
					scratch_cursor += alignup(sizeof(frame_data), 256);

					VkDescriptorSetAllocateInfo alloc_info = {
						.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
						.descriptorPool = descriptor_pool,
						.descriptorSetCount = 1,
						.pSetLayouts = &pipeline_shadow.set_layouts[0],
					};
					vkAllocateDescriptorSets(context->device.logical, &alloc_info, &frame_set);

					{ // frame data
						VkDescriptorBufferInfo buffer_info = {
							.buffer = scratch_buffers[context->current_frame_index]->handle,
							.offset = frame_data_offset,
							.range = sizeof(frame_data),
						};
						VkWriteDescriptorSet write = {
							.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
							.dstSet = frame_set,
							.dstBinding = 0,
							.dstArrayElement = 0,
							.descriptorCount = 1,
							.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
							.pBufferInfo = &buffer_info,
						};
						vkUpdateDescriptorSets(context->device.logical, 1, &write, 0, 0);
					}
				}

				vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_shadow.handle);
				vkCmdBindDescriptorSets(cmd->handle, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_shadow.layout, 0, 1, &frame_set, 0, 0);

				// :draw
				for (uint32_t instance_index = 0; instance_index < countof(instances); ++instance_index) {
					MeshInstance *instance = &instances[instance_index];
					if (instance->cast_shadow == false)
						continue;

					Mesh *mesh = &meshes[instance->id];

					vkCmdBindIndexBuffer(command_buffer, geometry->handle, mesh->buffer_index_byte_offset, VK_INDEX_TYPE_UINT32);
					for (uint32_t part_index = 0; part_index < mesh->part_count; ++part_index) {
						MeshPart *part = &mesh->parts[part_index];
						Material *material = &mesh->materials[part->material_id];

						float4x4 transform = float4x4_compose_quat(
							instance->transform.translation,
							instance->transform.rotation,
							instance->transform.scale //
						);
						struct {
							float4x4 model;
						} pc = { .model = transform };

						VkDescriptorSet draw_set = 0;
						{ // allocate & write draw set
							VkDescriptorSetAllocateInfo alloc_info = {
								.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
								.descriptorPool = descriptor_pool,
								.descriptorSetCount = 1,
								.pSetLayouts = &pipeline_shadow.set_layouts[1],
							};
							vkAllocateDescriptorSets(context->device.logical, &alloc_info, &draw_set);

							{ // geometry descriptor binding
								VkDescriptorBufferInfo buffer_info = {
									.buffer = mesh->buffer->handle,
									.offset = mesh->buffer_vertex_byte_offset,
									.range = mesh->total_vertex_count * sizeof(Vertex3),
								};
								if (mesh->skeleton.bone_count && animation_counts[instance->id]) {
									buffer_info.buffer = scratch_buffers[context->current_frame_index]->handle;
									buffer_info.offset = instance->skinned_vertices_offset;
								}
								VkWriteDescriptorSet write = {
									.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
									.dstSet = draw_set,
									.dstBinding = 0,
									.dstArrayElement = 0,
									.descriptorCount = 1,
									.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
									.pBufferInfo = &buffer_info,
								};
								vkUpdateDescriptorSets(context->device.logical, 1, &write, 0, 0);
							}
						}
						vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_shadow.layout, 1, 1, &draw_set, 0, 0);

						vkCmdPushConstants(cmd->handle, pipeline_3d.layout, VK_SHADER_STAGE_ALL, 0, sizeof(pc), &pc);
						vkCmdDrawIndexed(command_buffer, part->index_count, 1, part->index_offset, part->vertex_offset, 0);
					}
				}

				vkCmdEndRendering(cmd->handle);
			}

			{ // :main
				gfx_cmd_image_barrier(cmd, RESOURCE_USAGE_DEPTH_ATTACHMENT, RESOURCE_USAGE_SHADER_READ, shadow_depthbuffer);

				VkRenderingAttachmentInfo color_attachments[] = {
					{
					  .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
					  .imageView = offscreen_render->view,
					  .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					  .resolveImageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
					  .resolveImageView = main_target.view,
					  .resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT,
					  .clearValue.color = { { 0.03f, 0.03f, 0.03f, 1.0f } },
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
					.width = main_target.width,
					.height = main_target.height,
				};
				VkRenderingInfo renderpass_info = {
					.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
					.renderArea.extent = extent,
					.layerCount = 1,
					.colorAttachmentCount = countof(color_attachments),
					.pColorAttachments = color_attachments,
					.pDepthAttachment = &depth_attachment,
				};
				vkCmdBeginRendering(command_buffer, &renderpass_info);

				VkViewport viewport = {
					.width = extent.width,
					.height = extent.height,
					.minDepth = 0.0f,
					.maxDepth = 1.0f,
				};
				vkCmdSetViewport(command_buffer, 0, 1, &viewport);
				vkCmdSetScissor(command_buffer, 0, 1, &(VkRect2D){ .extent = extent });

				VkDescriptorSet frame_set = 0;
				{ // allocate & write frame set
					frame_data.view = float4x4_lookat(camera.position, camera.target, camera.up);
					frame_data.proj = float4x4_perspective(to_radians(45.f), (float)dims.x / (float)dims.y, 0.1f, 500.f);
					frame_data.camera_position = float4_from_float3(camera.position, 0.0f);

					memory_copy(scratch_buffers[context->current_frame_index]->mapped + scratch_cursor, &frame_data, sizeof(frame_data));
					uint64_t frame_data_offset = scratch_cursor;
					scratch_cursor += alignup(sizeof(frame_data), 256);

					VkDescriptorSetAllocateInfo alloc_info = {
						.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
						.descriptorPool = descriptor_pool,
						.descriptorSetCount = 1,
						.pSetLayouts = &pipeline_3d.set_layouts[0],
					};
					vkAllocateDescriptorSets(context->device.logical, &alloc_info, &frame_set);

					{ // frame data

						VkDescriptorBufferInfo buffer_info = {
							.buffer = scratch_buffers[context->current_frame_index]->handle,
							.offset = frame_data_offset,
							.range = sizeof(frame_data),
						};
						VkWriteDescriptorSet write = {
							.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
							.dstSet = frame_set,
							.dstBinding = 0,
							.dstArrayElement = 0,
							.descriptorCount = 1,
							.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
							.pBufferInfo = &buffer_info,
						};
						vkUpdateDescriptorSets(context->device.logical, 1, &write, 0, 0);
					}
					{ // light data
						memory_copy(scratch_buffers[context->current_frame_index]->mapped + scratch_cursor, lights, sizeof(lights));
						uint64_t light_offset = scratch_cursor;
						scratch_cursor += alignup(sizeof(lights), 256);

						VkDescriptorBufferInfo buffer_info = {
							.buffer = scratch_buffers[context->current_frame_index]->handle,
							.offset = light_offset,
							.range = sizeof(lights),
						};
						VkWriteDescriptorSet write = {
							.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
							.dstSet = frame_set,
							.dstBinding = 1,
							.dstArrayElement = 0,
							.descriptorCount = 1,
							.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
							.pBufferInfo = &buffer_info,
						};
						vkUpdateDescriptorSets(context->device.logical, 1, &write, 0, 0);
					}
					{ // shadow sampler

						VkDescriptorImageInfo image_info = {
							.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
							.imageView = shadow_depthbuffer->view,
							.sampler = shadow_sampler->handle
						};
						VkWriteDescriptorSet write = {
							.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
							.dstSet = frame_set,
							.dstBinding = 2,
							.dstArrayElement = 0,
							.descriptorCount = 1,
							.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
							.pImageInfo = &image_info
						};
						vkUpdateDescriptorSets(context->device.logical, 1, &write, 0, 0);
					}
					{ // skybox sampler
						VkDescriptorImageInfo image_info = {
							.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
							.imageView = skybox.gpu_image->view,
							.sampler = linear_sampler->handle
						};
						VkWriteDescriptorSet write = {
							.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
							.dstSet = frame_set,
							.dstBinding = 3,
							.dstArrayElement = 0,
							.descriptorCount = 1,
							.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
							.pImageInfo = &image_info
						};
						vkUpdateDescriptorSets(context->device.logical, 1, &write, 0, 0);
					}
				}

				vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_3d.handle);
				vkCmdBindDescriptorSets(cmd->handle, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_3d.layout, 0, 1, &frame_set, 0, 0);

				// :draw
				for (uint32_t instance_index = 0; instance_index < countof(instances); ++instance_index) {
					MeshInstance *instance = &instances[instance_index];
					Mesh *mesh = &meshes[instance->id];

					vkCmdBindIndexBuffer(command_buffer, geometry->handle, mesh->buffer_index_byte_offset, VK_INDEX_TYPE_UINT32);
					for (uint32_t part_index = 0; part_index < mesh->part_count; ++part_index) {
						MeshPart *part = &mesh->parts[part_index];
						Material *material = &mesh->materials[part->material_id];

						float4x4 transform = float4x4_compose_quat(
							instance->transform.translation,
							instance->transform.rotation,
							instance->transform.scale //
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

						if (instance->id == MESH_TERRAIN) {
							pc.uv_scale.x *= 4.f;
							pc.uv_scale.y *= 4.f;
						}

						if (instance->id == MESH_HERO_MALE && part_index == 3) { // hero head
							if (blink_timer > 2.0f && blink_timer < 2.1f)
								pc.uv_offset.x = 1.0f / 3.0f;
							else if (blink_timer > 2.1f && blink_timer < 2.3f) {
								pc.uv_offset.x = 2.0f / 3.0f;
							} else if (blink_timer >= 2.3f) {
								blink_timer = 0.0f;
							}
						}

						VkDescriptorSet draw_set = 0;
						{ // allocate & write draw set
							VkDescriptorSetAllocateInfo alloc_info = {
								.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
								.descriptorPool = descriptor_pool,
								.descriptorSetCount = 1,
								.pSetLayouts = &pipeline_3d.set_layouts[1],
							};
							vkAllocateDescriptorSets(context->device.logical, &alloc_info, &draw_set);

							{ // geometry descriptor binding
								VkDescriptorBufferInfo buffer_info = {
									.buffer = mesh->buffer->handle,
									.offset = mesh->buffer_vertex_byte_offset,
									.range = mesh->total_vertex_count * sizeof(Vertex3),
								};
								if (mesh->skeleton.bone_count && animation_counts[instance->id]) {
									buffer_info.buffer = scratch_buffers[context->current_frame_index]->handle;
									buffer_info.offset = instance->skinned_vertices_offset;
								}
								VkWriteDescriptorSet write = {
									.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
									.dstSet = draw_set,
									.dstBinding = 0,
									.dstArrayElement = 0,
									.descriptorCount = 1,
									.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
									.pBufferInfo = &buffer_info,
								};
								vkUpdateDescriptorSets(context->device.logical, 1, &write, 0, 0);
							}
							{ // material textures descriptor binding
								VkWriteDescriptorSet writes[5] = { 0 };
								VkDescriptorImageInfo image_infos[5] = { 0 };
								for (uint32_t texture_index = 0; texture_index < 5; ++texture_index) {
									GFX_Image *image = material->textures[texture_index].gpu_image;
									image_infos[texture_index] = (VkDescriptorImageInfo){
										.imageView = image->view,
										.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
										.sampler = linear_sampler->handle,
									};

									writes[texture_index] = (VkWriteDescriptorSet){
										.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
										.dstSet = draw_set,
										.dstBinding = 1,
										.dstArrayElement = texture_index,
										.descriptorCount = 1,
										.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
										.pImageInfo = &image_infos[texture_index],
									};
								}
								vkUpdateDescriptorSets(context->device.logical, countof(writes), writes, 0, 0);
							}
						}
						vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_3d.layout, 1, 1, &draw_set, 0, 0);

						vkCmdPushConstants(cmd->handle, pipeline_3d.layout, VK_SHADER_STAGE_ALL, 0, sizeof(pc), &pc);
						vkCmdDrawIndexed(command_buffer, part->index_count, 1, part->index_offset, part->vertex_offset, 0);
					}
				}

				// :grass
				{
					Mesh *mesh = &meshes[MESH_GRASS_BILLBOARD];

					vkCmdBindPipeline(cmd->handle, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_grass.handle);
					vkCmdBindIndexBuffer(command_buffer, geometry->handle, mesh->buffer_index_byte_offset, VK_INDEX_TYPE_UINT32);

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
						} pc = {
							.model = float4x4_identity(),
							.base_color = material->tint,
							.emissive = FLOAT4_ONE,
							.metallic_roughness = { 0.0f, 0.5f },
							.uv_offset = { 0.0f, 0.0f },
							.uv_scale = { 1.0f, 1.0f },
						};

						VkDescriptorSet draw_set = 0;
						{ // allocate & write draw set
							VkDescriptorSetAllocateInfo alloc_info = {
								.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
								.descriptorPool = descriptor_pool,
								.descriptorSetCount = 1,
								.pSetLayouts = &pipeline_grass.set_layouts[1],
							};
							vkAllocateDescriptorSets(context->device.logical, &alloc_info, &draw_set);

							{ // geometry descriptor binding
								VkDescriptorBufferInfo buffer_info = {
									.buffer = mesh->buffer->handle,
									.offset = mesh->buffer_vertex_byte_offset,
									.range = mesh->total_vertex_count * sizeof(Vertex3),
								};
								VkWriteDescriptorSet write = {
									.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
									.dstSet = draw_set,
									.dstBinding = 0,
									.dstArrayElement = 0,
									.descriptorCount = 1,
									.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
									.pBufferInfo = &buffer_info,
								};
								vkUpdateDescriptorSets(context->device.logical, 1, &write, 0, 0);
							}
							{ // material textures descriptor binding
								VkWriteDescriptorSet writes[5] = { 0 };
								VkDescriptorImageInfo image_infos[5] = { 0 };
								for (uint32_t texture_index = 0; texture_index < 5; ++texture_index) {
									GFX_Image *image = material->textures[texture_index].gpu_image;
									image_infos[texture_index] = (VkDescriptorImageInfo){
										.imageView = image->view,
										.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
										.sampler = linear_sampler->handle,
									};

									writes[texture_index] = (VkWriteDescriptorSet){
										.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
										.dstSet = draw_set,
										.dstBinding = 1,
										.dstArrayElement = texture_index,
										.descriptorCount = 1,
										.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
										.pImageInfo = &image_infos[texture_index],
									};
								}
								vkUpdateDescriptorSets(context->device.logical, countof(writes), writes, 0, 0);
							}
							{ // instance descriptor binding
								VkDescriptorBufferInfo buffer_info = {
									.buffer = grass_instancing_buffer->handle,
									.offset = 0,
									.range = grass_instancing_buffer->size,
								};
								VkWriteDescriptorSet write = {
									.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
									.dstSet = draw_set,
									.dstBinding = 2,
									.dstArrayElement = 0,
									.descriptorCount = 1,
									.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
									.pBufferInfo = &buffer_info,
								};
								vkUpdateDescriptorSets(context->device.logical, 1, &write, 0, 0);
							}
						}
						vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_grass.layout, 1, 1, &draw_set, 0, 0);

						vkCmdPushConstants(cmd->handle, pipeline_grass.layout, VK_SHADER_STAGE_ALL, 0, sizeof(pc), &pc);
						vkCmdDrawIndexed(command_buffer, part->index_count, map_width * map_depth, part->index_offset, part->vertex_offset, 0);
					}
				}

				vkCmdBindPipeline(cmd->handle, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_skybox.handle);
				vkCmdDraw(cmd->handle, 36, 1, 0, 0);

				vkCmdEndRendering(command_buffer);
			}
			gfx_cmd_image_barrier(
				cmd,
				RESOURCE_USAGE_COLOR_ATTACHMENT,
				RESOURCE_USAGE_PRESENT,
				&main_target);

		} else // TODO: resize swapchain
			break;

		if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
			LOG_INFO("failed to record command buffer.");
			break;
		}

		VkSwapchainKHR swapchain_handles[MAX_SWAPCHAINS] = { 0 };
		VkSemaphore wait_semaphores[MAX_SWAPCHAINS] = { 0 };
		VkSemaphore signal_semaphores[MAX_SWAPCHAINS] = { 0 };
		VkPipelineStageFlags wait_stages[MAX_SWAPCHAINS] = {
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		};

		for (uint32_t swapchain_index = 0; swapchain_index < cmd->swapchain_count; ++swapchain_index) {
			swapchain_handles[swapchain_index] = cmd->swapchains[swapchain_index]->handle;
			wait_semaphores[swapchain_index] = cmd->swapchains[swapchain_index]->image_available_semaphores[cmd->frame_index];
			signal_semaphores[swapchain_index] = cmd->swapchains[swapchain_index]->render_done_semaphores[cmd->swapchain_image_indices[swapchain_index]];
		}

		VkSubmitInfo submit_info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = cmd->swapchain_count,
			.pWaitSemaphores = wait_semaphores,
			.pWaitDstStageMask = wait_stages,
			.commandBufferCount = 1,
			.pCommandBuffers = &command_buffer,
			.signalSemaphoreCount = cmd->swapchain_count,
			.pSignalSemaphores = signal_semaphores,
		};

		if (vkQueueSubmit(context->graphics_queue, 1, &submit_info, context->cmd_buffers[context->current_frame_index].in_flight_fence) != VK_SUCCESS) {
			LOG_ERROR("failed to submit command buffer to queue.");
			break;
		}

		VkPresentInfoKHR present_info = {
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = cmd->swapchain_count,
			.pWaitSemaphores = signal_semaphores,
			.swapchainCount = cmd->swapchain_count,
			.pSwapchains = swapchain_handles,
			.pImageIndices = cmd->swapchain_image_indices,
		};
		VkResult result = vkQueuePresentKHR(context->present_queue, &present_info);
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
			break;

		OS_Timestamp current_ts = os_file_last_modified(s("assets/shaders/compute/bin/test.compute.spv"));
		if (compute_ts != current_ts) {
			LOG_INFO("hot-reloading...");
			vkDeviceWaitIdle(context->device.logical); // TODO: Proper synchronization for hot-reload
			pipeline_destroy(context, &c_pipeline);

			ArenaTemp scratch = arena_scratch_begin(NULL);
			String8 compute_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/compute/bin/test.compute.spv"));
			c_pipeline = compute_pipeline_make(context, compute_bytecode, &test_compute_descriptor_layout, 1);
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

		memory_zero(pipeline_skybox.set_layouts, sizeof(pipeline_skybox.set_layouts)); // destroyed by pipeline_3d
		pipeline_destroy(context, &pipeline_skybox);

		memory_zero(&pipeline_grass.set_layouts[0], sizeof(pipeline_grass.set_layouts[0]));
		pipeline_destroy(context, &pipeline_grass);
	}

	gfx_shutdown(context);

	os_surface_close(main_render);
	os_surface_close(popup_compute);
	os_display_shutdown();
	return 0;
}

// EXTENSIONS
static VKAPI_ATTR VkResult VKAPI_CALL STUB_vkCreateDebugUtilsMessenger(VkInstance i, const VkDebugUtilsMessengerCreateInfoEXT *c, const VkAllocationCallbacks *a, VkDebugUtilsMessengerEXT *m) { return VK_ERROR_EXTENSION_NOT_PRESENT; }
static VKAPI_ATTR void VKAPI_CALL STUB_vkDestroyDebugUtilsMessenger(VkInstance i, VkDebugUtilsMessengerEXT m, const VkAllocationCallbacks *a) {}
static VKAPI_ATTR VkResult VKAPI_CALL STUB_vkSetDebugUtilsObjectName(VkDevice d, const VkDebugUtilsObjectNameInfoEXT *n) { return VK_SUCCESS; }
static VKAPI_ATTR void VKAPI_CALL STUB_vkCmdBeginDebugUtilsLabel(VkCommandBuffer c, const VkDebugUtilsLabelEXT *l) {}
static VKAPI_ATTR void VKAPI_CALL STUB_vkCmdEndDebugUtilsLabel(VkCommandBuffer c) {}

PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessenger = STUB_vkCreateDebugUtilsMessenger;
PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessenger = STUB_vkDestroyDebugUtilsMessenger;
PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectName = STUB_vkSetDebugUtilsObjectName;
PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabel = STUB_vkCmdBeginDebugUtilsLabel;
PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabel = STUB_vkCmdEndDebugUtilsLabel;

VkImageUsageFlags gfx__image_opt_to_vk_usage(ImageOptions opt) {
	VkImageUsageFlags result = 0;
	if (FLAG_GET(opt.usage, IMAGE_USAGE_RENDER)) {
		if (pixel_format_is_depth(opt.format))
			result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		else
			result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}

	if (FLAG_GET(opt.usage, IMAGE_USAGE_SAMPLE))
		result |= VK_IMAGE_USAGE_SAMPLED_BIT;

	if (FLAG_GET(opt.usage, IMAGE_USAGE_STORAGE))
		result |= VK_IMAGE_USAGE_STORAGE_BIT;

	if (FLAG_GET(opt.usage, IMAGE_USAGE_TRANSFER))
		result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

	if (result == 0) { // default
		result |= VK_IMAGE_USAGE_SAMPLED_BIT;
		result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}

	return result;
}

VkBufferUsageFlags gfx__to_vk_buffer_usage(BufferUsage usage) {
	VkBufferUsageFlags result = 0;
	if (FLAG_GET(usage, BUFFER_USAGE_UNIFORM))
		result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	if (FLAG_GET(usage, BUFFER_USAGE_STORAGE))
		result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	if (FLAG_GET(usage, BUFFER_USAGE_VERTEX))
		result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	if (FLAG_GET(usage, BUFFER_USAGE_INDEX))
		result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	if (FLAG_GET(usage, BUFFER_USAGE_TRANSFER))
		result |= VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	return result;
}

VkMemoryPropertyFlags gfx__memory_type_to_vk_memory_property_flags(BufferMemory memory) {
	switch (memory) {
		case BUFFER_MEMORY_LOCAL:
			return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		case BUFFER_MEMORY_SHARED:
			return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	}

	ASSERT(false);
	return 0;
}

VkFormat gfx__pixel_format_to_vk_format(PixelFormat format) {
	switch (format) {
		case PIXELFORMAT_RGBA8_UNORM:
			return VK_FORMAT_R8G8B8A8_UNORM;
		case PIXELFORMAT_RGBA8_SRGB:
			return VK_FORMAT_R8G8B8A8_SRGB;
		case PIXELFORMAT_RGBA16_FLOAT:
			return VK_FORMAT_R16G16B16A16_SFLOAT;
		case PIXELFORMAT_R32_FLOAT:
			return VK_FORMAT_R32_SFLOAT;
		case PIXELFORMAT_DEPTH:
			return VK_FORMAT_D32_SFLOAT;
		case PIXELFORMAT_DEPTH_STENCIL:
			return VK_FORMAT_D24_UNORM_S8_UINT;
		case PIXELFORMAT_BACKBUFFER:
			return VK_FORMAT_B8G8R8A8_SRGB;
	}

	return VK_FORMAT_UNDEFINED;
}

VkImageAspectFlags gfx__pixel_format_to_vk_aspect(PixelFormat format) {
	if (pixel_format_is_depth_stencil(format))
		return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	if (pixel_format_is_depth(format))
		return VK_IMAGE_ASPECT_DEPTH_BIT;

	return VK_IMAGE_ASPECT_COLOR_BIT;
}

uint32_t gfx__pixel_format_to_stride(PixelFormat format) {
	switch (format) {
		case PIXELFORMAT_RGBA8_UNORM:
			return 4;
		case PIXELFORMAT_RGBA8_SRGB:
			return 4;
		case PIXELFORMAT_RGBA16_FLOAT:
			return 2 * 4;
		case PIXELFORMAT_R32_FLOAT:
			return 4;
		case PIXELFORMAT_DEPTH:
			return 4;
		case PIXELFORMAT_DEPTH_STENCIL:
			return 4;
		case PIXELFORMAT_BACKBUFFER:
			return 4;
			break;
	}
	if (pixel_format_is_depth(format))
		return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

	return VK_IMAGE_ASPECT_COLOR_BIT;
}

VkSampleCountFlags gfx__usage_to_vk_sample(VkPhysicalDeviceLimits limits, ImageOptions options) {
	VkSampleCountFlags result = MAX((VkSampleCountFlags)options.sample, VK_SAMPLE_COUNT_1_BIT);

	if (FLAG_GET(options.usage, IMAGE_USAGE_SAMPLE)) {
		if (pixel_format_is_depth(options.format)) {
			result = MIN(result, limits.sampledImageDepthSampleCounts);
			result = MIN(result, limits.sampledImageStencilSampleCounts);
		} else {
			result = MIN(result, limits.sampledImageColorSampleCounts);
		}
	}

	if (FLAG_GET(options.usage, IMAGE_USAGE_RENDER)) {
		if (pixel_format_is_depth(options.format)) {
			result = MIN(result, limits.framebufferDepthSampleCounts);
			result = MIN(result, limits.framebufferStencilSampleCounts);
		} else
			result = MIN(result, limits.framebufferColorSampleCounts);
	}

	if (FLAG_GET(options.usage, IMAGE_USAGE_STORAGE))
		result = MIN(result, limits.storageImageSampleCounts);

	if (result < options.sample)
		LOG_WARN("requested sample size of [%u] is too large, clamping to [%u]", options.sample, result);

	return result;
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

	if (FLAG_GET(opt.usage, IMAGE_USAGE_SAMPLE))
		result = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	if (FLAG_GET(opt.usage, IMAGE_USAGE_RENDER))
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

GFX_Buffer *gfx_buffer_make(GFX_Context *context, uint64_t size, BufferMemory memory, BufferUsage usage, const char *debug_name) {
	GFX_Buffer *result = 0;
	LOG_DEBUG("creating vulkan buffer.");

	bool ok = context && (context->buffer_count < MAX_BUFFERS || context->first_free_buffer);
	const char *name = debug_name ? debug_name : "<unnamed_buffer>";

	if (ok) { // acquire new buffer
		if (context->first_free_buffer) {
			result = context->first_free_buffer;
			context->first_free_buffer = result->next;

			memory_zero(result, sizeof(*result));
		} else
			result = &context->buffers[context->buffer_count];

		context->buffer_count++;
	}

	VkBufferUsageFlags vk_usage = 0;
	VkMemoryPropertyFlags memory_flags = 0;
	if (ok) {
		result->memory_type = memory;
		result->usage = usage;

		vk_usage = gfx__to_vk_buffer_usage(usage);
		memory_flags = gfx__memory_type_to_vk_memory_property_flags(memory);

		ok = vk_usage && memory_flags;
		if (ok == false)
			LOG_WARN("invalid properties passed, aborting creation of %s", name);
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

GFX_Image *gfx_image_make(GFX_Context *context, uint32_t width, uint32_t height, ImageOptions opt) {
	GFX_Image *result = 0;
	LOG_DEBUG("creating vulkan image.");

	bool ok = context && (context->buffer_count < MAX_BUFFERS || context->first_free_buffer);
	const char *name = opt.debug_name ? opt.debug_name : "<unnamed_image>";

	if (ok) { // acquire new image
		if (context->first_free_image) {
			result = context->first_free_image;
			context->first_free_image = result->next;
		} else
			result = &context->images[context->image_count];

		context->image_count++;
	}

	uint32_t layer_count = opt.slice_count
		? opt.slice_count
		: opt.type == IMAGE_TYPE_CUBE ? 6
									  : 1;

	if (ok) { // make vulkan image handle

		VkImageUsageFlags vk_usage = 0;
		VkFormat vk_format = 0;
		VkSampleCountFlags vk_sample = 0;
		VkImageCreateFlags vk_flags = 0;

		vk_usage = gfx__image_opt_to_vk_usage(opt);
		vk_format = gfx__pixel_format_to_vk_format(opt.format);
		vk_sample = gfx__usage_to_vk_sample(context->device.limits, opt);
		vk_flags = gfx__opt_to_vk_image_flags(opt);

		result->width = width, result->height = height;
		result->format = opt.format;
		result->usage = RESOURCE_USAGE_UNDEFINED;

		result->image_info = (VkImageCreateInfo){
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.flags = vk_flags,
			.format = vk_format,
			.extent = {
			  .width = width,
			  .height = height,
			  .depth = 1,
			},
			.mipLevels = 1,
			.arrayLayers = layer_count,
			.samples = vk_sample,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = vk_usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.initialLayout = gfx__usage_to_image_layout(result->usage),
		};

		ok = vkCreateImage(context->device.logical, &result->image_info, 0, &result->handle) == VK_SUCCESS;
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
		VkImageViewType vk_type = gfx__opt_to_vk_view_type(opt);
		VkImageAspectFlags vk_aspect = gfx__pixel_format_to_vk_aspect(opt.format);

		result->view_info = (VkImageViewCreateInfo){
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = result->handle,
			.viewType = vk_type,
			.format = result->image_info.format,
			.components = {
			  .r = VK_COMPONENT_SWIZZLE_IDENTITY,
			  .g = VK_COMPONENT_SWIZZLE_IDENTITY,
			  .b = VK_COMPONENT_SWIZZLE_IDENTITY,
			  .a = VK_COMPONENT_SWIZZLE_IDENTITY,
			},
			.subresourceRange = {
			  .aspectMask = vk_aspect,
			  .baseMipLevel = 0,
			  .levelCount = 1,
			  .baseArrayLayer = 0,
			  .layerCount = layer_count,
			}
		};

		ok = vkCreateImageView(context->device.logical, &result->view_info, 0, &result->view) == VK_SUCCESS;
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

	bool ok = context && (context->buffer_count < MAX_BUFFERS || context->first_free_buffer);
	const char *name = opt.debug_name ? opt.debug_name : "<unnamed_sampler>";

	if (ok) { // acquire new sampler
		if (context->first_free_sampler) {
			result = context->first_free_sampler;
			context->first_free_sampler = result->next;

			memory_zero(result, sizeof(*result));
		} else
			result = &context->samplers[context->sampler_count];

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
		};

		ok = vkCreateSampler(context->device.logical, &result->info, NULL, &result->handle) == VK_SUCCESS;
		if (ok == false)
			LOG_WARN("failed to create vulkan sampler.");
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

	bool ok = context && (context->swapchain_count < MAX_BUFFERS || context->first_free_swapchain);
	const char *name = debug_name ? debug_name : "<unnamed_swapchain";

	if (ok) { // acquire new swapchain
		if (context->first_free_swapchain) {
			result = context->first_free_swapchain;
			context->first_free_swapchain = result->next;

			memory_zero(result, sizeof(*result));
		} else
			result = &context->swapchains[context->swapchain_count];

		context->swapchain_count++;
	}

	if (ok) { // create surface
		VkXcbSurfaceCreateInfoKHR surface_create_info = {
			.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
			.connection = os_native_display_handle(),
			.window = (uint32_t)(uint64_t)os_native_surface_handle(surface),
		}; // TODO: Have os decide this

		ok = vkCreateXcbSurfaceKHR(context->instance, &surface_create_info, 0, &result->surface) == VK_SUCCESS;
		if (ok == false)
			LOG_WARN("failed to create vulkan surface.");
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
		swapchain_destroy(context, result);
		LOG_ERROR("failed to create graphics surface.");
	}

	arena_scratch_end(scratch);
	return result;
}

Shader compute_pipeline_make(GFX_Context *context, String8 compute_bytecode, VkDescriptorSetLayout *layouts, uint32_t layout_count) {
	LOG_DEBUG("creating vulkan compute pipeline.");
	Shader result = { 0 };

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
		if (ok == false)
			LOG_WARN("failed to create compute pipeline shader module.");
	}

	if (ok) // create descriptor set layouts
		memory_copy(result.set_layouts, layouts, MIN(countof(result.set_layouts), layout_count) * sizeof(VkDescriptorSetLayout));

	if (ok) { // create pipeline layout
		VkPipelineLayoutCreateInfo pl_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = layout_count,
			.pSetLayouts = result.set_layouts,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = &context->global_range,
		};

		ok = vkCreatePipelineLayout(context->device.logical, &pl_create_info, NULL, &result.layout) == VK_SUCCESS;
		if (ok == false)
			LOG_WARN("failed to create compute pipeline layout.");
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
		if (ok == false)
			LOG_WARN("failed to create compute pipeline.");
	}

	if (ok == false) // remove half-made resources on error
		pipeline_destroy(context, &result);

	return result;
}

Shader graphics_pipeline_make(
	GFX_Context *context,
	String8 vertex_bytecode, String8 fragment_bytecode,
	VkDescriptorSetLayout *layouts, uint32_t layout_count,
	PipelineOptions opt //
) {
	LOG_DEBUG("creating vulkan grahpics pipeline.");
	Shader result = { 0 };

	bool ok = true;

	if (ok) { // check validitiy of shader code
		ok &= vertex_bytecode.text && vertex_bytecode.length > 0;
		ok &= fragment_bytecode.text && fragment_bytecode.length > 0;

		if (ok == false)
			LOG_WARN("invalid shader bytecode passed.");
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
		if (ok == false)
			LOG_WARN("failed to create vertex/fragment shader module.");
	}

	if (ok) // create descriptor set layouts
		memory_copy(result.set_layouts, layouts, MIN(layout_count, countof(result.set_layouts)) * sizeof(VkDescriptorSetLayout));

	if (ok) { // create pipeline layout
		VkPipelineLayoutCreateInfo pl_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = MIN(layout_count, countof(result.set_layouts)),
			.pSetLayouts = result.set_layouts,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = &context->global_range,
		};

		ok = vkCreatePipelineLayout(context->device.logical, &pl_create_info, NULL, &result.layout) == VK_SUCCESS;
		if (ok == false)
			LOG_WARN("failed to create pipeline layout.");
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
			.cullMode = opt.cull_mode,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
			.depthBiasEnable = VK_FALSE,
		};

		VkPipelineMultisampleStateCreateInfo mss_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.sampleShadingEnable = VK_FALSE,
			.rasterizationSamples = opt.sample_count ? (VkSampleCountFlags)opt.sample_count : VK_SAMPLE_COUNT_1_BIT,
			.minSampleShading = 1.0f,
		};

		VkPipelineDepthStencilStateCreateInfo depth_stencil_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
			.depthTestEnable = VK_TRUE,
			.depthWriteEnable = VK_TRUE,
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
			for (uint32_t index = 0; index < opt.color_attachment_count; ++index)
				memory_copy(color_attachment_blends + index, &color_attachment_blend_default, sizeof(VkPipelineColorBlendAttachmentState));

			cbs_create_info = (VkPipelineColorBlendStateCreateInfo){
				.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
				.logicOpEnable = VK_FALSE,
				.attachmentCount = opt.color_attachment_count,
				.pAttachments = color_attachment_blends,
			};

			for (uint32_t attachment_index = 0; attachment_index < opt.color_attachment_count; ++attachment_index)
				color_attachment_formats[attachment_index] = gfx__pixel_format_to_vk_format(opt.color_attachments[attachment_index]);

			r_create_info = (VkPipelineRenderingCreateInfo){
				.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
				.colorAttachmentCount = opt.color_attachment_count,
				.pColorAttachmentFormats = color_attachment_formats,
				.depthAttachmentFormat = pixel_format_is_depth_stencil(opt.depth_attachment) ? gfx__pixel_format_to_vk_format(opt.depth_attachment) : VK_FORMAT_D32_SFLOAT,
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
		if (ok == false)
			LOG_WARN("failed to create compute pipeline.");
	}

	if (ok == false) // remove half-made resources on error
		pipeline_destroy(context, &result);

	return result;
}

bool gfx_buffer_destroy(GFX_Context *context, GFX_Buffer *buffer) {
	bool ok = context && buffer;
	ok &= buffer >= context->buffers && buffer < context->buffers + MAX_BUFFERS; // valid pool member

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
	ok &= image >= context->images && image < context->images + MAX_IMAGES; // valid pool member

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
	ok &= sampler >= context->samplers && sampler < context->samplers + MAX_SAMPLERS; // valid pool member

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

bool swapchain_destroy(GFX_Context *context, GFX_Swapchain *swapchain) {
	bool ok = context && swapchain;
	ok &= swapchain >= context->swapchains && swapchain < context->swapchains + MAX_SWAPCHAINS; // valid pool member

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

void pipeline_destroy(GFX_Context *context, Shader *pipeline) {
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

GFX_Image gfx_swapchain_backbuffer(GFX_Context *context, GFX_CommandContext *cmd, GFX_Swapchain *surface) {
	GFX_Image result = { 0 };
	bool ok = true;

	uint32_t swapchain_index = cmd->swapchain_count;
	uint32_t image_index = -1;
	if (ok) { // acquire swapchain image
		VkResult result = vkAcquireNextImageKHR(
			context->device.logical,
			surface->handle,
			UINT64_MAX,
			surface->image_available_semaphores[context->current_frame_index],
			VK_NULL_HANDLE, &cmd->swapchain_image_indices[swapchain_index]);

		image_index = cmd->swapchain_image_indices[swapchain_index];

		ok = (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) && image_index != (uint32_t)-1;
	}

	if (ok) { // wrap swapchain
		cmd->swapchain_count++;
		cmd->swapchains[swapchain_index] = surface;

		result.handle = surface->images[image_index];
		result.view = surface->views[image_index];
		result.memory = 0; // not managed
		result.image_info = (VkImageCreateInfo){
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = surface->info.imageFormat,
			.extent = {
			  .width = surface->info.imageExtent.width,
			  .height = surface->info.imageExtent.height,
			  .depth = 1,
			},
			.mipLevels = 0,
			.arrayLayers = surface->info.imageArrayLayers,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = surface->info.imageUsage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,

			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		};
		result.view_info = surface->view_infos[image_index];
		result.width = result.image_info.extent.width;
		result.height = result.image_info.extent.height;
	}

	if (ok == false)
		memory_zero(&result, sizeof(result));

	return result;
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
		context->buffers = arena_push_count(context->arena, GFX_Buffer, MAX_BUFFERS);
		context->images = arena_push_count(context->arena, GFX_Image, MAX_IMAGES);
		context->samplers = arena_push_count(context->arena, GFX_Sampler, MAX_SAMPLERS);
		context->shaders = arena_push_count(context->arena, Shader, MAX_SHADERS);
		context->swapchains = arena_push_count(context->arena, GFX_Swapchain, MAX_SWAPCHAINS);
	}

	if (ok) {
		context->staging_buffer_frame_size = MiB(256);
		context->staging_buffer =
			gfx_buffer_make(
				context,
				context->staging_buffer_frame_size * MAX_FRAMES_IN_FLIGHT,
				BUFFER_MEMORY_SHARED,
				BUFFER_USAGE_TRANSFER, "staging_buffer");
		ok = context->staging_buffer;
		if (ok == false) {
			LOG_ERROR("failed to create context staging buffer.");
		}
	}
	if (ok)
		vkMapMemory(context->device.logical, context->staging_buffer->memory, 0, context->staging_buffer->size, 0, (void **)&context->staging_buffer->mapped);

	if (ok) {
		context->initialized = true;
	} else
		LOG_WARN("failed to initialize vulkan context.");

	return ok;
}

void gfx_shutdown(GFX_Context *context) {
	for (uint32_t index = 0; index < MAX_BUFFERS; ++index) {
		GFX_Buffer *buffer = &context->buffers[index];
		gfx_buffer_destroy(context, buffer);

		if (context->buffer_count == 0)
			break;
	}

	for (uint32_t index = 0; index < MAX_IMAGES; ++index) {
		GFX_Image *image = &context->images[index];
		gfx_image_destroy(context, image);

		if (context->image_count == 0)
			break;
	}

	for (uint32_t index = 0; index < MAX_SAMPLERS; ++index) {
		GFX_Sampler *sampler = &context->samplers[index];
		gfx_sampler_destroy(context, sampler);

		if (context->sampler_count == 0)
			break;
	}

	for (uint32_t index = 0; index < MAX_SWAPCHAINS; ++index) {
		GFX_Swapchain *swapchain = &context->swapchains[index];
		swapchain_destroy(context, swapchain);

		if (context->swapchain_count == 0)
			break;
	}

	for (uint32_t index = 0; index < countof(context->cmd_buffers); ++index) {
		GFX_CommandContext *cmd_buffer = &context->cmd_buffers[index];

		if (cmd_buffer->descriptor_pool)
			vkDestroyDescriptorPool(context->device.logical, cmd_buffer->descriptor_pool, 0);
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
	}

	ASSERT_FORMAT(false, "unhandled/invalid ResourceUsage [%u] in %s.", usage, __func__);
	return 0;
}

VkImageLayout gfx__usage_to_image_layout(ResourceUsage usage) {
	switch (usage) {
		case RESOURCE_USAGE_UNDEFINED:
			return VK_IMAGE_LAYOUT_UNDEFINED;

		case RESOURCE_USAGE_TRANSFER_SRC:
			return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		case RESOURCE_USAGE_TRANSFER_DST:
			return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

		case RESOURCE_USAGE_SHADER_READ:
		case RESOURCE_USAGE_VERTEX_SHADER_READ:
		case RESOURCE_USAGE_FRAGMENT_SHADER_READ:
		case RESOURCE_USAGE_COMPUTE_SHADER_READ:
			return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		case RESOURCE_USAGE_SHADER_WRITE:
		case RESOURCE_USAGE_VERTEX_SHADER_WRITE:
		case RESOURCE_USAGE_FRAGMENT_SHADER_WRITE:
		case RESOURCE_USAGE_COMPUTE_SHADER_WRITE:
			return VK_IMAGE_LAYOUT_GENERAL;

		case RESOURCE_USAGE_COLOR_ATTACHMENT:
			return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		case RESOURCE_USAGE_DEPTH_ATTACHMENT:
			return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		case RESOURCE_USAGE_PRESENT:
			return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		case RESOURCE_USAGE_VERTEX_BUFFER:
		case RESOURCE_USAGE_INDEX_BUFFER:
			break;
	}

	ASSERT_FORMAT(false, "unhandled/invalid ResourceUsage [%u] in %s.", usage, __func__);
	return 0;
}

GFX_CommandContext *gfx_frame_begin(GFX_Context *context) {
	GFX_CommandContext *result = 0;

	bool ok = context;

	if (ok) {
		result = &context->cmd_buffers[context->current_frame_index];
		result->frame_index = context->current_frame_index;
		memory_zero_array(result->swapchains);
		memory_zero_array(result->swapchain_image_indices);
		result->swapchain_count = 0;

		// Wait for frame resource availability
		vkWaitForFences(context->device.logical, 1, &result->in_flight_fence, VK_TRUE, UINT64_MAX);

		// reset frame resources
		vkResetCommandBuffer(result->handle, 0);
		vkResetFences(context->device.logical, 1, &result->in_flight_fence);
		vkResetDescriptorPool(context->device.logical, result->descriptor_pool, 0);

		result->transient_buffer = context->staging_buffer;
		result->transient_arena[0] = arena_wrap(
			context->staging_buffer->mapped + context->current_frame_index * context->staging_buffer_frame_size,
			context->staging_buffer_frame_size);

		// Begin command recording
		VkCommandBufferBeginInfo cb_begin_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		};

		ok = vkBeginCommandBuffer(result->handle, &cb_begin_info) == VK_SUCCESS;

		if (ok == false)
			LOG_WARN("failed to begin command buffer recording.");
	}

	if (ok == false)
		result = 0;

	return result;
}

/* bool gfx_frame_end(GFX_Context *context, GFX_CommandContext *cmd) { */
/* 	VkSemaphore wait_semaphores[MAX_SWAPCHAINS] = { 0 }; */
/* 	VkSemaphore signal_semaphores[MAX_SWAPCHAINS] = { 0 }; */
/* 	VkPipelineStageFlags wait_stages[MAX_SWAPCHAINS] = { 0 }; */
/* 	VkSwapchainKHR swapchain_handles[MAX_SWAPCHAINS] = { 0 }; */

/* 	bool ok = context && cmd; */

/* 	if (ok) { */
/* 		for (uint32_t swapchain_index = 0; swapchain_index < cmd->swapchain_count; ++swapchain_index) { */
/* 			wait_semaphores[swapchain_index] = cmd->swapchains[swapchain_index]->image_available_semaphores[cmd->frame_index]; */
/* 			signal_semaphores[swapchain_index] = cmd->swapchains[swapchain_index]->render_done_semaphores[cmd->swapchain_image_indices[swapchain_index]]; */
/* 			swapchain_handles[swapchain_index] = cmd->swapchains[swapchain_index]->handle; */
/* 			wait_stages[swapchain_index] = VK_PIPELINE_STAGE_TRANSFER_BIT; */

/* 			gfx_cmd_image_transition(cmd, RESOURCE_USAGE_PRESENT, &cmd->swapchain_images[swapchain_index]); */
/* 		} */

/* 		VkSubmitInfo submit_info = { */
/* 			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, */
/* 			.waitSemaphoreCount = countof(wait_semaphores), */
/* 			.pWaitSemaphores = wait_semaphores, */
/* 			.pWaitDstStageMask = wait_stages, */
/* 			.commandBufferCount = 1, */
/* 			.pCommandBuffers = &cmd->handle, */
/* 			.signalSemaphoreCount = countof(signal_semaphores), */
/* 			.pSignalSemaphores = signal_semaphores, */
/* 		}; */

/* 		ok = vkQueueSubmit(context->graphics_queue, 1, &submit_info, context->cmd_buffers[cmd->frame_index].in_flight_fence) == VK_SUCCESS; */
/* 		if (ok == false) */
/* 			LOG_ERROR("failed to submit command buffer to queue."); */
/* 	} */

/* 	if (ok) { */
/* 		VkPresentInfoKHR present_info = { */
/* 			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, */
/* 			.waitSemaphoreCount = countof(signal_semaphores), */
/* 			.pWaitSemaphores = signal_semaphores, */
/* 			.swapchainCount = cmd->swapchain_count, */
/* 			.pSwapchains = swapchain_handles, */
/* 			.pImageIndices = cmd->swapchain_image_indices, */
/* 		}; */
/* 		VkResult result = vkQueuePresentKHR(context->present_queue, &present_info); */
/* 		ok = vkQueuePresentKHR(context->present_queue, &present_info) == VK_SUCCESS; */
/* 		if (ok == false) */
/* 			LOG_ERROR("failed to present swapchains."); */
/* 	} */

/* 	return ok; */
/* } */

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
			context->staging_buffer->mapped + context->current_frame_index * context->staging_buffer_frame_size,
			context->staging_buffer_frame_size); // TODO: staging buffer pool
		result.transient_buffer = context->staging_buffer;
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
		if (ok == false)
			LOG_WARN("failed to submit transfer commands.");
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
			context->cmd_buffers[frame_index].handle = buffers[frame_index];
	}

	if (ok) { // allocate frame fences

		VkFenceCreateInfo f_create_info = {
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT
		};

		for (uint32_t frame_index = 0; frame_index < countof(context->cmd_buffers); ++frame_index)
			ok &= vkCreateFence(context->device.logical, &f_create_info, 0, &context->cmd_buffers[frame_index].in_flight_fence) == VK_SUCCESS;
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
			ok &= vkCreateDescriptorPool(context->device.logical, &dp_create_info, 0, &context->cmd_buffers[frame_index].descriptor_pool) == VK_SUCCESS;
	}
	return ok;
}

void gfx_cmd_buffer_to_buffer(GFX_CommandContext *cmd, GFX_Buffer *dst, GFX_Buffer *src, uint64_t dst_offset, uint64_t src_offset, uint64_t size) {
	LOG_TRACE("Copying region of %llu from %p to %p", size, src, dst);
	VkBufferCopy copy_region = { .srcOffset = src_offset, .dstOffset = dst_offset, .size = size };
	vkCmdCopyBuffer(cmd->handle, src->handle, dst->handle, 1, &copy_region);
}

void gfx_cmd_buffer_to_image(GFX_CommandContext *cmd, GFX_Image *dst, GFX_Buffer *src, uint64_t src_offset, uint32_t width, uint32_t height) {
	ArenaTemp scratch = arena_scratch_begin(NULL);
	bool ok = cmd && cmd->handle && dst && dst->handle;
	if (ok == false)
		LOG_WARN("%s - invalid parameter '%s' passed", __func__, cmd == 0 || cmd->handle == 0 ? "GFX_CommandContext" : "GFX_Image");

	if (ok) {
		ResourceUsage original = dst->usage;
		gfx_cmd_image_transition(cmd, RESOURCE_USAGE_TRANSFER_DST, dst);

		VkBufferImageCopy *regions = arena_push_count(scratch.arena, VkBufferImageCopy, dst->image_info.arrayLayers);

		for (uint32_t layer_index = 0; layer_index < dst->image_info.arrayLayers; ++layer_index) {
			regions[layer_index] = (VkBufferImageCopy){
				.bufferOffset = src_offset + (layer_index * width * height * gfx__pixel_format_to_stride(dst->format)),
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
		vkCmdCopyBufferToImage(cmd->handle, src->handle, dst->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, dst->image_info.arrayLayers, regions);
		gfx_cmd_image_transition(cmd, original, dst);
	}

	arena_scratch_end(scratch);
}

void gfx_cmd_buffer_barrier(GFX_CommandContext *cmd, ResourceUsage src, ResourceUsage dst, uint64_t offset, uint64_t size, GFX_Buffer *target) {
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

void gfx_cmd_image_barrier(GFX_CommandContext *cmd, ResourceUsage src, ResourceUsage dst, GFX_Image *target) {
	bool ok = cmd && target;

	if (ok) {
		target->usage = dst;

		ok = dst;
	}

	if (ok) {
		VkPipelineStageFlags src_stage = gfx__usage_to_pipeline_stage(src);
		VkPipelineStageFlags dst_stage = gfx__usage_to_pipeline_stage(dst);
		VkAccessFlags src_access = gfx__usage_to_access(src);
		VkAccessFlags dst_access = gfx__usage_to_access(dst);
		VkImageLayout src_layout = gfx__usage_to_image_layout(src);
		VkImageLayout dst_layout = gfx__usage_to_image_layout(dst);

		VkImageMemoryBarrier image_barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = src_access,
			.dstAccessMask = dst_access,
			.oldLayout = src_layout,
			.newLayout = dst_layout,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = target->handle,
			.subresourceRange = target->view_info.subresourceRange,
		};

		vkCmdPipelineBarrier(
			cmd->handle,
			src_stage, dst_stage,
			0,
			0, NULL,
			0, NULL,
			1, &image_barrier);
	}
}

void gfx_cmd_image_transition(GFX_CommandContext *cmd, ResourceUsage dst, GFX_Image *target) {
	gfx_cmd_image_barrier(cmd, RESOURCE_USAGE_UNDEFINED, dst, target);
}

void gfx_cmd_image_blit(GFX_CommandContext *cmd, Rectangle source_rect, GFX_Image *source, Rectangle target_rect, GFX_Image *target) {
	// TODO: Store current image layout, transition if necessary
	VkImageBlit blit_info = {
		.srcOffsets[1] = {
		  .x = source_rect.width,
		  .y = source_rect.height,
		  .z = 1,
		},
		.srcSubresource = {
		  .aspectMask = source->view_info.subresourceRange.aspectMask,
		  .baseArrayLayer = source->view_info.subresourceRange.baseArrayLayer,
		  .layerCount = source->view_info.subresourceRange.layerCount,
		  .mipLevel = 0,
		},
		.dstOffsets[1] = {
		  .x = target_rect.width,
		  .y = target_rect.height,
		  .z = 1,
		},
		.dstSubresource = {
		  .aspectMask = target->view_info.subresourceRange.aspectMask,
		  .baseArrayLayer = target->view_info.subresourceRange.baseArrayLayer,
		  .layerCount = target->view_info.subresourceRange.layerCount,
		  .mipLevel = 0,
		},
	};
	vkCmdBlitImage(cmd->handle,
		source->handle,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		target->handle,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &blit_info, 0);
}

void gfx_cmd_image_upload(GFX_CommandContext *cmd, GFX_Image *image, uint32_t width, uint32_t height, void *pixels) {
	uint32_t stride = gfx__pixel_format_to_stride(image->format);
	uint64_t size = width * height * stride * image->image_info.arrayLayers;
	uint64_t start_offset = cmd->transient_arena->offset;
	memory_copy(arena_push(cmd->transient_arena, alignup(size, 256), 1, 0), pixels, size);
	gfx_cmd_buffer_to_image(cmd, image, cmd->transient_buffer, start_offset, width, height);
}

Image2D load_image(Arena *arena, String8 path) {
	Image2D result = { .format = PIXELFORMAT_RGBA8_UNORM };

	bool ok = true;

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
		channels = 4;
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

Image2D load_cubemap(Arena *arena, String8 paths[FACE_COUNT]) {
	Image2D result = { 0 };

	Image2D images[FACE_COUNT];
	for (uint32_t face_index = 0; face_index < FACE_COUNT; ++face_index) {
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
		result.material_count = data->materials_count;
		result.materials = arena_push_count(arena, Material, result.material_count);

		for (uint32_t material_index = 0; material_index < data->materials_count; ++material_index) {
			cgltf_material *material = &data->materials[material_index];
			Material *out = &result.materials[material_index];
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
					out->textures[TEXTURE_SLOT_ALEBDO] = load_gltf_image(arena, directory, image);
					out->textures[TEXTURE_SLOT_ALEBDO].format = PIXELFORMAT_RGBA8_SRGB;
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
						part->material_id = cgltf_material_index(data, material);
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

			cgltf_accessor_unpack_floats(accessor, (float *)result.skeleton.inverse_rest_matrices, accessor->count * cgltf_num_components(accessor->type));

			for (uint32_t joint_index = 0; joint_index < skin->joints_count; ++joint_index) {
				cgltf_node *joint = skin->joints[joint_index];
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

static inline float3 face_orient(float3 v, Face face) {
	float3 result = { 0 };
	switch (face) {
		case FACE_UP:
			result = (float3){ v.x, v.y, v.z };
			break;
		case FACE_DOWN:
			result = (float3){ v.x, -v.y, -v.z };
			break;
		case FACE_RIGHT:
			result = (float3){ v.y, v.z, v.x };
			break;
		case FACE_LEFT:
			result = (float3){ -v.y, v.z, -v.x };
			break;
		case FACE_FORWARD:
			result = (float3){ v.x, v.z, -v.y };
			break;
		case FACE_BACKWARD:
			result = (float3){ -v.x, v.z, v.y };
			break;
		default:
			ASSERT(!"invalid orientation passed.");
			break;
	}

	return result;
}

Mesh generate_plane(Arena *arena, Face orientation, float width, float height, uint32_t subdivision_x, uint32_t subdivision_z) {
	Mesh result = { 0 };

	bool ok = arena;

	if (arena) {
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
