#include "core/arena.h"
#include "core/logger.h"

#include <loaders/cgltf.h>

#include "platform.h"
#include "vk_renderer.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

void draw_frame(struct arena *arena, VKRenderer *renderer, struct platform *platform);
void resize_callback(struct platform *platform, uint32_t width, uint32_t height);
void update_uniforms(VKRenderer *renderer, Platform *platform);

void load_model(Arena *arena, VKRenderer *renderer);

static bool resized = false;
static uint64_t start_time = 0;

int main(void) {
	Arena *vk_arena = arena_alloc();
	Arena *model_arena = arena_alloc();
	Arena *window_arena = arena_alloc();
	Arena *frame_arena = arena_alloc();

	uint32_t version = 0;
	vkEnumerateInstanceVersion(&version);
	logger_set_level(LOG_LEVEL_INFO);

	LOG_INFO("Vulkan %d.%d.%d", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));

	VKRenderer renderer = { 0 };
	Platform *platform = platform_startup(window_arena, 1280, 720, "Starter Vulkan");
	if (platform == NULL) {
		LOG_ERROR("Platform startup failed");
		return -1;
	}

	start_time = platform_time_ms(platform);

	LOG_INFO("Logical pixel dimensions { %d, %d }", platform->logical_width, platform->logical_height);
	LOG_INFO("Physical pixel dimensions { %d, %d }", platform->physical_width, platform->physical_height);
	platform_physical_dimensions_set_callback(platform, resize_callback);

	vk_create_instance(vk_arena, &renderer, platform);
	vk_create_surface(platform, &renderer);

	vk_select_physical_device(vk_arena, &renderer);
	vk_create_logical_device(vk_arena, &renderer);

	vk_create_swapchain(vk_arena, &renderer, platform);
	vk_create_swapchain_image_views(vk_arena, &renderer);
	vk_create_render_pass(&renderer);
	vk_create_depth_resources(vk_arena, &renderer);
	vk_create_framebuffers(vk_arena, &renderer);

	vk_create_descriptor_set_layout(&renderer);
	vk_create_graphics_pipline(vk_arena, &renderer);

	vk_create_command_pool(vk_arena, &renderer);

	vk_create_texture_image(vk_arena, &renderer);
	vk_create_texture_image_view(vk_arena, &renderer);
	vk_create_texture_sampler(&renderer);

	load_model(model_arena, &renderer);
	vk_create_vertex_buffer(vk_arena, &renderer);
	vk_create_index_buffer(vk_arena, &renderer);

	vk_create_uniform_buffers(vk_arena, &renderer);
	vk_create_descriptor_pool(&renderer);
	vk_create_descriptor_set(&renderer);

	vk_create_command_buffer(&renderer);
	vk_create_sync_objects(&renderer);

	while (platform_should_close(platform) == false) {
		platform_poll_events(platform);
		draw_frame(frame_arena, &renderer, platform);
	}

	vkDeviceWaitIdle(renderer.logical_device);

	return 0;
}

void draw_frame(struct arena *arena, VKRenderer *renderer, struct platform *platform) {
	vkWaitForFences(renderer->logical_device, 1, &renderer->in_flight_fences[renderer->current_frame], VK_TRUE, UINT64_MAX);

	uint32_t image_index = 0;
	VkResult result = vkAcquireNextImageKHR(renderer->logical_device, renderer->swapchain.handle, UINT64_MAX, renderer->image_available_semaphores[renderer->current_frame], VK_NULL_HANDLE, &image_index);

	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		vk_recreate_swapchain(arena, renderer, platform);
		LOG_INFO("Recreating Swapchain");
		return;
	} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		LOG_ERROR("Failed to acquire swapchain image!");
	}

	vkResetFences(renderer->logical_device, 1, &renderer->in_flight_fences[renderer->current_frame]);

	vkResetCommandBuffer(renderer->command_buffers[renderer->current_frame], 0);
	vk_record_command_buffers(renderer, image_index);

	update_uniforms(renderer, platform);

	VkSemaphore wait_semaphores[] = { renderer->image_available_semaphores[renderer->current_frame] };
	VkSemaphore signal_semaphores[] = { renderer->render_finished_semaphores[image_index] };
	VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = array_count(wait_semaphores),
		.pWaitSemaphores = wait_semaphores,
		.pWaitDstStageMask = wait_stages,
		.commandBufferCount = 1,
		.pCommandBuffers = &renderer->command_buffers[renderer->current_frame],
		.signalSemaphoreCount = array_count(signal_semaphores),
		.pSignalSemaphores = signal_semaphores
	};

	if (vkQueueSubmit(renderer->graphics_queue, 1, &submit_info, renderer->in_flight_fences[renderer->current_frame]) != VK_SUCCESS) {
		LOG_ERROR("Failed to submit draw command buffer");
		return;
	}

	VkSwapchainKHR swapchains[] = { renderer->swapchain.handle };
	VkPresentInfoKHR present_info = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = signal_semaphores,
		.swapchainCount = 1,
		.pSwapchains = swapchains,
		.pImageIndices = &image_index,
	};

	renderer->current_frame = (renderer->current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
	result = vkQueuePresentKHR(renderer->present_queue, &present_info);

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || resized) {
		vk_recreate_swapchain(arena, renderer, platform);
		LOG_INFO("Recreating Swapchain");
		resized = false;
		return;
	} else if (result != VK_SUCCESS) {
		LOG_ERROR("Failed to acquire swapchain image!");
		return;
	}
}

