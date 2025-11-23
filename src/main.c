#include "platform.h"

#include "event.h"

#include "input.h"
#include "input/input_types.h"

#include "renderer/renderer_types.h"
#include "renderer/vk_renderer.h"

#include <cgltf/cgltf.h>

#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

#include "common.h"
#include "core/arena.h"
#include "core/logger.h"

#include <string.h>

#define GATE_FILE_PATH "assets/models/modular_dungeon/gate.glb"
#define GATE_DOOR_FILE_PATH "assets/models/modular_dungeon/gate-door.glb"
#define BOT_FILE_PATH "assets/models/characters/gdbot.glb"
#define MAGE_FILE_PATH "assets/models/characters/mage.glb"

Model *load_gltf_model(Arena *arena, const char *path);
bool resize_event(Event *event);
void resize_callback(Platform *platform, uint32_t width, uint32_t height);
void update_uniforms(VulkanContext *context, Platform *platform);
void get_filename(const char *src, char *dst);
void get_path(const char *src, char *dst);

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

	event_system_startup();
	input_system_startup();

	LOG_INFO("Vulkan %d.%d.%d", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));

	VulkanContext context = { 0 };
	Platform *platform = platform_startup(state.permanent, 1280, 720, "Starter Vulkan");
	if (platform == NULL) {
		LOG_ERROR("Platform startup failed");
		return -1;
	}

	platform_pointer_mode(platform, PLATFORM_POINTER_DISABLED);

	state.start_time = platform_time_ms(platform);
	event_subscribe(SV_EVENT_WINDOW_RESIZED, resize_event);

	vulkan_renderer_create(state.permanent, platform, &context);

	// ================== DYNAMIC ==================

	VertexAttribute attributes[] = {
		{ .name = "in_position", FORMAT_FLOAT3, 0 },
		{ .name = "in_uv", FORMAT_FLOAT2, 0 },
		{ .name = "in_normal", FORMAT_FLOAT3, 0 },
	};

	vulkan_create_descriptor_set_layout(&context);
	vulkan_create_pipline(&context, "./assets/shaders/vs_default.spv", "./assets/shaders/fs_default.spv", attributes, array_count(attributes));

	// Model *model = load_gltf_model(state.assets, GATE_FILE_PATH);
	// Model *model = load_gltf_model(state.assets, GATE_DOOR_FILE_PATH);
	Model *model = load_gltf_model(state.assets, BOT_FILE_PATH);
	// Model *model = load_gltf_model(state.assets, MAGE_FILE_PATH);

	LOG_DEBUG("Model path to diffuse color is: '%s'", model->primitives->material.base_color_texture.path);
	const char *file_path = model->primitives->material.base_color_texture.path;
	char file_name[256];
	get_filename(file_path, file_name);
	LOG_INFO("Loading %s...", file_name);
	logger_indent();
	int32_t width, height, channels;
	uint8_t *pixels = stbi_load(file_path, &width, &height, &channels, STBI_rgb_alpha);

	if (pixels == NULL) {
		LOG_ERROR("Failed to load image [ %s ]", file_path);
		return -1;
	}

	LOG_DEBUG("%s: { width = %d, height = %d, channels = %d }", file_name, width, height, channels);
	LOG_INFO("%s loaded", file_name);
	logger_dedent();

	vulkan_create_texture_image(&context, width, height, channels, pixels);
	vulkan_create_texture_image_view(&context);
	vulkan_create_texture_sampler(&context);

	stbi_image_free(pixels);

	Buffer *vertex_buffers[model->primitive_count];
	Buffer *index_buffers[model->primitive_count];

	for (uint32_t index = 0; index < model->primitive_count; ++index) {
		vertex_buffers[index] = vulkan_buffer_create(state.permanent, &context, BUFFER_TYPE_VERTEX, model->primitives[index].vertex_count * sizeof(Vertex), model->primitives[index].vertices);
		vertex_buffers[index]->vertex_count = model->primitives[index].vertex_count;

		index_buffers[index] = vulkan_buffer_create(state.permanent, &context, BUFFER_TYPE_INDEX, sizeof(model->primitives[index].indices) * model->primitives->index_count, (void *)model->primitives[index].indices);
		index_buffers[index]->index_count = model->primitives[index].index_count;
	}

	vulkan_create_uniform_buffers(&context);
	vulkan_create_descriptor_pool(&context);
	vulkan_create_descriptor_set(&context);

	while (platform_should_close(platform) == false) {
		platform_poll_events(platform);
		vulkan_renderer_begin_frame(&context, platform);

		for (uint32_t index = 0; index < model->primitive_count; ++index) {
			vulkan_renderer_draw_indexed(&context, vertex_buffers[index], index_buffers[index]);
		}

		Vulkan_renderer_end_frame(&context);

		update_uniforms(&context, platform);

		LOG_INFO("MouseMotion { x = %.2f, y = %.2f, dx = %.2f, dy = %.2f}", input_mouse_x(), input_mouse_y(), input_mouse_delta_x(), input_mouse_delta_y());

		if (input_mouse_pressed(SV_MOUSE_BUTTON_LEFT))
			platform_pointer_mode(platform, PLATFORM_POINTER_NORMAL);
		if (input_mouse_pressed(SV_MOUSE_BUTTON_RIGHT))
			platform_pointer_mode(platform, PLATFORM_POINTER_DISABLED);

		if (state.resized) {
			LOG_INFO("Recreating Swapchain...");
			logger_indent();
			if (vulkan_recreate_swapchain(&context, platform) == true) {
				LOG_INFO("Swapchain successfully recreated");
			} else {
				LOG_WARN("Failed to recreate swapchain");
			}

			logger_dedent();
			state.resized = false;
		}

		input_system_update();
	}

	input_system_shutdown();
	event_system_shutdown();

	vkDeviceWaitIdle(context.device.logical);

	return 0;
}

