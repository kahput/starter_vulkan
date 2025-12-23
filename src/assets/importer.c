#include "importer.h"
#include "assets/asset_types.h"

#include "common.h"
#include "core/debug.h"
#include "core/astring.h"

#include "platform/filesystem.h"

#include <cgltf/cgltf.h>
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

#include "allocators/arena.h"
#include "core/logger.h"

#include <stdio.h>
#include <string.h>

#define MATERIAL_PROPERTY_COUNT 9

void filesystem_filename(const char *src, char *dst);
void filesystem_directory(const char *src, char *dst);

static void calculate_tangents(Vertex *vertices, uint32_t vertex_count, uint32_t *indices, uint32_t index_count);
static ImageSource *find_loaded_texture(const cgltf_data *data, ModelSource *scene, const cgltf_texture *gltf_tex);

bool importer_load_shader(Arena *arena, String vertex_path, String fragment_path, ShaderSource *out_shader) {
	out_shader->vertex_shader = filesystem_read(arena, vertex_path);
	out_shader->fragment_shader = filesystem_read(arena, fragment_path);

	return out_shader->fragment_shader.content && out_shader->vertex_shader.content;
}

bool importer_load_image(Arena *arena, String path, ImageSource *out_texture) {
	ArenaTemp scratch = arena_scratch(arena);

	String filename = string_filename_from_path(scratch.arena, path);
	LOG_INFO("Loading %s...", filename.data);

	logger_indent();
	uint8_t *pixels = stbi_load(path.data, &out_texture->width, &out_texture->height, &out_texture->channels, 4);
	if (pixels == NULL) {
		LOG_ERROR("Failed to load image [ %s ]", path.data);
		static uint8_t magenta[] = { 255, 0, 255, 255 };
		out_texture->width = out_texture->height = 1;
		out_texture->channels = 4;
		out_texture->pixels = magenta;
		arena_release_scratch(scratch);
		return false;
	}
	out_texture->channels = 4;
	uint32_t pixel_buffer_size = out_texture->width * out_texture->height * out_texture->channels;
	out_texture->pixels = arena_push_array(arena, uint8_t, pixel_buffer_size);
	memcpy(out_texture->pixels, pixels, pixel_buffer_size);
	stbi_image_free(pixels);

	LOG_DEBUG("%s: { width = %d, height = %d, channels = %d }", filename.data, out_texture->width, out_texture->height, out_texture->channels);
	LOG_INFO("%s loaded", filename.data);
	logger_dedent();

	arena_release_scratch(scratch);
	return true;
}

static MaterialProperty default_properties[MATERIAL_PROPERTY_COUNT] = {
	{ .name = { .data = "u_base_color_texture", .length = 20, .size = 21 }, .type = PROPERTY_TYPE_IMAGE, .as.image = NULL },
	{ .name = { .data = "u_metallic_roughness_texture", .length = 28, .size = 29 }, .type = PROPERTY_TYPE_IMAGE, .as.image = NULL },
	{ .name = { .data = "u_normal_texture", .length = 16, .size = 17 }, .type = PROPERTY_TYPE_IMAGE, .as.image = NULL },
	{ .name = { .data = "u_occlusion_texture", .length = 19, .size = 20 }, .type = PROPERTY_TYPE_IMAGE, .as.image = NULL },
	{ .name = { .data = "u_emissive_texture", .length = 18, .size = 19 }, .type = PROPERTY_TYPE_IMAGE, .as.image = NULL },

	{ .name = { .data = "base_color_factor", .length = 17, .size = 18 }, .type = PROPERTY_TYPE_FLOAT4, .as.vec4f = { 1.0f, 1.0f, 1.0f, 1.0f } },
	{ .name = { .data = "metallic_factor", .length = 15, .size = 16 }, .type = PROPERTY_TYPE_FLOAT, .as.f = 0.0f },
	{ .name = { .data = "roughness_factor", .length = 16, .size = 17 }, .type = PROPERTY_TYPE_FLOAT, .as.f = 0.5f },
	{ .name = { .data = "emissive_factor", .length = 15, .size = 16 }, .type = PROPERTY_TYPE_FLOAT3, .as.vec3f = { 1.0f, 1.0f, 1.0f } },
};

