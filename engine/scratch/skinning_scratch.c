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

#include <vulkan/vulkan_core.h>

#include <vulkan/vulkan.h>
#include <cgltf/cgltf.h>

#define MAX_FRAMES_IN_FLIGHT 2

typedef struct Buffer Buffer;
struct Buffer {
	Buffer *next;

	VkBuffer handle;
	VkDeviceMemory memory;
	uint8_t *mapped;
	VkDeviceAddress address;

	uint64_t size;
	VkBufferCreateInfo info;

	BufferMemory memory_type;
	BufferUsage usage;
};

typedef struct Image Image;
struct Image {
	Image *next;

	VkImage handle;
	VkImageView view;
	VkDeviceMemory memory;

	uint32_t width, height;

	VkImageCreateInfo image_info;
	VkImageViewCreateInfo view_info;
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
typedef struct Swapchain Swapchain;
struct Swapchain {
	Swapchain *next;

	VkSwapchainKHR handle;
	VkSurfaceKHR surface;

	VkSwapchainCreateInfoKHR info;

	VkImage images[SWAPCHAIN_IMAGE_COUNT];
	VkImageView views[SWAPCHAIN_IMAGE_COUNT];
	VkImageViewCreateInfo view_infos[SWAPCHAIN_IMAGE_COUNT];
	uint32_t image_count;

	VkSemaphore image_available_semaphores[MAX_FRAMES_IN_FLIGHT]; // has to be MAX_FRAMES_IN_FLIGHT, as you need one for each frame index
	VkSemaphore render_done_semaphores[SWAPCHAIN_IMAGE_COUNT]; // has to be SWAPCHAIN_IMAGE_COUNT, as you need one for each swapchain image

	int32_t present_index;
	VkQueue present_queue;
};

#define MAX_BUFFERS 1024
#define MAX_IMAGES 512
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

	uint8_t swapchain_image_indices[MAX_SWAPCHAINS];

	Buffer *staging_buffer;
	Arena staging_arena[1];
} GFX_CommandBuffer;

typedef struct {
	Arena arena[1];

	VkInstance instance;
	VulkanDevice device;

	// Engine globals
	VkCommandPool graphics_command_pool;
	VkPushConstantRange global_range;

	Buffer staging_buffer;
	uint64_t staging_buffer_frame_size;

	VkQueue graphics_queue;
	VkQueue transfer_queue, compute_queue;

	int32_t graphics_index, present_index;
	int32_t transfer_index, compute_index;

	// Frame resources
	GFX_CommandBuffer cmd_buffers[MAX_FRAMES_IN_FLIGHT];
	uint32_t current_frame;

	// Resources
	Buffer *buffers;
	uint32_t buffer_count;

	Image *images;
	uint32_t image_count;

	Shader *shaders;
	uint32_t shader_count;

	Swapchain *swapchains;
	uint32_t swapchain_count;

	Buffer *first_free_buffer;
	Image *first_free_image;
	Shader *first_free_shader;
	Swapchain *first_free_swapchain;

	bool initialized;

#ifdef DEV_BUILD
	VkDebugUtilsMessengerEXT debug_messenger;
#endif
} GFX_Context;

bool gfx_startup(GFX_Context *context);
void gfx_shutdown(GFX_Context *context);

GFX_CommandBuffer *gfx_frame_begin(GFX_Context *context);
bool gfx_frame_end(GFX_Context *context);

GFX_CommandBuffer gfx_transfer_batch_begin(GFX_Context *context);
bool gfx_transfer_batch_submit(GFX_Context *context, GFX_CommandBuffer *batch);

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

Buffer buffer_make(GFX_Context *context, uint64_t size, BufferMemory memory, BufferUsage usage);
Image image_make(GFX_Context *context, uint32_t width, uint32_t height, ImageOptions options);
Swapchain swapchain_make(GFX_Context *context, OS_Surface *surface);
Shader compute_pipeline_make(GFX_Context *context, String8 compute_bytecode, VkDescriptorSetLayout *layouts, uint32_t layout_count);
Shader graphics_pipeline_make(GFX_Context *context, String8 vertexcode, String8 fragmentcode);

bool buffer_destroy(GFX_Context *context, Buffer *buffer);
void image_destroy(GFX_Context *context, Image *image);
void swapchain_destroy(GFX_Context *context, Swapchain *surface);
void pipeline_destroy(GFX_Context *context, Shader *pipeline);

Image swapchain_backbuffer(GFX_Context *context, Swapchain *surface, uint32_t *out_image_index);

void gfx_cmd_buffer_to_buffer(GFX_CommandBuffer *cmd, Buffer *dst, Buffer *src, uint64_t dst_offset, uint64_t src_offset, uint64_t size);
void gfx_cmd_buffer_barrier(GFX_CommandBuffer *cmd, ResourceUsage src, ResourceUsage dst, uint64_t offset, uint64_t size, Buffer *target);
void gfx_cmd_image_barrier(GFX_CommandBuffer *cmd, ResourceUsage src, ResourceUsage dst, Image *target);
void gfx_cmd_image_transition(GFX_CommandBuffer *cmd, ResourceUsage dst, Image *target);
VkDescriptorSet gfx_cmd_bindset_push(GFX_CommandBuffer *cmd);

typedef struct {
	uint64_t vertex_offset, index_offset;
	uint32_t vertex_count, index_count;
} MeshPart;

typedef struct {
	// CPU
	Vertex3 *vertices;
	SkinningData *skinning;
	uint32_t *indices;

	// GPU
	Buffer *buffer;
	uint64_t buffer_vertex_byte_offset;
	uint64_t buffer_index_byte_offset;

	uint64_t buffer_skinning_data_byte_offset;
	uint64_t buffer_skinned_vertex_byte_offset;

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
	MESH_COUNT,
} MeshID;