void update_uniforms(VKRenderer *renderer, Platform *platform) {
	uint64_t current_time = platform_time_ms(platform);
	double time = (double)(current_time - start_time) / 1000.;

	vec3 axis = { 1.0f, 1.0f, 0.0f };

	MVPObject mvp = { 0 };
	glm_mat4_identity(mvp.model);
	glm_rotate(mvp.model, time, axis);

	// if (input_is_key_pressed(KEY_W))
	// LOG_INFO("Key W pressed");

	vec3 eye = { 0.0f, 0.0f, 3.0f }, center = { 0.0f, 0.0f, 0.0f }, up = { 0.0f, 1.0f, 0.0f };
	glm_mat4_identity(mvp.view);
	glm_lookat(eye, center, up, mvp.view);

	glm_mat4_identity(mvp.projection);
	glm_perspective(glm_rad(45.f), (float)renderer->swapchain.extent.width / (float)renderer->swapchain.extent.height, 0.1f, 1000.f, mvp.projection);
	mvp.projection[1][1] *= -1;

	mempcpy(renderer->uniform_buffers_mapped[renderer->current_frame], &mvp, sizeof(MVPObject));
}

void resize_callback(struct platform *platform, uint32_t width, uint32_t height) {
	resized = true;
}

VertexAttributeFormat cgf_to_vaf(cgltf_type type) {
	switch (type) {
		case cgltf_type_scalar:
			return ATTRIBUTE_FORMAT_FLOAT;
		case cgltf_type_vec2:
			return ATTRIBUTE_FORMAT_FLOAT2;
		case cgltf_type_vec3:
			return ATTRIBUTE_FORMAT_FLOAT3;
		case cgltf_type_vec4:
			return ATTRIBUTE_FORMAT_FLOAT4;
		case cgltf_type_invalid:
		case cgltf_type_mat2:
		case cgltf_type_mat3:
		case cgltf_type_mat4:
		case cgltf_type_max_enum:
		default:
			return ATTRIBUTE_FORMAT_INVALID;
	};
}

static const char *cgltf_type_to_string[] = {
	"cgltf_type_invalid",
	"cgltf_type_scalar",
	"cgltf_type_vec2",
	"cgltf_type_vec3",
	"cgltf_type_vec4",
	"cgltf_type_mat2",
	"cgltf_type_mat3",
	"cgltf_type_mat4",
	"cgltf_type_max_enum"
};