bool importer_load_gltf(Arena *arena, String path, ModelSource *out_model) {
	cgltf_options options = { 0 };
	cgltf_data *data = NULL;
	cgltf_result result = cgltf_parse_file(&options, path.data, &data);

	if (result == cgltf_result_success)
		result = cgltf_load_buffers(&options, data, path.data);

	if (result == cgltf_result_success)
		result = cgltf_validate(data);

	if (result != cgltf_result_success) {
		LOG_ERROR("Importer: Failed to load model");
		return NULL;
	}

	LOG_INFO("Loading %s...", path.data);
	logger_indent();

	ArenaTemp scratch = arena_scratch(arena);
	String base_directory = string_directory_from_path(scratch.arena, path);

	out_model->image_count = data->images_count;
	out_model->images = arena_push_array_zero(arena, ImageSource, out_model->image_count);

	for (uint32_t texture_index = 0; texture_index < out_model->image_count; ++texture_index) {
		cgltf_image *src = &data->images[texture_index];
		ImageSource *dst = &out_model->images[texture_index];

		if (src->buffer_view) {
			uint8_t *buffer_data = (uint8_t *)src->buffer_view->buffer->data + src->buffer_view->offset;
			uint8_t *pixels = stbi_load_from_memory(buffer_data, src->buffer_view->size, &dst->width, &dst->height, &dst->channels, 4);
			String mime_type = string_wrap_cstring(src->mime_type);

			String name = string_format(scratch.arena, S("%s_%s"), data->scene->name, src->name);
			if (string_contains(string_wrap_cstring(src->mime_type), S("png")) != -1) {
				dst->path = string_format(arena, S("%s/%s.png"), base_directory, name);
				stbi_write_png(dst->path.data, dst->width, dst->height, dst->channels, pixels, 0);
			} else if (string_contains(mime_type, S("jpg")) != -1 || string_contains(mime_type, S("jpeg")) != -1) {
				dst->path = string_format(arena, S("%s/%s.jpg"), base_directory, name);
				stbi_write_jpg(dst->path.data, dst->width, dst->height, dst->channels, pixels, 0);
			}
			stbi_image_free(pixels);
		} else if (src->uri) {
			dst->path = string_format(arena, S("%s/%s"), base_directory.data, src->uri);
		}
	}

	out_model->material_count = data->materials_count;
	out_model->materials = arena_push_array_zero(arena, MaterialSource, out_model->material_count);
	LOG_INFO("Number of materials: %d", out_model->material_count);

	for (uint32_t index = 0; index < out_model->material_count; ++index) {
		cgltf_material *src = &data->materials[index];
		MaterialSource *dst = &out_model->materials[index];

		dst->property_count = MATERIAL_PROPERTY_COUNT;
		dst->properties = arena_push_array_zero(arena, MaterialProperty, dst->property_count);

		memcpy(dst->properties, default_properties, sizeof(default_properties));

		if (src->has_pbr_metallic_roughness) {
			cgltf_pbr_metallic_roughness *pbr = &src->pbr_metallic_roughness;

			mempcpy(dst->properties[5].as.vec4f, pbr->base_color_factor, sizeof(vec4));

			dst->properties[6].as.f = pbr->metallic_factor;
			dst->properties[7].as.f = pbr->roughness_factor;

			dst->properties[0].as.image = find_loaded_texture(data, out_model, pbr->base_color_texture.texture);
			dst->properties[1].as.image = find_loaded_texture(data, out_model, pbr->metallic_roughness_texture.texture);
		}

		dst->properties[2].as.image = find_loaded_texture(data, out_model, src->normal_texture.texture);
		dst->properties[3].as.image = find_loaded_texture(data, out_model, src->occlusion_texture.texture);

		memcpy(dst->properties[8].as.vec3f, src->emissive_factor, sizeof(vec3));
		dst->properties[4].as.image = find_loaded_texture(data, out_model, src->emissive_texture.texture);
	}

	uint32_t mesh_count = 0;
	for (uint32_t index = 0; index < data->meshes_count; ++index) {
		cgltf_mesh *mesh = &data->meshes[index];

		mesh_count += mesh->primitives_count;
	}

	out_model->mesh_count = mesh_count;
	out_model->meshes = arena_push_array_zero(arena, MeshSource, out_model->mesh_count);

	mesh_count = 0;
	for (uint32_t mesh_index = 0; mesh_index < data->meshes_count; ++mesh_index) {
		cgltf_mesh *mesh = &data->meshes[mesh_index];

		for (uint32_t primitive_index = 0; primitive_index < mesh->primitives_count; ++primitive_index) {
			cgltf_primitive *src_mesh = &mesh->primitives[primitive_index];
			MeshSource *dst_mesh = &out_model->meshes[mesh_count++];

			bool has_tangents = false;

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
					case cgltf_attribute_type_tangent: {
						for (uint32_t vertex_index = 0; vertex_index < dst_mesh->vertex_count; ++vertex_index) {
							Vertex *dst_vertex = &dst_mesh->vertices[vertex_index];
							float *src_vertex = (float *)(vertex_buffer_data + vertex_index * attribute_stride);

							has_tangents = true;

							dst_vertex->tangent[0] = src_vertex[0];
							dst_vertex->tangent[1] = src_vertex[1];
							dst_vertex->tangent[2] = src_vertex[2];
							dst_vertex->tangent[3] = src_vertex[3];
						}
					} break;
					case cgltf_attribute_type_invalid:
					case cgltf_attribute_type_color:
					case cgltf_attribute_type_joints:
					case cgltf_attribute_type_weights:
					case cgltf_attribute_type_custom:
					case cgltf_attribute_type_max_enum:
						break;
				}
			}

			if (has_tangents == false)
				calculate_tangents(dst_mesh->vertices, dst_mesh->vertex_count, dst_mesh->indices, dst_mesh->index_count);

			if (src_mesh->material) {
				uint32_t index = src_mesh->material - data->materials;
				dst_mesh->material = &out_model->materials[index];
			}
		}
	}

	logger_dedent();
	cgltf_free(data);

	arena_release_scratch(scratch);
	return true;
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