Mesh load_gltf(Arena *arena, String8 path);
AnimationClip *load_animations(Arena *arena, String8 path, uint32_t *count);

AnimationClip *find_animation(AnimationClip *clips, uint32_t count, String8 target) {
	for (uint32_t anim_index = 0; anim_index < count; ++anim_index) {
		if (str8_equals(str8_wrap(clips[anim_index].name), target)) {
			return clips + anim_index;
		}
	}

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
		.position = { 0.0f, 1.5f, 3.f },
		.target = { 0.0f, 1.5f, 0.0f },
		.up = FLOAT3_Y,
		.fovy = 45.f,
	};

	Arena arena[1] = { arena_make(MiB(256)) };

	GFX_Context context[] = { 0 };
	gfx_startup(context);
	Swapchain swapchains[] = { swapchain_make(context, popup_compute), swapchain_make(context, main_render) };

	Image compute_image = image_make(context, 640, 360, (ImageOptions){ PIXEL_FORMAT_RGBA16_FLOAT, IMAGE_USAGE_STORAGE | IMAGE_USAGE_TRANSFER });
	Image offscreen_render = image_make(context, 948, 1044, (ImageOptions){ PIXEL_FORMAT_BACKBUFFER, IMAGE_USAGE_RENDER, SAMPLE_COUNT_8 });

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

	Shader pipeline_2d = { 0 };
	{ // create 2d graphics pipeline
		ArenaTemp scratch = arena_scratch_begin(NULL);
		String8 vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/batch2d.vertex.spv"));
		String8 fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/flat.fragment.spv"));
		pipeline_2d = graphics_pipeline_make(context, vertex_bytecode, fragment_bytecode);
		arena_scratch_end(scratch);
	}

	Shader pipeline_3d = { 0 };
	{ // create 3d graphics pipeline
		ArenaTemp scratch = arena_scratch_begin(NULL);
		String8 vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/batch3d.vertex.spv"));
		String8 fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/flat.fragment.spv"));
		pipeline_3d = graphics_pipeline_make(context, vertex_bytecode, fragment_bytecode);
		arena_scratch_end(scratch);
	}

	Buffer geometry = buffer_make(context, MiB(256), BUFFER_MEMORY_LOCAL, BUFFER_USAGE_STORAGE | BUFFER_USAGE_INDEX | BUFFER_USAGE_TRANSFER);

	Mesh meshes[] = {
		[MESH_HERO_MALE] = load_gltf(arena, s("assets/models/hero_male.glb")),
		[MESH_GDBOT] = load_gltf(arena, s("assets/models/gdbot.glb")),
		[MESH_MAGE] = load_gltf(arena, s("assets/models/mage.glb")),
		[MESH_BARREL] = load_gltf(arena, s("assets/models/barrel.glb")),
		[MESH_ROOM] = load_gltf(arena, s("assets/models/room.glb")),
	};

	{ // upload geometry
		GFX_CommandBuffer cmd = gfx_transfer_batch_begin(context);
		if (cmd.handle) {
			uint64_t upload_cursor = 0;

			for (uint32_t mesh_index = 0; mesh_index < countof(meshes); ++mesh_index) {
				Mesh *mesh = &meshes[mesh_index];
				mesh->buffer = &geometry;

				uint64_t initial_offset = arena_mark(cmd.staging_arena);
				uint64_t mesh_start_cursor = upload_cursor;
				uint64_t copied_size = 0;

				uint64_t total_vertex_buffer_size = alignup(mesh->total_vertex_count * sizeof(Vertex3), 256);
				uint64_t total_index_buffer_size = alignup(mesh->total_index_count * sizeof(uint32_t), 256);
				uint64_t total_skinning_buffer_size = alignup(mesh->total_vertex_count * sizeof(SkinningData), 256);

				// Vertices
				mesh->buffer_vertex_byte_offset = upload_cursor;
				memory_copy(arena_push(cmd.staging_arena, total_vertex_buffer_size, 1, false), mesh->vertices, mesh->total_vertex_count * sizeof(Vertex3));
				upload_cursor += total_vertex_buffer_size;
				copied_size += total_vertex_buffer_size;

				// Indices
				mesh->buffer_index_byte_offset = upload_cursor;
				memory_copy(arena_push(cmd.staging_arena, total_index_buffer_size, 1, false), mesh->indices, mesh->total_index_count * sizeof(uint32_t));
				upload_cursor += total_index_buffer_size;
				copied_size += total_index_buffer_size;

				// Skinning
				if (mesh->skeleton.bone_count) {
					mesh->buffer_skinning_data_byte_offset = upload_cursor;
					memory_copy(arena_push(cmd.staging_arena, total_skinning_buffer_size, 1, false), mesh->skinning, mesh->total_vertex_count * sizeof(SkinningData));
					upload_cursor += total_skinning_buffer_size;
					copied_size += total_skinning_buffer_size;

					mesh->buffer_skinned_vertex_byte_offset = upload_cursor;
					upload_cursor += total_vertex_buffer_size; // Reserve space for compute output (no copy needed)
				}

				gfx_cmd_buffer_to_buffer(&cmd, mesh->buffer, cmd.staging_buffer, mesh_start_cursor, initial_offset, copied_size);

				gfx_cmd_buffer_barrier(&cmd, RESOURCE_USAGE_TRANSFER_DST, RESOURCE_USAGE_SHADER_READ, mesh->buffer_vertex_byte_offset, total_vertex_buffer_size, mesh->buffer);
				gfx_cmd_buffer_barrier(&cmd, RESOURCE_USAGE_TRANSFER_DST, RESOURCE_USAGE_INDEX_BUFFER, mesh->buffer_index_byte_offset, total_index_buffer_size, mesh->buffer);

				if (mesh->skeleton.bone_count) {
					gfx_cmd_buffer_barrier(&cmd, RESOURCE_USAGE_TRANSFER_DST, RESOURCE_USAGE_SHADER_READ, mesh->buffer_skinning_data_byte_offset, total_vertex_buffer_size + total_skinning_buffer_size, mesh->buffer);
				}
			}

			gfx_transfer_batch_submit(context, &cmd);
		}
	}

	uint32_t animation_counts[MESH_COUNT];
	AnimationClip *animations[MESH_COUNT] = {
		[MESH_HERO_MALE] = load_animations(arena, s("assets/models/hero_male.glb"), &animation_counts[MESH_HERO_MALE]),
		[MESH_GDBOT] = load_animations(arena, s("assets/models/gdbot.glb"), &animation_counts[MESH_GDBOT]),
	};

	Buffer scratch_buffers[MAX_FRAMES_IN_FLIGHT];
	for (uint32_t index = 0; index < countof(scratch_buffers); ++index) {
		scratch_buffers[index] = buffer_make(context, MiB(1), BUFFER_MEMORY_SHARED, BUFFER_USAGE_STORAGE | BUFFER_USAGE_TRANSFER);
		vkMapMemory(context->device.logical, scratch_buffers[index].memory, 0, scratch_buffers[index].size, 0, (void **)&scratch_buffers[index].mapped);
	}

	bool is_open = true;

	float dt = 0.0f;
	float last_frame = 0.0f;

	Arena frame_arena[] = { arena_make(MiB(1)) };
	memory_zero(context->staging_buffer.mapped, context->staging_buffer_frame_size);
	while (is_open) {
		double time = os_time_ns() * 1e-9;
		dt = time - last_frame;
		last_frame = time;

		static bool capture = false;
		os_cursor_capture(main_render, capture);
		if (input_key_pressed(KEY_CODE_TAB)) {
			LOG_INFO("%s...", capture ? "releasing" : "capturing");
			capture = !capture;
		}

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
					LOG_INFO("%g, %g", input_mouse_x(), input_mouse_y());
					break;
				default:
					break;
			}
		}

		uint2 dims = os_surface_size(main_render);

		// Frame resources
		GFX_CommandBuffer *cmd = gfx_frame_begin(context);
		if (cmd == 0)
			break;
		VkCommandBuffer command_buffer = cmd->handle;
		VkDescriptorPool descriptor_pool = cmd->descriptor_pool;

		// Swapchain image acquisition
		uint32_t image_indices[SWAPCHAIN_IMAGE_COUNT];
		Image main_target = swapchain_backbuffer(context, &swapchains[0], &image_indices[0]); // TODO: remove manual indices handling?
		if (main_target.handle == 0) // TODO: Handle out of date swapchain
			break;

		Image popup_target = swapchain_backbuffer(context, &swapchains[1], &image_indices[1]);
		if (popup_target.handle == 0) // TODO: Handle out of date swapchain
			break;

		static float anim_t = 0.0f;
		static uint32_t anim_index = 3;

		anim_t += dt;

		typedef struct {
			MeshID id;
			Transform3 transform;
			uint64_t skinned_vertices_offset;
		} MeshInstance;

		MeshInstance instances[] = {
			{ MESH_HERO_MALE, { { 0 }, { 0 }, FLOAT3_ONE }, 0 },
			{ MESH_HERO_MALE, { { 0.0f, 0.0f, -3.0f }, { 0 }, FLOAT3_ONE }, 0 },
			{ MESH_GDBOT, { { 3.0f, 0.0f, 0.0f }, { 0 }, FLOAT3_ONE }, 0 },
			{ MESH_MAGE, { { -3.0f, 0.0f, 0.0f }, { 0 }, FLOAT3_ONE }, 0 },
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

			memory_copy(scratch_buffers[context->current_frame].mapped + matrices_offset, skin_matrices, matrices_size);

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
					.skinning_matrices_address = scratch_buffers[context->current_frame].address + matrices_offset,
					.input_address = mesh->buffer->address + mesh->buffer_vertex_byte_offset,
					.skinning_address = mesh->buffer->address + mesh->buffer_skinning_data_byte_offset,
					.output_address = scratch_buffers[context->current_frame].address + instance->skinned_vertices_offset,
				};
				vkCmdPushConstants(command_buffer, pipeline_skinning.layout, VK_SHADER_STAGE_ALL, 0, sizeof(pc), &pc);

				vkCmdDispatch(command_buffer, (mesh->total_vertex_count + 255) / 256, 1, 1);

				gfx_cmd_buffer_barrier(
					cmd,
					RESOURCE_USAGE_COMPUTE_SHADER_WRITE,
					RESOURCE_USAGE_VERTEX_SHADER_READ,
					instance->skinned_vertices_offset,
					skinned_vertices_size, &scratch_buffers[context->current_frame]);
			}
		}

		// transition sawpchain images for drawing
		gfx_cmd_image_transition(
			cmd,
			RESOURCE_USAGE_TRANSFER_DST,
			&main_target);
		gfx_cmd_image_transition(
			cmd,
			RESOURCE_USAGE_COLOR_ATTACHMENT,
			&popup_target);

		// transition offscreen target
		gfx_cmd_image_transition(
			cmd,
			RESOURCE_USAGE_COLOR_ATTACHMENT,
			&offscreen_render);

		// transition compute storage image
		gfx_cmd_image_transition(
			cmd,
			RESOURCE_USAGE_COMPUTE_SHADER_WRITE,
			&compute_image);