void update_uniforms(VulkanContext *context, Platform *platform) {
	uint64_t current_time = platform_time_ms(platform);
	double time = (double)(current_time - state.start_time) / 1000.;

	vec3 axis = { 1.0f, 1.0f, 0.0f };

	MVPObject mvp = { 0 };
	glm_mat4_identity(mvp.model);
	// glm_rotate(mvp.model, time, axis);

	vec3 eye = { 0.0f, 0.0f, -10.0f }, center = { 0.0f, 0.0f, 0.0f }, up = { 0.0f, 1.0f, 0.0f };
	glm_mat4_identity(mvp.view);
	glm_lookat(eye, center, up, mvp.view);

	glm_mat4_identity(mvp.projection);
	glm_perspective(glm_rad(45.f), (float)context->swapchain.extent.width / (float)context->swapchain.extent.height, 0.1f, 1000.f, mvp.projection);
	mvp.projection[1][1] *= -1;

	mempcpy(context->uniform_buffers_mapped[context->current_frame], &mvp, sizeof(MVPObject));
}

bool resize_event(Event *event) {
	state.resized = true;
	return true;
}

void load_nodes(Arena *arena, cgltf_node *node, uint32_t *total_primitives, Model *out_model, const char *relative_path) {
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
			LOG_DEBUG("---PRIMITIVE[%d]---", primitive_index, primitive->attributes_count);
			logger_indent();
			for (uint32_t attribute_index = 0; attribute_index < primitive->attributes_count; ++attribute_index) {
				cgltf_attribute *attribute = &primitive->attributes[attribute_index];
				if (out_primitive->vertices == NULL)
					out_primitive->vertices = arena_push_array_zero(arena, Vertex, attribute->data->count);
				LOG_DEBUG("Attribute[%d]- %s", attribute_index, attribute->name);
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
						float *src = (float *)((uint8_t *)buffer->data + view->offset);

						for (uint32_t index = 0; index < accessor->count; ++index) {
							out_primitive->vertices[index].position[0] = src[index * 3 + 0];
							out_primitive->vertices[index].position[1] = src[index * 3 + 1];
							out_primitive->vertices[index].position[2] = src[index * 3 + 2];
						}
					} break;
					case cgltf_attribute_type_normal: {
						float *src = (float *)((uint8_t *)buffer->data + view->offset);

						for (uint32_t index = 0; index < accessor->count; ++index) {
							out_primitive->vertices[index].normal[0] = src[index * 3 + 0];
							out_primitive->vertices[index].normal[1] = src[index * 3 + 1];
							out_primitive->vertices[index].normal[2] = src[index * 3 + 2];
						}
					} break;
					case cgltf_attribute_type_texcoord: {
						float *src = (float *)((uint8_t *)buffer->data + view->offset);

						for (uint32_t index = 0; index < accessor->count; ++index) {
							out_primitive->vertices[index].uv[0] = src[index * 2 + 0];
							out_primitive->vertices[index].uv[1] = src[index * 2 + 1];
						}
					} break;
					case cgltf_attribute_type_invalid:
					case cgltf_attribute_type_tangent:
					case cgltf_attribute_type_color:
					case cgltf_attribute_type_joints:
					case cgltf_attribute_type_weights:
					case cgltf_attribute_type_custom:
					case cgltf_attribute_type_max_enum:
						break;
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
				Material *out_material = &out_primitive->material;
				LOG_DEBUG("Material is Metallic Roughness PBR based");
				logger_indent();

				cgltf_pbr_metallic_roughness *pbr_material = &material->pbr_metallic_roughness;

				if (pbr_material->base_color_texture.texture) {
					cgltf_texture_view base_color_view = pbr_material->base_color_texture;
					cgltf_texture *base_color_texture = base_color_view.texture;
					cgltf_image *base_color = base_color_texture->image;

					Texture *out_texture = &out_material->base_color_texture;
					out_texture->path = arena_push_array_zero(arena, char, MAX_FILE_PATH_LENGTH);

					if (base_color->buffer_view) {
						cgltf_buffer_view *view = base_color->buffer_view;
						cgltf_buffer *buffer = view->buffer;

						LOG_DEBUG("%s BufferView { offset = %d, size = %d, stride = %d }", base_color->name, view->offset, view->size, view->stride);
						LOG_DEBUG("%s Buffer { size = %d }", base_color->name, buffer->size);

						uint32_t length = 0;
						char c;

						uint8_t *src = (uint8_t *)buffer->data + view->offset;

						while ((c = base_color->mime_type[length++]) != '\0') {
							if (c == '/' || c == '\\') {
								if (strcmp(&base_color->mime_type[length], "png") == 0) {
									LOG_DEBUG("MimeType should be png (it is '%s')", base_color->mime_type);

									strncpy(out_texture->path, relative_path, MAX_FILE_PATH_LENGTH);
									strncat(out_texture->path, node->name, MAX_FILE_NAME_LENGTH);
									out_texture->path[strnlen(out_texture->path, MAX_FILE_PATH_LENGTH)] = '_';
									out_texture->path[strnlen(out_texture->path, MAX_FILE_PATH_LENGTH) + 1] = '\0';
									strncat(out_texture->path, base_color->name, MAX_FILE_PATH_LENGTH);
									strncat(out_texture->path, ".png", MAX_FILE_PATH_LENGTH);

									uint8_t *pixels = stbi_load_from_memory(src, view->size, (int32_t *)&out_texture->width, (int32_t *)&out_texture->height, (int32_t *)&out_texture->channels, 4);
									stbi_write_png(out_texture->path, out_texture->width, out_texture->height, out_texture->channels, pixels, out_texture->width);
								}
								if (strcmp(&base_color->mime_type[length], "jpeg") == 0) {
									LOG_DEBUG("MimeType should be jpeg (it is '%s')", base_color->mime_type);
								}
							}
						}
					} else {
						LOG_DEBUG("URI '%s'[%s]", base_color->name, base_color->uri);
						strncpy(out_texture->path, relative_path, MAX_FILE_PATH_LENGTH);
						strncat(out_texture->path, base_color->uri, MAX_FILE_NAME_LENGTH);
					}
				}

				if (pbr_material->metallic_roughness_texture.texture) {
					cgltf_texture_view metallic_roughness_view = pbr_material->metallic_roughness_texture;
					cgltf_texture *metallic_roughness_texture = metallic_roughness_view.texture;
					cgltf_image *metallic_roughness = metallic_roughness_texture->image;

					if (metallic_roughness->buffer_view) {
						cgltf_buffer_view *view = metallic_roughness->buffer_view;
						cgltf_buffer *buffer = view->buffer;

						LOG_DEBUG("%s BufferView { offset = %d, size = %d, stride = %d }", metallic_roughness->name, view->offset, view->size, view->stride);
						LOG_DEBUG("%s Buffer { size = %d }", metallic_roughness->name, buffer->size);

						LOG_INFO("MimeType: %s", metallic_roughness->mime_type);
					} else {
						LOG_DEBUG("URI '%s'[%s]", metallic_roughness->name, metallic_roughness->uri);
					}
				}
				logger_dedent();

				if (material->emissive_texture.texture) {
					LOG_DEBUG("Has emission texture");
				}
			} else {
				LOG_WARN("Non Metallic Roughness PBR material, loading skipped");
			}
			logger_dedent();
		}
		logger_dedent();
	}

