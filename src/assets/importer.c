#include "importer.h"

#include "renderer/renderer_types.h"

#include <cgltf/cgltf.h>
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

#include "allocators/arena.h"
#include "core/logger.h"

#include <string.h>

bool file_exists(const char *path);
void file_name(const char *src, char *dst);
void file_path(const char *src, char *dst);

static void load_nodes(Arena *arena, cgltf_node *node, uint32_t *total_primitives, Model *out_model, const char *relative_path);

Model *importer_load_gltf(struct arena *arena, const char *path) {
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
		Model *model = arena_push_struct_zero(arena, Model);
		for (uint32_t node_index = 0; node_index < scene->nodes_count; ++node_index) {
			uint32_t primitive_count = 0;
			load_nodes(arena, scene->nodes[node_index], &primitive_count, model, NULL);
			model->primitives = arena_push_array_zero(arena, RenderPrimitive, primitive_count);
			char directory[MAX_FILE_PATH_LENGTH];
			file_path(path, directory);
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

Image importer_load_image(const char *path) {
	Image image;
	char filename[256];
	file_name(path, filename);
	LOG_INFO("Loading %s...", filename);
	logger_indent();
	image.pixels = stbi_load(path, &image.width, &image.height, &image.channels, STBI_default);

	if (image.pixels == NULL) {
		LOG_ERROR("Failed to load image [ %s ]", path);
		return (Image){ 0 };
	}

	LOG_DEBUG("%s: { width = %d, height = %d, channels = %d }", filename, image.width, image.height, image.channels);
	LOG_INFO("%s loaded", filename);
	logger_dedent();

	return image;
}

void importer_unload_image(Image image) {
	if (image.pixels)
		stbi_image_free(image.pixels);
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
						uint8_t *data = (uint8_t *)buffer->data + view->offset;
						size_t stride = view->stride ? view->stride : accessor->stride;

						for (uint32_t index = 0; index < accessor->count; ++index) {
							float *src = (float *)(data + index * stride);
							out_primitive->vertices[index].uv[0] = src[0];
							out_primitive->vertices[index].uv[1] = src[1];
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
					cgltf_texture_view texture_view = pbr_material->base_color_texture;

					cgltf_texture *texture = texture_view.texture;
					cgltf_sampler *sampler = texture->sampler;

					cgltf_image *image = texture->image;

					Texture *out_texture = &out_material->base_color_texture;
					out_texture->path = arena_push_array_zero(arena, char, MAX_FILE_PATH_LENGTH);

					if (image->buffer_view) {
						cgltf_buffer_view *buffer_view = image->buffer_view;
						cgltf_buffer *buffer = buffer_view->buffer;

						LOG_DEBUG("%s BufferView { offset = %d, size = %d, stride = %d }", image->name, buffer_view->offset, buffer_view->size, buffer_view->stride);
						LOG_DEBUG("%s Buffer { size = %d }", image->name, buffer->size);

						uint32_t length = 0;
						char c;

						uint8_t *src = (uint8_t *)buffer->data + buffer_view->offset;

						while ((c = image->mime_type[length++]) != '\0') {
							if (c == '/' || c == '\\') {
								if (strcmp(&image->mime_type[length], "png") == 0) {
									LOG_DEBUG("MimeType should be png (it is '%s')", image->mime_type);

									strncpy(out_texture->path, relative_path, MAX_FILE_PATH_LENGTH);
									strncat(out_texture->path, node->name, MAX_FILE_NAME_LENGTH);
									out_texture->path[strnlen(out_texture->path, MAX_FILE_PATH_LENGTH)] = '_';
									out_texture->path[strnlen(out_texture->path, MAX_FILE_PATH_LENGTH) + 1] = '\0';
									strncat(out_texture->path, image->name, MAX_FILE_PATH_LENGTH);
									strncat(out_texture->path, ".png", MAX_FILE_PATH_LENGTH);

									if (!file_exists(out_texture->path)) {
										LOG_DEBUG("Generating %s...", out_texture->path);
										logger_indent();
										uint8_t *pixels = stbi_load_from_memory(src, buffer_view->size, (int32_t *)&out_texture->width, (int32_t *)&out_texture->height, (int32_t *)&out_texture->channels, 4);
										stbi_write_png(out_texture->path, out_texture->width, out_texture->height, out_texture->channels, pixels, 0);
										LOG_DEBUG("File generated");
										logger_dedent();
									}
								}
								if (strcmp(&image->mime_type[length], "jpeg") == 0) {
									LOG_DEBUG("MimeType should be jpeg (it is '%s')", image->mime_type);
								}
							}
						}
					} else {
						LOG_DEBUG("URI '%s'[%s]", image->name, image->uri);
						strncpy(out_texture->path, relative_path, MAX_FILE_PATH_LENGTH);
						strncat(out_texture->path, image->uri, MAX_FILE_NAME_LENGTH);
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

bool file_exists(const char *path) {
	FILE *file = fopen(path, "r");
	if (file) {
		fclose(file);
		return true;
	}
	return false;
}

void file_name(const char *src, char *dst) {
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

void file_path(const char *src, char *dst) {
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
