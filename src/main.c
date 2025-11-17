#include "platform.h"

#include "renderer/renderer_types.h"
#include "renderer/vk_renderer.h"

#include <loaders/cgltf.h>

#include "common.h"
#include "core/arena.h"
#include "core/logger.h"

#include <string.h>

#define GATE_FILE_PATH "assets/models/modular_dungeon/gate.glb"
#define GATE_DOOR_FILE_PATH "assets/models/modular_dungeon/gate-door.glb"
#define BOT_FILE_PATH "assets/models/characters/gdbot.glb"
#define MAGE_FILE_PATH "assets/models/characters/mage.glb"

Model *load_gltf_model(Arena *arena, const char *path);
void draw_frame(Arena *arena, VulkanContext *context, Platform *platform, uint32_t buffer_count, Buffer **vertex_buffer, Buffer **index_buffer);
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
	vulkan_create_pipline(&context, "./assets/shaders/vs_default.spv", "./assets/shaders/fs_default.spv", attributes, array_count(attributes));

	// Mesh *mesh = load_gltf_model(state.assets, GATE_FILE_PATH);
	Model *model = load_gltf_model(state.assets, GATE_DOOR_FILE_PATH);
	// Mesh *mesh = load_gltf_model(state.assets, BOT_FILE_PATH);
	// Mesh *mesh = load_gltf_model(state.assets, MAGE_FILE_PATH);

	vulkan_create_texture_image(&context, "assets/models/modular_dungeon/textures/colormap.png");
	vulkan_create_texture_image_view(&context);
	vulkan_create_texture_sampler(&context);

	Buffer *vertex_buffers[model->primitive_count];
	Buffer *index_buffers[model->primitive_count];

	for (uint32_t index = 0; index < model->primitive_count; ++index) {
		vertex_buffers[index] = vulkan_create_buffer(state.permanent, &context, BUFFER_TYPE_VERTEX, model->primitives[index].vertex_count * (12 + 16 + 12 + 8), model->primitives[index].positions);
		vertex_buffers[index]->vertex_count = model->primitives[index].vertex_count;

		index_buffers[index] = vulkan_create_buffer(state.permanent, &context, BUFFER_TYPE_INDEX, sizeof(model->primitives[index].indices) * model->primitives->index_count, (void *)model->primitives[index].indices);
		index_buffers[index]->index_count = model->primitives[index].index_count;
	}

	vulkan_create_uniform_buffers(&context);
	vulkan_create_descriptor_pool(&context);
	vulkan_create_descriptor_set(&context);

	while (platform_should_close(platform) == false) {
		platform_poll_events(platform);
		draw_frame(state.frame, &context, platform, model->primitive_count, vertex_buffers, index_buffers);
	}

	vkDeviceWaitIdle(context.device.logical);

	return 0;
}

void draw_frame(Arena *arena, VulkanContext *context, Platform *platform, uint32_t buffer_count, Buffer **vertex_buffer, Buffer **index_buffer) {
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
	vulkan_command_buffer_draw(context, vertex_buffer[0], index_buffer[0], image_index);

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

void load_nodes(Arena *arena, cgltf_node *node, uint32_t *total_primitives, Model *out_model) {
	if (node->mesh) {
		cgltf_mesh *mesh = node->mesh;
		*total_primitives += mesh->primitives_count;
		LOG_DEBUG("Node[%s] has Mesh '%s' with %d primitives", node->name, mesh->name, mesh->primitives_count);
		if (out_model->primitives == NULL)
			goto skip_loading;
		logger_indent();

		for (uint32_t primitive_index = 0; primitive_index < mesh->primitives_count; ++primitive_index) {
			cgltf_primitive *primitive = &mesh->primitives[primitive_index];
			RenderPrimitive *out_primitive = &out_model->primitives[out_model->primitive_count++];

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
				out_primitive->vertex_count = accessor->count;

				LOG_DEBUG("Accessor { offset = %d, count = %d, stride = %d }", accessor->offset, accessor->count, accessor->stride);
				LOG_DEBUG("BufferView { offset = %d, size = %d, stride = %d }", view->offset, view->size, view->stride);
				LOG_DEBUG("Buffer { size = %d }", buffer->size);

				switch (attribute->type) {
					case cgltf_attribute_type_position: {
						out_primitive->positions = arena_push_array(arena, vec3, accessor->count);
						void *src = (uint8_t *)buffer->data + view->offset;

						memcpy(out_primitive->positions, src, view->size);
					} break;
					case cgltf_attribute_type_normal: {
						out_primitive->normals = arena_push_array(arena, vec3, accessor->count);
						void *src = (uint8_t *)buffer->data + view->offset;

						memcpy(out_primitive->normals, src, view->size);
					} break;
					case cgltf_attribute_type_tangent: {
						out_primitive->tangets = arena_push_array(arena, vec4, accessor->count);
						void *src = (uint8_t *)buffer->data + view->offset;

						memcpy(out_primitive->tangets, src, view->size);
					} break;
					case cgltf_attribute_type_texcoord: {
						out_primitive->uvs = arena_push_array(arena, vec2, accessor->count);
						void *src = (uint8_t *)buffer->data + view->offset;

						memcpy(out_primitive->uvs, src, view->size);
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

			out_primitive->indices = arena_push_array(arena, uint32_t, indices->count);
			out_primitive->index_count = indices->count;
			for (uint32_t index = 0; index < indices->count; ++index) {
				void *data = (uint8_t *)indices_buffer->data + indices_view->offset;
				uint16_t *value = (uint16_t *)data + index;

				out_primitive->indices[index] = *value;
			}

			cgltf_material *material = primitive->material;
			if (material->has_pbr_metallic_roughness) {
				LOG_DEBUG("Material is PBR Metallic Roughness based");

				cgltf_pbr_metallic_roughness *pbr_material = &material->pbr_metallic_roughness;

				cgltf_texture_view base_color_view = pbr_material->base_color_texture;
				cgltf_texture *base_color_texture = base_color_view.texture;
				cgltf_image *base_color = base_color_texture->image;

				LOG_DEBUG("BaseColorTexture: '%s'[%s]", base_color_texture->name, base_color->uri);
			}

			logger_dedent();
		}
		logger_dedent();
	}

skip_loading:

	for (uint32_t node_index = 0; node_index < node->children_count; ++node_index) {
		load_nodes(arena, node->children[node_index], total_primitives, out_model);
	}
}

Model *load_gltf_model(Arena *arena, const char *path) {
	cgltf_options options = { 0 };
	cgltf_data *data = NULL;
	cgltf_result result = cgltf_parse_file(&options, path, &data);
	cgltf_load_buffers(&options, data, path);
	if (result == cgltf_result_success) {
		LOG_INFO("Loading %s...", path);
		cgltf_scene *scene = data->scene;
		logger_indent();
		Model *model = arena_push_type_zero(arena, Model);
		for (uint32_t node_index = 0; node_index < scene->nodes_count; ++node_index) {
			uint32_t primitive_count = 0;
			load_nodes(arena, scene->nodes[node_index], &primitive_count, model);
			model->primitives = arena_push_array_zero(arena, RenderPrimitive, primitive_count);
			load_nodes(arena, scene->nodes[node_index], &primitive_count, model);
			break;
		}
		logger_dedent();
		cgltf_free(data);
		return model;
	}
	LOG_ERROR("Failed to load model");
	return NULL;
}