#define PART_INDEX 4

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
				.imageView = compute_image.view,
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
		gfx_cmd_image_barrier(cmd, RESOURCE_USAGE_COMPUTE_SHADER_WRITE, RESOURCE_USAGE_TRANSFER_SRC, &compute_image);

		VkImageBlit blit_info = {
			.srcOffsets[1] = {
			  .x = 640,
			  .y = 360,
			  .z = 1,
			},
			.srcSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseArrayLayer = 0, .layerCount = 1, .mipLevel = 0 },
			.dstOffsets[1] = {
			  .x = 640,
			  .y = 360,
			  .z = 1,
			},
			.dstSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseArrayLayer = 0, .layerCount = 1, .mipLevel = 0 },
		};
		vkCmdBlitImage(command_buffer,
			compute_image.handle,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			main_target.handle,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit_info, 0);

		float2 mouse_delta = float2_from_double2(input_mouse_delta());
		mouse_delta.x /= dims.x;
		mouse_delta.y /= dims.y;

		VkRenderingAttachmentInfo attachment_info = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = offscreen_render.view,
			.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.resolveImageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			.resolveImageView = popup_target.view,
			.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT,
			.clearValue.color = { 1.0f, 0.0f, 1.0f, 1.0f },
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		};

		VkExtent2D extent = {
			.width = popup_target.width,
			.height = popup_target.height,
		};
		VkRenderingInfo renderpass_info = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea.extent = extent,
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &attachment_info,
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

		scene_camera_orbit(&camera, mouse_delta);
		float4x4 view_projection = float4x4_multiply(
			float4x4_perspective(to_radians(45.f), (float)dims.x / (float)dims.y, 0.001f, 200.f),
			float4x4_lookat(camera.position, camera.target, camera.up));
		vkCmdPushConstants(command_buffer, pipeline_3d.layout, VK_SHADER_STAGE_ALL, 0, sizeof(float4x4), &view_projection);

		uint32_t last_mesh_id = -1;
		for (uint32_t instance_index = 0; instance_index < countof(instances); ++instance_index) {
			MeshInstance *instance = &instances[instance_index];
			Mesh *mesh = &meshes[instance->id];

			VkDescriptorSetAllocateInfo alloc_info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = pipeline_3d.set_layouts + 0,
			};

			VkDescriptorSet grahpics_set = 0;
			vkAllocateDescriptorSets(context->device.logical, &alloc_info, &grahpics_set);

			VkDescriptorBufferInfo buffer_info = {
				.buffer = mesh->buffer->handle,
				.offset = mesh->buffer_vertex_byte_offset,
				.range = mesh->total_vertex_count * sizeof(Vertex3),
			};
			if (mesh->skeleton.bone_count && animation_counts[instance->id]) {
                buffer_info.buffer= scratch_buffers[context->current_frame].handle;
                buffer_info.offset = instance->skinned_vertices_offset;
            }
			VkWriteDescriptorSet write = {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = grahpics_set,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.pBufferInfo = &buffer_info,
			};
			vkUpdateDescriptorSets(context->device.logical, 1, &write, 0, 0);

			vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_3d.handle);
			vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_3d.layout, 0, 1, &grahpics_set, 0, 0);

			vkCmdBindIndexBuffer(command_buffer, geometry.handle, mesh->buffer_index_byte_offset, VK_INDEX_TYPE_UINT32);
			for (uint32_t part_index = 0; part_index < mesh->part_count; ++part_index) {
				// clang-format off
                float4x4 transform = float4x4_compose_quat(
                        instance->transform.translation,
                        instance->transform.rotation,
                        instance->transform.scale
                );
				// clang-format on
				//
				vkCmdPushConstants(cmd->handle, pipeline_3d.layout, VK_SHADER_STAGE_ALL, sizeof(float4x4), sizeof(float4x4), &transform);
				vkCmdDrawIndexed(command_buffer, mesh->parts[part_index].index_count, 1, mesh->parts[part_index].index_offset, mesh->parts[part_index].vertex_offset, 0);
			}
		}

		vkCmdEndRendering(command_buffer);

		// transition swapchain images for presenting
		gfx_cmd_image_barrier(
			cmd,
			RESOURCE_USAGE_TRANSFER_DST,
			RESOURCE_USAGE_PRESENT,
			&main_target);
		gfx_cmd_image_barrier(
			cmd,
			RESOURCE_USAGE_COLOR_ATTACHMENT,
			RESOURCE_USAGE_PRESENT,
			&popup_target);

		// draw to offscreen render target

		if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
			LOG_INFO("failed to record command buffer.");
			break;
		}

		VkSemaphore wait_semaphores[] = { swapchains[0].image_available_semaphores[context->current_frame], swapchains[1].image_available_semaphores[context->current_frame] };
		VkSemaphore signal_semaphores[] = { swapchains[0].render_done_semaphores[image_indices[0]], swapchains[1].render_done_semaphores[image_indices[1]] };
		VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

		VkSubmitInfo submit_info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = countof(wait_semaphores),
			.pWaitSemaphores = wait_semaphores,
			.pWaitDstStageMask = wait_stages,
			.commandBufferCount = 1,
			.pCommandBuffers = &command_buffer,
			.signalSemaphoreCount = countof(signal_semaphores),
			.pSignalSemaphores = signal_semaphores,
		};

		if (vkQueueSubmit(context->graphics_queue, 1, &submit_info, context->cmd_buffers[context->current_frame].in_flight_fence) != VK_SUCCESS) {
			LOG_ERROR("failed to submit command buffer to queue.");
			break;
		}

		VkSwapchainKHR swapchain_handles[] = { swapchains[0].handle, swapchains[1].handle };
		VkPresentInfoKHR present_info = {
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = countof(signal_semaphores),
			.pWaitSemaphores = signal_semaphores,
			.swapchainCount = countof(swapchains),
			.pSwapchains = swapchain_handles,
			.pImageIndices = image_indices,
		};
		VkResult result = vkQueuePresentKHR(swapchains[0].present_queue, &present_info);
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

		context->current_frame = (context->current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
		arena_reset(frame_arena);

		/* batch.offset = 0; */
		/* batch.memory = scratch_buffers[context->current_frame].mapped; */
	}

	vkDeviceWaitIdle(context->device.logical);

	// :destroy
	{
		for (uint32_t index = 0; index < countof(scratch_buffers); ++index)
			buffer_destroy(context, &scratch_buffers[index]);

		buffer_destroy(context, &geometry);
		image_destroy(context, &offscreen_render);
		image_destroy(context, &compute_image);

		pipeline_destroy(context, &c_pipeline);
		pipeline_destroy(context, &pipeline_skinning);
		pipeline_destroy(context, &pipeline_2d);
		pipeline_destroy(context, &pipeline_3d);

		for (uint32_t index = 0; index < countof(swapchains); ++index)
			swapchain_destroy(context, &swapchains[index]);
	}

	gfx_shutdown(context);

	os_surface_close(main_render);
	os_surface_close(popup_compute);
	os_display_shutdown();
	return 0;
}

