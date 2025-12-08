#include "importer.h"

#include "common.h"
#include "core/identifiers.h"
#include "renderer/renderer_types.h"

#include <cgltf/cgltf.h>
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

#include "allocators/arena.h"
#include "core/logger.h"

#include <stdio.h>
#include <string.h>

bool filesystem_file_exists(const char *path);
void filesystem_filename(const char *src, char *dst);
void filesystem_directory(const char *src, char *dst);

static TextureSource *find_loaded_texture(const cgltf_data *data, SceneAsset *scene, const cgltf_texture *gltf_tex);

SceneAsset *importer_load_gltf(Arena *arena, const char *path) {
	cgltf_options options = { 0 };
	cgltf_data *data = NULL;
	cgltf_result result = cgltf_parse_file(&options, path, &data);

	if (result == cgltf_result_success)
		result = cgltf_load_buffers(&options, data, path);

	if (result == cgltf_result_success)
		result = cgltf_validate(data);

	if (result != cgltf_result_success) {
		LOG_ERROR("Importer: Failed to load model");
		return NULL;
	}

	LOG_INFO("Loading %s...", path);
	logger_indent();
	SceneAsset *asset = arena_push_struct(arena, SceneAsset);
	char base_directory[MAX_FILE_PATH_LENGTH];
	filesystem_directory(path, base_directory);

	asset->texture_count = data->images_count;
	asset->textures = arena_push_array_zero(arena, TextureSource, asset->texture_count);

	for (uint32_t texture_index = 0; texture_index < asset->texture_count; ++texture_index) {
		cgltf_image *src = &data->images[texture_index];
		TextureSource *dst = &asset->textures[texture_index];

		if (src->buffer_view) {
			uint8_t *data = (uint8_t *)src->buffer_view->buffer->data + src->buffer_view->offset;
			dst->pixels = stbi_load_from_memory(data, src->buffer_view->size, &dst->width, &dst->height, &dst->channels, 4);
		} else if (src->uri) {
			size_t full_path_length = strnlen(base_directory, MAX_FILE_PATH_LENGTH) + strlen(src->uri) + 1;
			snprintf(dst->path, full_path_length, "%s%s", base_directory, src->uri);

			dst->pixels = stbi_load(dst->path, &dst->width, &dst->height, &dst->channels, 4);
		}

		if (!dst->pixels) {
			LOG_WARN("Importer: Failed to load texture for image index %d", texture_index);
		} else {
			dst->channels = 4; // We forced 4 channels in stbi_load
		}
	}

	asset->material_count = data->materials_count;
	asset->materials = arena_push_array_zero(arena, MaterialSource, asset->material_count);
	LOG_INFO("Number of materials: %d", asset->material_count);

	for (uint32_t index = 0; index < asset->material_count; ++index) {
		cgltf_material *src = &data->materials[index];
		MaterialSource *dst = &asset->materials[index];

		// PBR Metallic Roughness
		if (src->has_pbr_metallic_roughness) {
			cgltf_pbr_metallic_roughness *pbr = &src->pbr_metallic_roughness;

			// Factors
			mempcpy(dst->base_color_factor, pbr->base_color_factor, sizeof(vec4));
			dst->metallic_factor = pbr->metallic_factor;
			dst->roughness_factor = pbr->roughness_factor;

			// Textures
			dst->base_color_texture = find_loaded_texture(data, asset, pbr->base_color_texture.texture);
			dst->metallic_roughness_texture = find_loaded_texture(data, asset, pbr->metallic_roughness_texture.texture);
		}

		// Normal Map
		dst->normal_texture = find_loaded_texture(data, asset, src->normal_texture.texture);

		// Occlusion
		dst->occlusion_texture = find_loaded_texture(data, asset, src->occlusion_texture.texture);

		// Emissive
		memcpy(dst->emissive_factor, src->emissive_factor, sizeof(vec3));
		dst->emissive_texture = find_loaded_texture(data, asset, src->emissive_texture.texture);
	}

	uint32_t mesh_count = 0;
	for (uint32_t index = 0; index < data->meshes_count; ++index) {
		cgltf_mesh *mesh = &data->meshes[index];

		mesh_count += mesh->primitives_count;
	}

	asset->mesh_count = mesh_count;
	asset->meshes = arena_push_array_zero(arena, MeshSource, asset->mesh_count);

	mesh_count = 0;
	for (uint32_t mesh_index = 0; mesh_index < data->meshes_count; ++mesh_index) {
		cgltf_mesh *mesh = &data->meshes[mesh_index];

		for (uint32_t primitive_index = 0; primitive_index < mesh->primitives_count; ++primitive_index) {
			cgltf_primitive *src_mesh = &mesh->primitives[primitive_index];
			MeshSource *dst_mesh = &asset->meshes[mesh_count++];

			dst_mesh->index_count = src_mesh->indices->count;
			dst_mesh->indices = arena_push_array_zero(arena, uint32_t, dst_mesh->index_count);

			uint8_t *index_buffer_data = (uint8_t *)src_mesh->indices->buffer_view->buffer->data + src_mesh->indices->offset + src_mesh->indices->buffer_view->offset;
			uint32_t index_stride = src_mesh->indices->buffer_view->stride ? src_mesh->indices->buffer_view->stride : src_mesh->indices->stride;

			for (uint32_t index = 0; index < src_mesh->indices->count; ++index) {
				if (index_stride == 2)
					dst_mesh->indices[index] = ((uint16_t *)index_buffer_data)[index];
				else if (index_stride == 4)
					dst_mesh->indices[index] = ((uint32_t *)index_buffer_data)[index];
			}

			for (uint32_t attribute_index = 0; attribute_index < src_mesh->attributes_count; ++attribute_index) {
				cgltf_attribute *attribute = &src_mesh->attributes[attribute_index];

				cgltf_accessor *accessor = attribute->data;
				cgltf_buffer_view *view = accessor->buffer_view;
				cgltf_buffer *buffer = view->buffer;

				uint8_t *vertex_buffer_data = (uint8_t *)buffer->data + accessor->offset + view->offset;
				uint32_t attribute_stride = view->stride ? view->stride : accessor->stride;

				if (dst_mesh->vertices == NULL) {
					dst_mesh->vertex_count = accessor->count;
					dst_mesh->vertices = arena_push_array_zero(arena, Vertex, dst_mesh->vertex_count);
				}

				switch (attribute->type) {
					case cgltf_attribute_type_position: {
						for (uint32_t vertex_index = 0; vertex_index < dst_mesh->vertex_count; ++vertex_index) {
							float *src_vertex = (float *)(vertex_buffer_data + vertex_index * attribute_stride);
							Vertex *dst_vertex = &dst_mesh->vertices[vertex_index];

							dst_vertex->position[0] = src_vertex[0];
							dst_vertex->position[1] = src_vertex[1];
							dst_vertex->position[2] = src_vertex[2];
						}
					} break;
					case cgltf_attribute_type_normal: {
						for (uint32_t vertex_index = 0; vertex_index < dst_mesh->vertex_count; ++vertex_index) {
							float *src_vertex = (float *)(vertex_buffer_data + vertex_index * attribute_stride);
							Vertex *dst_vertex = &dst_mesh->vertices[vertex_index];

							dst_vertex->normal[0] = src_vertex[0];
							dst_vertex->normal[1] = src_vertex[1];
							dst_vertex->normal[2] = src_vertex[2];
						}
					} break;
					case cgltf_attribute_type_texcoord: {
						for (uint32_t vertex_index = 0; vertex_index < dst_mesh->vertex_count; ++vertex_index) {
							Vertex *dst_vertex = &dst_mesh->vertices[vertex_index];
							float *src_vertex = (float *)(vertex_buffer_data + vertex_index * attribute_stride);

							dst_vertex->uv[0] = src_vertex[0];
							dst_vertex->uv[1] = src_vertex[1];
						}
					} break;
					case cgltf_attribute_type_tangent:
					case cgltf_attribute_type_invalid:
					case cgltf_attribute_type_color:
					case cgltf_attribute_type_joints:
					case cgltf_attribute_type_weights:
					case cgltf_attribute_type_custom:
					case cgltf_attribute_type_max_enum:
						break;
				}
			}

			if (src_mesh->material) {
				uint32_t index = src_mesh->material - data->materials;
				dst_mesh->material = &asset->materials[index];

			} else
				dst_mesh->material = NULL;
		}
	}

	logger_dedent();
	cgltf_free(data);

	return asset;
}