void create_model(Arena *arena, VKRenderer *renderer, cgltf_node *node, uint32_t depth) {
	if (node->mesh != NULL && renderer->mesh.primitves == NULL) {
		// TODO: Make Vulkan Buffers
		cgltf_mesh *mesh = node->mesh;

		Mesh *upload_mesh = &renderer->mesh;
		upload_mesh->primitves = arena_push_array(arena, Primitive, mesh->primitives_count);
		for (uint32_t primitive_index = 0; primitive_index < mesh->primitives_count; ++primitive_index) {
			cgltf_primitive primitive = mesh->primitives[primitive_index];
			cgltf_material *material = primitive.material;
			Primitive *upload_primitive = &upload_mesh->primitves[primitive_index];

			upload_primitive->index_count = primitive.indices->count;
			upload_primitive->vertex_count = primitive.attributes[0].data->count;

			upload_primitive->indices = arena_push_array(arena, uint32_t, upload_primitive->index_count);
			upload_primitive->vertices = arena_push_array(arena, Vertex, upload_primitive->vertex_count);

			uint8_t *index_buffer = (uint8_t *)primitive.indices->buffer_view->buffer->data + (primitive.indices->offset + primitive.indices->buffer_view->offset);
			for (uint32_t index = 0; index < primitive.indices->count; ++index) {
				cgltf_accessor *accessor = primitive.indices;

				uint8_t *value = index_buffer + (index * primitive.indices->stride);

				LOG_INFO("Indices[%d] = %d", index, *(uint16_t *)value);
				memcpy(upload_primitive->indices + index, value, primitive.indices->stride);
			}

			for (uint32_t attribute_index = 0; attribute_index < primitive.attributes_count; ++attribute_index) {
				cgltf_attribute attribute = primitive.attributes[attribute_index];
				if (!(attribute.type == cgltf_attribute_type_position || attribute.type == cgltf_attribute_type_normal || attribute.type == cgltf_attribute_type_texcoord))
					continue;

				for (uint32_t index = 0; index < upload_primitive->vertex_count; ++index) {
					uint32_t stride = attribute.data->stride;
					Vertex *vertex = &upload_primitive->vertices[index];
					uint8_t *data = (uint8_t *)attribute.data->buffer_view->buffer->data + (attribute.data->buffer_view->offset + attribute.data->offset);
					float *value = (float *)(data + (stride * index));

					if (attribute.type == cgltf_attribute_type_position)
						memcpy(vertex->position, value, stride);
					if (attribute.type == cgltf_attribute_type_normal)
						memcpy(vertex->normal, value, stride);
					if (attribute.type == cgltf_attribute_type_texcoord)
						memcpy(vertex->texture_coordinate, value, stride);
				}
			}
		}

		for (uint32_t index = 0; index < upload_mesh->primitves->vertex_count; ++index) {
			Vertex vertex = upload_mesh->primitves->vertices[index];
			LOG_INFO("Vertex { position = { %.2f, %.2f, %.2f }, normal = { %2f. %.2f, %2f }, texture_coordinate = { %.2f, %.2f }}",
				vertex.position[0], vertex.position[1], vertex.position[2],
				vertex.normal[0], vertex.normal[1], vertex.normal[2],
				vertex.texture_coordinate[0], vertex.texture_coordinate[1]);
		}

		for (uint32_t primitive_index = 0; primitive_index < mesh->primitives_count; ++primitive_index) {
			cgltf_primitive primitive = mesh->primitives[primitive_index];

			VertexAttribute attributes[primitive.attributes_count];

			uint32_t vertex_size = 0;

			LOG_INFO("==========================================");
			LOG_INFO("'%s' Primitive[%d], INDEX_COUNT = %d", mesh->name, primitive_index, primitive.indices->count);
			for (uint32_t attribute_index = 0; attribute_index < primitive.attributes_count; ++attribute_index) {
				cgltf_attribute attribute = primitive.attributes[attribute_index];
				cgltf_accessor *accessor = attribute.data;

				LOG_INFO("[%d, %s] %s", attribute_index, attribute.name, cgltf_type_to_string[attribute.data->type]);
				LOG_INFO("Accessor: Count = %d, Stride = %d, Offset = %d", accessor->count, accessor->stride, accessor->offset);
				LOG_INFO("BufferView: Offset = %d, Size = %d", accessor->buffer_view->offset, accessor->buffer_view->size);

				attributes[attribute_index] = (VertexAttribute){
					.format = cgf_to_vaf(attribute.data->type)
				};
				if (attribute.type == cgltf_attribute_type_position) {
				}

				vertex_size += vaf_to_byte_size(attributes[attribute_index].format);
				for (uint32_t index = 0; index < accessor->buffer_view->size; index += accessor->stride) {
				}
			}

			// LOG_INFO("Primitive single vertex size = %d", vertex_size);
		}
	}

	for (uint32_t child_index = 0; child_index < node->children_count; ++child_index) {
		cgltf_node *child = node->children[child_index];

		create_model(arena, renderer, child, depth + 1);
	}
}

void load_model(Arena *arena, VKRenderer *renderer) {
	const char file[] = "assets/models/Box.glb";
	cgltf_options options = { 0 };
	cgltf_data *data = NULL;
	cgltf_result result = cgltf_parse_file(&options, file, &data);
	if (result != cgltf_result_success) {
		LOG_ERROR("Failed to load model with error code %d", result);
	}

	result = cgltf_load_buffers(&options, data, file);
	result = cgltf_validate(data);
	if (result != cgltf_result_success) {
		LOG_ERROR("Failed to load validate with error code %d", result);
	}

	for (uint32_t scene_index = 0; scene_index < data->scenes_count; ++scene_index) {
		if (data->scenes + scene_index != data->scene)
			continue;

		cgltf_scene scene = data->scenes[scene_index];
		for (uint32_t root_index = 0; root_index < scene.nodes_count; ++root_index) {
			cgltf_node *root = scene.nodes[root_index];
			LOG_INFO("======SCENE_NODE[%d]======", root_index);

			create_model(arena, renderer, root, 0);
		}
	}

	cgltf_free(data);
	return;
}