VkImageUsageFlags gfx__to_vk_image_usage(PixelFormat format, ImageUsageFlags usage) {
	VkImageUsageFlags result = 0;
	if (FLAG_GET(usage, IMAGE_USAGE_RENDER)) {
		if (pixel_format_is_depth(format))
			result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		else
			result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}

	if (FLAG_GET(usage, IMAGE_USAGE_SAMPLE))
		result |= VK_IMAGE_USAGE_SAMPLED_BIT;

	if (FLAG_GET(usage, IMAGE_USAGE_STORAGE))
		result |= VK_IMAGE_USAGE_STORAGE_BIT;

	if (FLAG_GET(usage, IMAGE_USAGE_TRANSFER))
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
}

VkFormat gfx__pixel_format_to_vk_format(PixelFormat format) {
	switch (format) {
		case PIXEL_FORMAT_RGBA8_UNORM:
			return VK_FORMAT_R8G8B8A8_UNORM;
		case PIXEL_FORMAT_RGBA8_SRGB:
			return VK_FORMAT_R8G8B8A8_SRGB;
		case PIXEL_FORMAT_RGBA16_FLOAT:
			return VK_FORMAT_R16G16B16A16_SFLOAT;
		case PIXEL_FORMAT_R32_FLOAT:
			return VK_FORMAT_R32_SFLOAT;
		case PIXEL_FORMAT_DEPTH:
			return VK_FORMAT_D32_SFLOAT;
		case PIXEL_FORMAT_DEPTH_STENCIL:
			return VK_FORMAT_D24_UNORM_S8_UINT;
		case PIXEL_FORMAT_BACKBUFFER:
			return VK_FORMAT_B8G8R8A8_SRGB;
	}

	return VK_FORMAT_UNDEFINED;
}

