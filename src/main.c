#include "platform.h"
#include "renderer/vk_renderer.h"

#include <loaders/cgltf.h>

#include "common.h"
#include "core/arena.h"
#include "core/logger.h"

#include <string.h>

#define GATE_FILE_PATH "assets/models/modular_dungeon/gate.glb"
#define BOT_FILE_PATH "assets/models/characters/gdbot.glb"
#define MAGE_FILE_PATH "assets/models/characters/mage.glb"

typedef struct Mesh {
	vec3 *positions;
	vec3 *normals;
	vec4 *tangets;
	vec2 *uvs;

	uint32_t vertices_count;

	uint32_t *indices;
	uint32_t indices_count;

} Mesh;

Mesh *load_gltf_model(Arena *arena, const char *path);
void draw_frame(Arena *arena, VulkanContext *context, Platform *platform, Buffer *vertex_buffer, Buffer *index_buffer);
void resize_callback(Platform *platform, uint32_t width, uint32_t height);
void update_uniforms(VulkanContext *context, Platform *platform);

// clang-format off
static const Vertex vertices[36] = {
	// Front (+Z)
	{ .position = { -0.5f, -0.5f,  0.5f }, .uv = { 0.0f, 0.0f }}, // Bottom-left
	{ .position = {  0.5f, -0.5f,  0.5f }, .uv = { 1.0f, 0.0f }}, // Bottom-right
	{ .position = {  0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 1.0f }}, // Top-right
	{ .position = {  0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = { -0.5f,  0.5f,  0.5f }, .uv = { 0.0f, 1.0f }},
	{ .position = { -0.5f, -0.5f,  0.5f }, .uv = { 0.0f, 0.0f }},

	// Back (-Z)
	{ .position = {  0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 0.0f }}, // Bottom-right
	{ .position = { -0.5f, -0.5f, -0.5f }, .uv = { 1.0f, 0.0f }}, // Bottom-left
	{ .position = { -0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }}, // Top-left
	{ .position = { -0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = {  0.5f,  0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},
	{ .position = {  0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 0.0f }},

	// Left (-X)
	{ .position = { -0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 0.0f }},
	{ .position = { -0.5f, -0.5f,  0.5f }, .uv = { 1.0f, 0.0f }},
	{ .position = { -0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = { -0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = { -0.5f,  0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},
	{ .position = { -0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 0.0f }},

	// Right (+X)
	{ .position = {  0.5f, -0.5f,  0.5f }, .uv = { 0.0f, 0.0f }},
	{ .position = {  0.5f, -0.5f, -0.5f }, .uv = { 1.0f, 0.0f }},
	{ .position = {  0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = {  0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = {  0.5f,  0.5f,  0.5f }, .uv = { 0.0f, 1.0f }},
	{ .position = {  0.5f, -0.5f,  0.5f }, .uv = { 0.0f, 0.0f }},

	// Bottom (-Y)
	{ .position = { -0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},
	{ .position = {  0.5f, -0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = {  0.5f, -0.5f,  0.5f }, .uv = { 1.0f, 0.0f }},
	{ .position = {  0.5f, -0.5f,  0.5f }, .uv = { 1.0f, 0.0f }},
	{ .position = { -0.5f, -0.5f,  0.5f }, .uv = { 0.0f, 0.0f }},
	{ .position = { -0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},

	// Top (+Y)
	{ .position = { -0.5f,  0.5f,  0.5f }, .uv = { 0.0f, 0.0f }},
	{ .position = {  0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 0.0f }},
	{ .position = {  0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = {  0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = { -0.5f,  0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},
	{ .position = { -0.5f,  0.5f,  0.5f }, .uv = { 0.0f, 0.0f }}
	};
// clang-format on

static struct State {
	bool resized;
	uint64_t start_time;

	Arena *permanent, *frame, *assets;
} state;

int main(void) {
	state.permanent = arena_alloc();
	state.frame = arena_alloc();
	state.assets = arena_alloc();

	LOG_INFO("Size: %d", sizeof(Vertex));

	uint32_t version = 0;
	vkEnumerateInstanceVersion(&version);
	logger_set_level(LOG_LEVEL_DEBUG);

	LOG_INFO("Vulkan %d.%d.%d", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));

	VulkanContext context = { 0 };
	Platform *platform = platform_startup(state.permanent, 1280, 720, "Starter Vulkan");
	if (platform == NULL) {
		LOG_ERROR("Platform startup failed");
		return -1;
	}

	state.start_time = platform_time_ms(platform);

	platform_set_physical_dimensions_callback(platform, resize_callback);

	vulkan_renderer_create(state.permanent, platform, &context);

	// ================== DYNAMIC ==================

	VertexAttribute attributes[] = {
		{ .name = "in_position", FORMAT_FLOAT3, 0 },
		{ .name = "in_normal", FORMAT_FLOAT3, 1 },
		{ .name = "in_tangent", FORMAT_FLOAT4, 2 },
		{ .name = "in_uv", FORMAT_FLOAT2, 3 },
	};

	vulkan_create_descriptor_set_layout(&context);
	vulkan_create_pipline(&context, attributes, array_count(attributes));

	vulkan_create_texture_image(&context, "assets/models/modular_dungeon/textures/colormap.png");
	vulkan_create_texture_image_view(&context);
	vulkan_create_texture_sampler(&context);

	Mesh *mesh = load_gltf_model(state.assets, GATE_FILE_PATH);
	// Mesh *mesh = load_gltf_model(state.assets, BOT_FILE_PATH);
	// Mesh *mesh = load_gltf_model(state.assets, MAGE_FILE_PATH);

	Buffer *vertex_buffer = vulkan_create_buffer(state.permanent, &context, BUFFER_TYPE_VERTEX, mesh->vertices_count * (12 + 16 + 12 + 8), mesh->positions);
	vertex_buffer->vertex_count = mesh->vertices_count;
	Buffer *index_buffer = vulkan_create_buffer(state.permanent, &context, BUFFER_TYPE_INDEX, sizeof(mesh->indices) * mesh->indices_count, (void *)mesh->indices);
	index_buffer->index_count = mesh->indices_count;

	vulkan_create_uniform_buffers(&context);
	vulkan_create_descriptor_pool(&context);
	vulkan_create_descriptor_set(&context);

	while (platform_should_close(platform) == false) {
		platform_poll_events(platform);
		draw_frame(state.frame, &context, platform, vertex_buffer, index_buffer);
	}

	vkDeviceWaitIdle(context.device.logical);

	return 0;
}

void draw_frame(Arena *arena, VulkanContext *context, Platform *platform, Buffer *vertex_buffer, Buffer *index_buffer) {
	vkWaitForFences(context->device.logical, 1, &context->in_flight_fences[context->current_frame], VK_TRUE, UINT64_MAX);

	uint32_t image_index = 0;
	VkResult result = vkAcquireNextImageKHR(context->device.logical, context->swapchain.handle, UINT64_MAX, context->image_available_semaphores[context->current_frame], VK_NULL_HANDLE, &image_index);

	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		vulkan_recreate_swapchain(context, platform);
		LOG_INFO("Recreating Swapchain");
		return;
	} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		LOG_ERROR("Failed to acquire swapchain image!");
	}

	vkResetFences(context->device.logical, 1, &context->in_flight_fences[context->current_frame]);

	vkResetCommandBuffer(context->command_buffers[context->current_frame], 0);
	vulkan_command_buffer_draw(context, vertex_buffer, index_buffer, image_index);

	update_uniforms(context, platform);

	VkSemaphore wait_semaphores[] = { context->image_available_semaphores[context->current_frame] };
	VkSemaphore signal_semaphores[] = { context->render_finished_semaphores[image_index] };
	VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = array_count(wait_semaphores),
		.pWaitSemaphores = wait_semaphores,
		.pWaitDstStageMask = wait_stages,
		.commandBufferCount = 1,
		.pCommandBuffers = &context->command_buffers[context->current_frame],
		.signalSemaphoreCount = array_count(signal_semaphores),
		.pSignalSemaphores = signal_semaphores
	};

	if (vkQueueSubmit(context->device.graphics_queue, 1, &submit_info, context->in_flight_fences[context->current_frame]) != VK_SUCCESS) {
		LOG_ERROR("Failed to submit draw command buffer");
		return;
	}

	VkSwapchainKHR swapchains[] = { context->swapchain.handle };
	VkPresentInfoKHR present_info = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = signal_semaphores,
		.swapchainCount = 1,
		.pSwapchains = swapchains,
		.pImageIndices = &image_index,
	};

	context->current_frame = (context->current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
	result = vkQueuePresentKHR(context->device.present_queue, &present_info);

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || state.resized) {
		vulkan_recreate_swapchain(context, platform);
		LOG_INFO("Recreating Swapchain");
		state.resized = false;
		return;
	} else if (result != VK_SUCCESS) {
		LOG_ERROR("Failed to acquire swapchain image!");
		return;
	}
}

void update_uniforms(VulkanContext *context, Platform *platform) {
	uint64_t current_time = platform_time_ms(platform);
	double time = (double)(current_time - state.start_time) / 1000.;

	vec3 axis = { 1.0f, 1.0f, 0.0f };

	MVPObject mvp = { 0 };
	glm_mat4_identity(mvp.model);
	// glm_rotate(mvp.model, time, axis);

	vec3 eye = { 0.0f, 0.0f, -20.0f }, center = { 0.0f, 0.0f, 0.0f }, up = { 0.0f, 1.0f, 0.0f };
	glm_mat4_identity(mvp.view);
	glm_lookat(eye, center, up, mvp.view);

	glm_mat4_identity(mvp.projection);
	glm_perspective(glm_rad(45.f), (float)context->swapchain.extent.width / (float)context->swapchain.extent.height, 0.1f, 1000.f, mvp.projection);
	mvp.projection[1][1] *= -1;

	mempcpy(context->uniform_buffers_mapped[context->current_frame], &mvp, sizeof(MVPObject));
}

void resize_callback(Platform *platform, uint32_t width, uint32_t height) {
	state.resized = true;
}

void load_nodes(Arena *arena, cgltf_node *node, Mesh *out_mesh) {
	if (node->mesh && !out_mesh->vertices_count) {
		cgltf_mesh *mesh = node->mesh;
		LOG_DEBUG("Node[%s] has Mesh '%s' with %d primitives", node->name, mesh->name, mesh->primitives_count);
		logger_indent();

		for (uint32_t primitive_index = 0; primitive_index < mesh->primitives_count; ++primitive_index) {
			cgltf_primitive *primitive = &mesh->primitives[primitive_index];

			// ATTRIBUTES
			LOG_DEBUG("Primitive[%d] has %d attributes", primitive_index, primitive->attributes_count);
			logger_indent();
			for (uint32_t attribute_index = 0; attribute_index < primitive->attributes_count; ++attribute_index) {
				cgltf_attribute *attribute = &primitive->attributes[attribute_index];
				LOG_DEBUG("- %s", attribute->name);
				logger_indent();

				cgltf_accessor *accessor = attribute->data;
				cgltf_buffer_view *view = accessor->buffer_view;
				cgltf_buffer *buffer = view->buffer;
				out_mesh->vertices_count = accessor->count;

				LOG_DEBUG("Accessor { offset = %d, count = %d, stride = %d }", accessor->offset, accessor->count, accessor->stride);
				LOG_DEBUG("BufferView { offset = %d, size = %d, stride = %d }", view->offset, view->size, view->stride);
				LOG_DEBUG("Buffer { size = %d }", buffer->size);

				switch (attribute->type) {
					case cgltf_attribute_type_position: {
						out_mesh->positions = arena_push_array(arena, vec3, accessor->count);
						void *src = (uint8_t *)buffer->data + view->offset;

						memcpy(out_mesh->positions, src, view->size);
					} break;
					case cgltf_attribute_type_normal: {
						out_mesh->normals = arena_push_array(arena, vec3, accessor->count);
						void *src = (uint8_t *)buffer->data + view->offset;

						memcpy(out_mesh->normals, src, view->size);
					} break;
					case cgltf_attribute_type_tangent: {
						out_mesh->tangets = arena_push_array(arena, vec4, accessor->count);
						void *src = (uint8_t *)buffer->data + view->offset;

						memcpy(out_mesh->tangets, src, view->size);
					} break;
					case cgltf_attribute_type_texcoord: {
						out_mesh->uvs = arena_push_array(arena, vec2, accessor->count);
						void *src = (uint8_t *)buffer->data + view->offset;

						memcpy(out_mesh->uvs, src, view->size);
					} break;
					case cgltf_attribute_type_invalid:
					case cgltf_attribute_type_color:
					case cgltf_attribute_type_joints:
					case cgltf_attribute_type_weights:
					case cgltf_attribute_type_custom:
					case cgltf_attribute_type_max_enum: {
						logger_dedent();
						continue;
					}
				}
				logger_dedent();

				// ATTRIBUTES
			}
			cgltf_accessor *indices = primitive->indices;
			cgltf_buffer_view *indices_view = indices->buffer_view;
			cgltf_buffer *indices_buffer = indices_view->buffer;

			LOG_DEBUG("Indices Accessor { offset = %d, count = %d, stride = %d }", indices->offset, indices->count, indices->stride);
			LOG_DEBUG("Indices BufferView { offset = %d, size = %d, stride = %d }", indices_view->offset, indices_view->size, indices_view->stride);
			LOG_DEBUG("Indices Buffer { size = %d }", indices_buffer->size);

			out_mesh->indices = arena_push_array(arena, uint32_t, indices->count);
			out_mesh->indices_count = indices->count;
			for (uint32_t index = 0; index < indices->count; ++index) {
				void *data = (uint8_t *)indices_buffer->data + indices_view->offset;
				uint16_t *value = (uint16_t *)data + index;

				out_mesh->indices[index] = *value;
			}
			logger_dedent();
		}
		logger_dedent();
	}

	for (uint32_t node_index = 0; node_index < node->children_count; ++node_index) {
		load_nodes(arena, node->children[node_index], out_mesh);
	}
}

Mesh *load_gltf_model(Arena *arena, const char *path) {
	cgltf_options options = { 0 };
	cgltf_data *data = NULL;
	cgltf_result result = cgltf_parse_file(&options, path, &data);
	cgltf_load_buffers(&options, data, path);
	if (result == cgltf_result_success) {
		LOG_INFO("Loading %s...", path);
		cgltf_scene *scene = data->scene;
		logger_indent();
		Mesh *mesh = arena_push_type_zero(arena, Mesh);
		for (uint32_t node_index = 0; node_index < scene->nodes_count; ++node_index) {
			load_nodes(arena, scene->nodes[node_index], mesh);
			break;
		}
		logger_dedent();
		cgltf_free(data);
		return mesh;
	}
	LOG_ERROR("Failed to load model");
	return NULL;
}