// Image importer_load_image(const char *path) {
// 	Image image;
// 	char filename[256];
// 	filesystem_filename(path, filename);
// 	LOG_INFO("Loading %s...", filename);
// 	logger_indent();
// 	image.pixels = stbi_load(path, &image.width, &image.height, &image.channels, STBI_default);
//
// 	if (image.pixels == NULL) {
// 		LOG_ERROR("Failed to load image [ %s ]", path);
// 		return (Image){ 0 };
// 	}
//
// 	LOG_DEBUG("%s: { width = %d, height = %d, channels = %d }", filename, image.width, image.height, image.channels);
// 	LOG_INFO("%s loaded", filename);
// 	logger_dedent();
//
// 	return image;
// }
//
// void importer_unload_image(Image image) {
// 	if (image.pixels)
// 		stbi_image_free(image.pixels);
// }

bool filesystem_file_exists(const char *path) {
	FILE *file = fopen(path, "r");
	if (file) {
		fclose(file);
		return true;
	}
	return false;
}

void filesystem_filename(const char *src, char *dst) {
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

void filesystem_directory(const char *src, char *dst) {
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

TextureSource *find_loaded_texture(const cgltf_data *data, SceneAsset *scene, const cgltf_texture *gltf_tex) {
	if (!gltf_tex || !gltf_tex->image)
		return NULL;

	// Calculate index based on pointer arithmetic
	// cgltf stores images in a contiguous array, so we can find the index
	size_t index = gltf_tex->image - data->images;

	if (index < scene->texture_count) {
		return &scene->textures[index];
	}
	return NULL;
}