VkImageAspectFlags gfx__pixel_format_to_aspect(PixelFormat format) {
	if (pixel_format_is_depth(format))
		return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

	return VK_IMAGE_ASPECT_COLOR_BIT;
}

VkSampleCountFlags gfx__usage_to_sample_count(VkPhysicalDeviceLimits limits, ImageOptions options) {
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

Buffer buffer_make(GFX_Context *context, uint64_t size, BufferMemory memory, BufferUsage usage) {
	Buffer result = { 0 };
	LOG_DEBUG("creating vulkan buffer.");

	bool ok = true;

	result.memory_type = memory;
	result.usage = usage;

	VkBufferUsageFlags vk_usage = gfx__to_vk_buffer_usage(usage);
	VkMemoryPropertyFlags memory_flags = gfx__memory_type_to_vk_memory_property_flags(memory);

	if (ok) { // create buffer handle
		result.size = size;

		uint32_t family_indices[] = { context->graphics_index, context->transfer_index };
		result.info = (VkBufferCreateInfo){
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = result.size,
			.usage = vk_usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		};

		ok = vkCreateBuffer(context->device.logical, &result.info, 0, &result.handle) == VK_SUCCESS;
	}

	if (ok) { // allocate buffer memory
		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements(context->device.logical, result.handle, &memory_requirements);

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

		ok = vkAllocateMemory(context->device.logical, &allocate_info, 0, &result.memory) == VK_SUCCESS;
	}

	if (ok) { // bind memory & get buffer device address
		ok = vkBindBufferMemory(context->device.logical, result.handle, result.memory, 0) == VK_SUCCESS;

		if (vk_usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
			VkBufferDeviceAddressInfo bda_info = {
				.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
				.buffer = result.handle,
			};
			result.address = vkGetBufferDeviceAddress(context->device.logical, &bda_info);
		}
	}

	if (ok == false) // remove half-made resources on error
		buffer_destroy(context, &result);

	return result;
}

Image image_make(GFX_Context *context, uint32_t width, uint32_t height, ImageOptions options) {
	Image result = { 0 };
	LOG_DEBUG("creating vulkan image.");

	bool ok = true;
	VkImageUsageFlags vk_usage = gfx__to_vk_image_usage(options.format, options.usage);
	VkFormat vk_format = gfx__pixel_format_to_vk_format(options.format);
	VkImageAspectFlags aspect = gfx__pixel_format_to_aspect(options.format);
	VkSampleCountFlags vk_sample = gfx__usage_to_sample_count(context->device.limits, options);
	result.width = width, result.height = height;

	if (ok) { // make vulkan image handle
		result.image_info = (VkImageCreateInfo){
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.flags = 0,
			.format = vk_format,
			.extent = {
			  .width = width,
			  .height = height,
			  .depth = 1,
			},
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = vk_sample,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = vk_usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
		};

		ok = vkCreateImage(context->device.logical, &result.image_info, 0, &result.handle) == VK_SUCCESS;
	}

	if (ok) { // allocate memory
		VkMemoryRequirements memory_requirements;
		vkGetImageMemoryRequirements(context->device.logical, result.handle, &memory_requirements);

		VkMemoryAllocateInfo allocate_info = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = memory_requirements.size,
			.memoryTypeIndex = gfx__find_memory_type(context->device.physical, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
		};

		ok = vkAllocateMemory(context->device.logical, &allocate_info, 0, &result.memory) == VK_SUCCESS;
	}

	if (ok) // bind memory to handle
		vkBindImageMemory(context->device.logical, result.handle, result.memory, 0);

	if (ok) { // create image view
		result.view_info = (VkImageViewCreateInfo){
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = result.handle,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = result.image_info.format,
			.components = {
			  .r = VK_COMPONENT_SWIZZLE_IDENTITY,
			  .g = VK_COMPONENT_SWIZZLE_IDENTITY,
			  .b = VK_COMPONENT_SWIZZLE_IDENTITY,
			  .a = VK_COMPONENT_SWIZZLE_IDENTITY,
			},
			.subresourceRange = {
			  .aspectMask = aspect,
			  .baseMipLevel = 0,
			  .levelCount = 1,
			  .baseArrayLayer = 0,
			  .layerCount = 1,
			}
		};

		ok = vkCreateImageView(context->device.logical, &result.view_info, 0, &result.view) == VK_SUCCESS;
	}

	if (ok == false) // remove half-made resources on error
		image_destroy(context, &result);

	/* LOG_INFO("image loaded successfuly (%ux%u | %s)", indexof(context->image_pool, image), width, height, image_format_to_string[format]); */
	return result;
}

Swapchain swapchain_make(GFX_Context *context, OS_Surface *surface) {
	Swapchain result = { 0 };
	ArenaTemp scratch = arena_scratch_begin(0);
	LOG_DEBUG("creating vulkan surface.");

	bool ok = true;

	if (ok) { // create surface
		VkXcbSurfaceCreateInfoKHR surface_create_info = {
			.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
			.connection = os_native_display_handle(),
			.window = (uint32_t)(uint64_t)os_native_surface_handle(surface),
		}; // TODO: Have os decide this

		ok = vkCreateXcbSurfaceKHR(context->instance, &surface_create_info, 0, &result.surface) == VK_SUCCESS;
		if (ok == false)
			LOG_WARN("failed to create vulkan surface.");
	}

	VkSurfaceCapabilitiesKHR capabilities;

	uint32_t surface_format_count = 0;
	VkSurfaceFormatKHR *surface_formats;

	uint32_t present_mode_count = 0;
	VkPresentModeKHR *present_modes;

	if (ok) { // query surface suitability
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->device.physical, result.surface, &capabilities);

		vkGetPhysicalDeviceSurfaceFormatsKHR(context->device.physical, result.surface, &surface_format_count, 0);
		surface_formats = arena_push_count(scratch.arena, VkSurfaceFormatKHR, surface_format_count);
		vkGetPhysicalDeviceSurfaceFormatsKHR(context->device.physical, result.surface, &surface_format_count, surface_formats);

		vkGetPhysicalDeviceSurfacePresentModesKHR(context->device.physical, result.surface, &present_mode_count, 0);
		present_modes = arena_push_count(scratch.arena, VkPresentModeKHR, present_mode_count);
		vkGetPhysicalDeviceSurfacePresentModesKHR(context->device.physical, result.surface, &present_mode_count, present_modes);

		uint32_t queue_family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(context->device.physical, &queue_family_count, 0);
		VkQueueFamilyProperties *queue_family_properties = arena_push_count(scratch.arena, VkQueueFamilyProperties, queue_family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(context->device.physical, &queue_family_count, queue_family_properties);

		result.present_index = -1;
		for (uint32_t index = 0; index < queue_family_count; ++index) {
			VkQueueFlags flags = queue_family_properties[index].queueFlags;

			VkBool32 present_support = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(context->device.physical, index, result.surface, &present_support);
			if (present_support && result.present_index == -1) {
				result.present_index = index;
				break;
			}
		}

		ok &= surface_format_count > 0; // valid surface formats available
		ok &= present_mode_count > 0; // valid present mode available
		ok &= result.present_index != -1; // supports present queue
	}

	if (ok) { // create swapchain
		// get queue handle
		vkGetDeviceQueue(context->device.logical, result.present_index, 0, &result.present_queue);

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
			if (present_modes[mode_index] == VK_PRESENT_MODE_MAILBOX_KHR) // ideal presentation mode
				selected_present_mode = present_modes[mode_index];
		}

		uint32x2 surface_size = os_surface_size(surface);
		float dpi = os_surface_dpi(surface);

		VkExtent2D selected_extents =
			capabilities.currentExtent.width != UINT32_MAX
			? capabilities.currentExtent
			: (VkExtent2D){ .width = (uint32_t)(surface_size.x * dpi), .height = (uint32_t)((float)surface_size.y * dpi) };

		uint32_t queue_family_indices[] = { (uint32_t)context->graphics_index, (uint32_t)result.present_index };

		uint32_t image_count = capabilities.minImageCount + 1;
		if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
			image_count = capabilities.maxImageCount;

		image_count = MIN(image_count, SWAPCHAIN_IMAGE_COUNT);

		result.info = (VkSwapchainCreateInfoKHR){
			.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
			.surface = result.surface,
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
			result.info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			result.info.queueFamilyIndexCount = 2;
			result.info.pQueueFamilyIndices = queue_family_indices;
		} else
			result.info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

		ok = vkCreateSwapchainKHR(context->device.logical, &result.info, NULL, &result.handle) == VK_SUCCESS;
	}

	if (ok) { // get the swapchain images & create image views
		vkGetSwapchainImagesKHR(context->device.logical, result.handle, &result.image_count, NULL);
		vkGetSwapchainImagesKHR(context->device.logical, result.handle, &result.image_count, result.images);

		for (uint32_t image_index = 0; image_index < result.image_count; ++image_index) {
			result.view_infos[image_index] = (VkImageViewCreateInfo){
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = result.images[image_index],
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = result.info.imageFormat,
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

			ok &= vkCreateImageView(context->device.logical, result.view_infos + image_index, NULL, &result.views[image_index]) == VK_SUCCESS;
		}
	}

	if (ok) { // create semaphores
		VkSemaphoreCreateInfo s_create_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		};

		for (uint32_t index = 0; index < countof(result.image_available_semaphores); ++index)
			ok &= vkCreateSemaphore(context->device.logical, &s_create_info, 0, result.image_available_semaphores + index) == VK_SUCCESS;
		for (uint32_t index = 0; index < countof(result.render_done_semaphores); ++index)
			ok &= vkCreateSemaphore(context->device.logical, &s_create_info, 0, result.render_done_semaphores + index) == VK_SUCCESS;
	}

	if (ok == false) { // remove half-made resources on error
		swapchain_destroy(context, &result);
		LOG_ERROR("failed to create vulkan swapchain.");
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

Shader graphics_pipeline_make(GFX_Context *context, String8 vertex_bytecode, String8 fragment_bytecode) {
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

	if (ok) { // create descriptor set layouts
		// TODO: Don't hard-code the layouts

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

		ok = vkCreateDescriptorSetLayout(context->device.logical, &dsl_create_info, NULL, result.set_layouts) == VK_SUCCESS;
		if (ok == false)
			LOG_WARN("failed to create pipeline descriptor set layout 0");
	}

	if (ok) { // create pipeline layout
		VkPipelineLayoutCreateInfo pl_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = 1,
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
			.cullMode = VK_CULL_MODE_NONE,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
			.depthBiasEnable = VK_FALSE,
		};

		VkPipelineMultisampleStateCreateInfo mss_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.sampleShadingEnable = VK_FALSE,
			.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT,
			.minSampleShading = 1.0f,
		};

		VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
			{
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
			},
		};

		VkPipelineColorBlendStateCreateInfo cbs_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.logicOpEnable = VK_FALSE,
			.attachmentCount = countof(color_blend_attachments),
			.pAttachments = color_blend_attachments,
		};

		VkPipelineDepthStencilStateCreateInfo depth_stencil_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
			.depthTestEnable = VK_FALSE,
			.depthWriteEnable = VK_FALSE,
			.depthBoundsTestEnable = VK_FALSE,
			.minDepthBounds = 0.0f,
			.maxDepthBounds = 1.0f
		};

		VkPipelineRenderingCreateInfo r_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
			.colorAttachmentCount = countof(color_blend_attachments),
			.pColorAttachmentFormats = (VkFormat[]){ VK_FORMAT_B8G8R8A8_SRGB },
		};

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