ImageSource *find_loaded_texture(const cgltf_data *data, ModelSource *source, const cgltf_texture *gltf_tex) {
	if (!gltf_tex || !gltf_tex->image)
		return NULL;

	size_t index = gltf_tex->image - data->images;

	if (index < source->image_count) {
		return &source->images[index];
	}
	return NULL;
}

void calculate_tangents(Vertex *vertices, uint32_t vertex_count, uint32_t *indices, uint32_t index_count) {
	for (uint32_t i = 0; i < vertex_count; i++) {
		memset(vertices[i].tangent, 0, sizeof(vec4));
	}

	for (uint32_t index = 0; index < index_count; index += 3) {
		uint32_t triangle[3] = { indices[index + 0], indices[index + 1], indices[index + 2] };
		Vertex *points[3] = { &vertices[triangle[0]], &vertices[triangle[1]], &vertices[triangle[2]] };

		vec3 edge1 = { 0 }, edge2 = { 0 };
		glm_vec3_sub(points[1]->position, points[0]->position, edge1);
		glm_vec3_sub(points[2]->position, points[0]->position, edge2);

		vec2 delta_1 = { 0 }, delta_2 = { 0 };
		glm_vec2_sub(points[1]->uv, points[0]->uv, delta_1);
		glm_vec2_sub(points[2]->uv, points[0]->uv, delta_2);

		float f = 1.0f / (delta_1[0] * delta_2[1] - delta_2[0] * delta_1[1]);

		vec3 tangent;
		tangent[0] = f * (delta_2[1] * edge1[0] - delta_1[1] * edge2[0]);
		tangent[1] = f * (delta_2[1] * edge1[1] - delta_1[1] * edge2[1]);
		tangent[2] = f * (delta_2[1] * edge1[2] - delta_1[1] * edge2[2]);

		glm_vec3_add(points[0]->tangent, tangent, points[0]->tangent);
		glm_vec3_add(points[1]->tangent, tangent, points[1]->tangent);
		glm_vec3_add(points[2]->tangent, tangent, points[2]->tangent);

		points[0]->tangent[3] = 1.0f;
		points[1]->tangent[3] = 1.0f;
		points[2]->tangent[3] = 1.0f;
	}
}

// TODO: Move these to the material system for reflection