skip_loading:

	for (uint32_t node_index = 0; node_index < node->children_count; ++node_index) {
		load_nodes(arena, node->children[node_index], total_primitives, out_model, relative_path);
	}
}

Model *load_gltf_model(Arena *arena, const char *path) {
	cgltf_options options = { 0 };
	cgltf_data *data = NULL;
	cgltf_result result = cgltf_parse_file(&options, path, &data);

	if (result == cgltf_result_success)
		result = cgltf_load_buffers(&options, data, path);

	if (result == cgltf_result_success)
		result = cgltf_validate(data);

	if (result == cgltf_result_success) {
		LOG_INFO("Loading %s...", path);
		cgltf_scene *scene = data->scene;
		logger_indent();
		Model *model = arena_push_type_zero(arena, Model);
		for (uint32_t node_index = 0; node_index < scene->nodes_count; ++node_index) {
			uint32_t primitive_count = 0;
			load_nodes(arena, scene->nodes[node_index], &primitive_count, model, NULL);
			model->primitives = arena_push_array_zero(arena, RenderPrimitive, primitive_count);
			char directory[MAX_FILE_PATH_LENGTH];
			get_path(path, directory);
			load_nodes(arena, scene->nodes[node_index], &primitive_count, model, directory);
			break;
		}
		logger_dedent();
		cgltf_free(data);
		return model;
	}
	LOG_ERROR("Failed to load model");
	return NULL;
}

void get_filename(const char *src, char *dst) {
	uint32_t start = 0, length = 0;
	char c;

	while ((c = src[length++]) != '\0') {
		if (c == '/' || c == '\\') {
			if (src[length] == '\0') {
				LOG_INFO("'%s' is not a file");
				return;
			}
			start = length;
		}
	}

	memcpy(dst, src + start, length - start);
}

void get_path(const char *src, char *dst) {
	uint32_t final = 0, length = 0;
	char c;

	while ((c = src[length++]) != '\0') {
		if (c == '/' || c == '\\') {
			final = length;
		}
	}

	memcpy(dst, src, final);
	dst[final] = '\0';
}