bool buffer_destroy(GFX_Context *context, Buffer *buffer) {
	bool ok = context && buffer;

	if (ok) {
		if (buffer->memory) {
			if (buffer->mapped)
				vkUnmapMemory(context->device.logical, buffer->memory);
			vkFreeMemory(context->device.logical, buffer->memory, NULL);
		}
		if (buffer->handle)
			vkDestroyBuffer(context->device.logical, buffer->handle, NULL);

		memory_zero(buffer, sizeof(*buffer));
	}

	return ok;
}

void image_destroy(GFX_Context *context, Image *image) {
	bool ok = context && image;

	if (ok) {
		if (image->view)
			vkDestroyImageView(context->device.logical, image->view, 0);
		if (image->memory)
			vkFreeMemory(context->device.logical, image->memory, 0);
		if (image->handle)
			vkDestroyImage(context->device.logical, image->handle, 0);

		memory_zero(image, sizeof(*image));
	}
}

void swapchain_destroy(GFX_Context *context, Swapchain *surface) {
	bool ok = context && surface;

	if (ok) {
		for (uint32_t semaphore_index = 0; semaphore_index < countof(surface->image_available_semaphores); ++semaphore_index)
			if (surface->image_available_semaphores[semaphore_index])
				vkDestroySemaphore(context->device.logical, surface->image_available_semaphores[semaphore_index], NULL);
		for (uint32_t semaphore_index = 0; semaphore_index < countof(surface->render_done_semaphores); ++semaphore_index)
			if (surface->render_done_semaphores[semaphore_index])
				vkDestroySemaphore(context->device.logical, surface->render_done_semaphores[semaphore_index], NULL);

		for (uint32_t image_index = 0; image_index < surface->image_count; ++image_index)
			if (surface->views[image_index])
				vkDestroyImageView(context->device.logical, surface->views[image_index], NULL);

		if (surface->handle)
			vkDestroySwapchainKHR(context->device.logical, surface->handle, NULL);
		if (surface->surface)
			vkDestroySurfaceKHR(context->instance, surface->surface, NULL);

		memory_zero(surface, sizeof(*surface));
	}
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

Image swapchain_backbuffer(GFX_Context *context, Swapchain *surface, uint32_t *out_image_index) {
	Image result = { 0 };
	bool ok = true;

	if (ok) { // acquire swapchain image
		VkResult result = vkAcquireNextImageKHR(
			context->device.logical,
			surface->handle,
			UINT64_MAX,
			surface->image_available_semaphores[context->current_frame],
			VK_NULL_HANDLE, out_image_index);

		ok = result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
	}

	if (ok) { // wrap swapchain
		result.handle = surface->images[*out_image_index];
		result.view = surface->views[*out_image_index];
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
		result.view_info = surface->view_infos[*out_image_index];
		result.width = result.image_info.extent.width;
		result.height = result.image_info.extent.height;
	}

	if (ok == false) {
		memory_zero(&result, sizeof(result));
		*out_image_index = -1;
	}

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
		context->staging_buffer_frame_size = MiB(256);
		context->staging_buffer =
			buffer_make(
				context,
				context->staging_buffer_frame_size * MAX_FRAMES_IN_FLIGHT,
				BUFFER_MEMORY_SHARED,
				BUFFER_USAGE_TRANSFER);
		vkMapMemory(context->device.logical, context->staging_buffer.memory, 0, context->staging_buffer.size, 0, (void **)&context->staging_buffer.mapped);
	}

	if (ok) {
		context->arena[0] = arena_make(MiB(32));
		context->buffers = arena_push_count(context->arena, Buffer, MAX_BUFFERS);
		context->images = arena_push_count(context->arena, Image, MAX_IMAGES);
		context->shaders = arena_push_count(context->arena, Shader, MAX_SHADERS);
		context->swapchains = arena_push_count(context->arena, Swapchain, MAX_SWAPCHAINS);
	}

	if (ok) {
		context->initialized = true;
	} else
		LOG_WARN("failed to initialize vulkan context.");

	return ok;
}