// static inline ShaderAttributeFormat to_shader_attribute_format(SpvReflectFormat format) {
// 	switch (format) {
// 		case SPV_REFLECT_FORMAT_R16_SINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_INT16, 1 };
// 		case SPV_REFLECT_FORMAT_R16G16_SINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_INT16, 2 };
// 		case SPV_REFLECT_FORMAT_R16G16B16_SINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_INT16, 3 };
// 		case SPV_REFLECT_FORMAT_R16G16B16A16_SINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_INT16, 4 };
//
// 		case SPV_REFLECT_FORMAT_R16_SFLOAT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_FLOAT16, 1 };
// 		case SPV_REFLECT_FORMAT_R16G16_SFLOAT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_FLOAT16, 2 };
// 		case SPV_REFLECT_FORMAT_R16G16B16_SFLOAT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_FLOAT16, 3 };
// 		case SPV_REFLECT_FORMAT_R16G16B16A16_SFLOAT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_FLOAT16, 4 };
//
// 		case SPV_REFLECT_FORMAT_R16_UINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_UINT16, 1 };
// 		case SPV_REFLECT_FORMAT_R16G16_UINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_UINT16, 2 };
// 		case SPV_REFLECT_FORMAT_R16G16B16_UINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_UINT16, 3 };
// 		case SPV_REFLECT_FORMAT_R16G16B16A16_UINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_UINT16, 4 };
//
// 		case SPV_REFLECT_FORMAT_R32_UINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_UINT32, 1 };
// 		case SPV_REFLECT_FORMAT_R32G32_UINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_UINT32, 2 };
// 		case SPV_REFLECT_FORMAT_R32G32B32_UINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_UINT32, 3 };
// 		case SPV_REFLECT_FORMAT_R32G32B32A32_UINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_UINT32, 4 };
//
// 		case SPV_REFLECT_FORMAT_R32_SINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_INT32, 1 };
// 		case SPV_REFLECT_FORMAT_R32G32_SINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_INT32, 2 };
// 		case SPV_REFLECT_FORMAT_R32G32B32_SINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_INT32, 3 };
// 		case SPV_REFLECT_FORMAT_R32G32B32A32_SINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_INT32, 4 };
//
// 		case SPV_REFLECT_FORMAT_R32_SFLOAT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_FLOAT32, 1 };
// 		case SPV_REFLECT_FORMAT_R32G32_SFLOAT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_FLOAT32, 2 };
// 		case SPV_REFLECT_FORMAT_R32G32B32_SFLOAT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_FLOAT32, 3 };
// 		case SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_FLOAT32, 4 };
//
// 		case SPV_REFLECT_FORMAT_R64_UINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_UINT64, 1 };
// 		case SPV_REFLECT_FORMAT_R64G64_UINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_UINT64, 2 };
// 		case SPV_REFLECT_FORMAT_R64G64B64_UINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_UINT64, 3 };
// 		case SPV_REFLECT_FORMAT_R64G64B64A64_UINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_UINT64, 4 };
//
// 		case SPV_REFLECT_FORMAT_R64_SINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_INT64, 1 };
// 		case SPV_REFLECT_FORMAT_R64G64_SINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_INT64, 2 };
// 		case SPV_REFLECT_FORMAT_R64G64B64_SINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_INT64, 3 };
// 		case SPV_REFLECT_FORMAT_R64G64B64A64_SINT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_INT64, 4 };
//
// 		case SPV_REFLECT_FORMAT_R64_SFLOAT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_FLOAT64, 1 };
// 		case SPV_REFLECT_FORMAT_R64G64_SFLOAT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_FLOAT64, 2 };
// 		case SPV_REFLECT_FORMAT_R64G64B64_SFLOAT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_FLOAT64, 3 };
// 		case SPV_REFLECT_FORMAT_R64G64B64A64_SFLOAT:
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_FLOAT64, 4 };
//
// 		default: {
// 			ASSERT_MESSAGE(0, "Undefined spirv reflection format");
// 			return (ShaderAttributeFormat){ SHADER_ATTRIBUTE_TYPE_UNDEFINED, 0 };
// 		}
// 	}
// }