void gfx_shutdown(GFX_Context *context) {
#ifdef DEV_BUILD
	if (context->debug_messenger)
		vkDestroyDebugUtilsMessenger(context->instance, context->debug_messenger, 0);
#endif

	for (uint32_t index = 0; index < countof(context->cmd_buffers); ++index) {
		GFX_CommandBuffer *cmd_buffer = &context->cmd_buffers[index];

		if (cmd_buffer->descriptor_pool)
			vkDestroyDescriptorPool(context->device.logical, cmd_buffer->descriptor_pool, 0);
		if (cmd_buffer->in_flight_fence)
			vkDestroyFence(context->device.logical, cmd_buffer->in_flight_fence, 0);
	}

	if (context->graphics_command_pool)
		vkDestroyCommandPool(context->device.logical, context->graphics_command_pool, 0);

	buffer_destroy(context, &context->staging_buffer);
	/* if (context->transfer_command_pool) */
	/* 	vkDestroyCommandPool(context->device.logical, context->transfer_command_pool, 0); */
	if (context->device.logical)
		vkDestroyDevice(context->device.logical, 0);
	if (context->instance)
		vkDestroyInstance(context->instance, 0);

	memory_zero(context, sizeof(GFX_Context));
}

GFX_CommandBuffer *gfx_frame_begin(GFX_Context *context) {
	GFX_CommandBuffer *result = 0;

	bool ok = context;

	if (ok) {
		result = &context->cmd_buffers[context->current_frame];

		// Wait for frame resource availability
		vkWaitForFences(context->device.logical, 1, &result->in_flight_fence, VK_TRUE, UINT64_MAX);

		// reset frame resources
		vkResetCommandBuffer(result->handle, 0);
		vkResetFences(context->device.logical, 1, &result->in_flight_fence);
		vkResetDescriptorPool(context->device.logical, result->descriptor_pool, 0);

		result->staging_buffer = &context->staging_buffer;
		result->staging_arena[0] = arena_wrap(
			context->staging_buffer.mapped + context->current_frame * context->staging_buffer_frame_size,
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

GFX_CommandBuffer gfx_transfer_batch_begin(GFX_Context *context) {
	GFX_CommandBuffer result = { 0 };
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
		result.staging_arena[0] = arena_wrap(
			context->staging_buffer.mapped + context->current_frame * context->staging_buffer_frame_size,
			context->staging_buffer_frame_size); // TODO: staging buffer pool
		result.staging_buffer = &context->staging_buffer;
	}

	return result;
}

bool gfx_transfer_batch_submit(GFX_Context *context, GFX_CommandBuffer *batch) {
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
				context->present_index = index;
				/* ASSERT(present_support && "grahpics index does not support presenting"); */
			}

			/* if (present_support && context->present_index == -1) */
			/* 	context->present_index = index; */

			if ((flags & VK_QUEUE_GRAPHICS_BIT) == false && (flags & VK_QUEUE_TRANSFER_BIT) && context->transfer_index == -1) // dedicated transfer
				context->transfer_index = index;

			if ((flags & VK_QUEUE_GRAPHICS_BIT) == false && (flags & VK_QUEUE_COMPUTE_BIT) && context->compute_index == -1) // dedicated compute
				context->compute_index = index;
		}

		if (context->graphics_index == -1 || context->present_index == -1) {
			LOG_ERROR("failed to find graphics queues");
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

void gfx_cmd_buffer_to_buffer(GFX_CommandBuffer *cmd, Buffer *dst, Buffer *src, uint64_t dst_offset, uint64_t src_offset, uint64_t size) {
	LOG_TRACE("Copying region of %llu from %p to %p", size, src, dst);
	VkBufferCopy copy_region = { .srcOffset = src_offset, .dstOffset = dst_offset, .size = size };
	vkCmdCopyBuffer(cmd->handle, src->handle, dst->handle, 1, &copy_region);
}

VkPipelineStageFlags gfx__usage_to_pipeline_stage(ResourceUsage usage) {
	switch (usage) {
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

void gfx_cmd_buffer_barrier(GFX_CommandBuffer *cmd, ResourceUsage src, ResourceUsage dst, uint64_t offset, uint64_t size, Buffer *target) {
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

void gfx_cmd_image_barrier(GFX_CommandBuffer *cmd, ResourceUsage src, ResourceUsage dst, Image *target) {
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

void gfx_cmd_image_transition(GFX_CommandBuffer *cmd, ResourceUsage dst, Image *target) {
	VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	VkPipelineStageFlags dst_stage = gfx__usage_to_pipeline_stage(dst);
	VkAccessFlags src_access = 0;
	VkAccessFlags dst_access = gfx__usage_to_access(dst);
	VkImageLayout src_layout = VK_IMAGE_LAYOUT_UNDEFINED;
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

				vertex_offset += part->vertex_count;
				index_offset += part->index_count;
			}
		}
	}

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

AnimationClip *load_animations(Arena *arena, String8 path, uint32_t *count) {
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
