#include "importer.h"
#include "assets/asset_types.h"

#include "assets/mesh_source.h"
#include "common.h"
#include "core/astring.h"

#include "core/debug.h"
#include "platform/filesystem.h"

#include <cgltf/cgltf.h>
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

#include "core/arena.h"
#include "core/logger.h"

#include <stdio.h>
#include <string.h>

#define MATERIAL_PROPERTY_COUNT 9

// static void calculate_tangents(Vertex *vertices, uint32_t vertex_count, uint32_t *indices, uint32_t index_count);
static ImageSource *find_loaded_texture(const cgltf_data *data, SModel *scene, const cgltf_texture *gltf_tex);

bool importer_load_shader(Arena *arena, String vertex_path, String fragment_path, ShaderSource *out_shader) {
	out_shader->vertex_shader = filesystem_read(arena, vertex_path);
	out_shader->fragment_shader = filesystem_read(arena, fragment_path);

	return out_shader->fragment_shader.content && out_shader->vertex_shader.content;
}

bool importer_load_image(Arena *arena, String path, ImageSource *out_texture) {
	ArenaTemp scratch = arena_scratch(arena);

	String filename = string_push_copy(scratch.arena, string_path_filename(path));
	LOG_INFO("Loading %s...", filename.memory);

	logger_indent();
	uint8_t *pixels = stbi_load(path.memory, &out_texture->width, &out_texture->height, &out_texture->channels, 4);
	if (pixels == NULL) {
		LOG_ERROR("Failed to load image [ %s ]", path.memory);
		static uint8_t magenta[] = { 255, 0, 255, 255 };
		out_texture->width = out_texture->height = 1;
		out_texture->channels = 4;
		out_texture->pixels = magenta;
		arena_scratch_release(scratch);
		return false;
	}
	out_texture->channels = 4;
	uint32_t pixel_buffer_size = out_texture->width * out_texture->height * out_texture->channels;
	out_texture->pixels = arena_push_array(arena, uint8_t, pixel_buffer_size);
	memcpy(out_texture->pixels, pixels, pixel_buffer_size);
	stbi_image_free(pixels);

	LOG_DEBUG("%s: { width = %d, height = %d, channels = %d }", filename.memory, out_texture->width, out_texture->height, out_texture->channels);
	LOG_INFO("%s loaded", filename.memory);
	logger_dedent();

	arena_scratch_release(scratch);
	return true;
}

static MaterialProperty default_properties[MATERIAL_PROPERTY_COUNT] = {
	{ .name = { .memory = "u_base_color_texture", .length = 20 }, .type = PROPERTY_TYPE_IMAGE, .as.image = NULL },
	{ .name = { .memory = "u_metallic_roughness_texture", .length = 28 }, .type = PROPERTY_TYPE_IMAGE, .as.image = NULL },
	{ .name = { .memory = "u_normal_texture", .length = 16 }, .type = PROPERTY_TYPE_IMAGE, .as.image = NULL },
	{ .name = { .memory = "u_occlusion_texture", .length = 19 }, .type = PROPERTY_TYPE_IMAGE, .as.image = NULL },
	{ .name = { .memory = "u_emissive_texture", .length = 18 }, .type = PROPERTY_TYPE_IMAGE, .as.image = NULL },

	{ .name = { .memory = "base_color_factor", .length = 17 }, .type = PROPERTY_TYPE_FLOAT4, .as.float4 = { 1.0f, 1.0f, 1.0f, 1.0f } },
	{ .name = { .memory = "metallic_factor", .length = 15 }, .type = PROPERTY_TYPE_FLOAT, .as.float1 = 0.0f },
	{ .name = { .memory = "roughness_factor", .length = 16 }, .type = PROPERTY_TYPE_FLOAT, .as.float1 = 0.5f },
	{ .name = { .memory = "emissive_factor", .length = 15 }, .type = PROPERTY_TYPE_FLOAT3, .as.float3 = { 1.0f, 1.0f, 1.0f } },
};

bool importer_load_gltf(Arena *arena, String path, SModel *out_model) {
	cgltf_options options = { 0 };
	cgltf_data *data = NULL;
	cgltf_result result = cgltf_parse_file(&options, path.memory, &data);

	if (result == cgltf_result_success)
		result = cgltf_load_buffers(&options, data, path.memory);

	if (result == cgltf_result_success)
		result = cgltf_validate(data);

	if (result != cgltf_result_success) {
		LOG_ERROR("Importer: Failed to load model");
		return NULL;
	}

	LOG_INFO("Loading %s...", path.memory);
	logger_indent();

	ArenaTemp scratch = arena_scratch(arena);
	String base_directory = string_push_copy(scratch.arena, string_path_folder(path));

	out_model->image_count = data->images_count;
	out_model->images = arena_push_array_zero(arena, ImageSource, out_model->image_count);

	for (uint32_t texture_index = 0; texture_index < out_model->image_count; ++texture_index) {
		cgltf_image *src = &data->images[texture_index];
		ImageSource *dst = &out_model->images[texture_index];

		if (src->buffer_view) {
			uint8_t *buffer_data = (uint8_t *)src->buffer_view->buffer->data + src->buffer_view->offset;
			String mime_type = string_from_cstr(src->mime_type);

			String filename = string_push_replace(scratch.arena, string_path_filename(path), slit(".glb"), slit(""));
			String name = string_pushf(scratch.arena, "%s_%s", filename.memory, (src->name ? src->name : "image"));
			if (string_find_first(string_from_cstr(src->mime_type), slit("png")) != -1) {
				dst->path = string_pushf(arena, "%s/%s.png", base_directory.memory, name.memory);
				if (filesystem_file_exists(dst->path) == false) {
					uint8_t *pixels = stbi_load_from_memory(buffer_data, src->buffer_view->size, &dst->width, &dst->height, &dst->channels, 4);
					stbi_write_png(dst->path.memory, dst->width, dst->height, 4, pixels, STBI_default);
					stbi_image_free(pixels);
				}
			} else if (string_find_first(mime_type, slit("jpg")) != -1 || string_find_first(mime_type, slit("jpeg")) != -1) {
				dst->path = string_pushf(arena, "%s/%s.jpg", base_directory.memory, name.memory);
				if (filesystem_file_exists(dst->path) == false) {
					uint8_t *pixels = stbi_load_from_memory(buffer_data, src->buffer_view->size, &dst->width, &dst->height, &dst->channels, 4);
					stbi_write_jpg(dst->path.memory, dst->width, dst->height, dst->channels, pixels, 0);
					stbi_image_free(pixels);
				}
			}
		} else if (src->uri) {
			dst->path = string_pushf(arena, "%s/%s", base_directory.memory, src->uri);
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
			memcpy(&dst->properties[5].as.float4, pbr->base_color_factor, sizeof(float4));

			dst->properties[6].as.float1 = pbr->metallic_factor;
			dst->properties[7].as.float1 = pbr->roughness_factor;

			dst->properties[0].as.image = find_loaded_texture(data, out_model, pbr->base_color_texture.texture);
			dst->properties[1].as.image = find_loaded_texture(data, out_model, pbr->metallic_roughness_texture.texture);
		}

		dst->properties[2].as.image = find_loaded_texture(data, out_model, src->normal_texture.texture);
		dst->properties[3].as.image = find_loaded_texture(data, out_model, src->occlusion_texture.texture);

		memcpy(&dst->properties[8].as.float3, src->emissive_factor, sizeof(float3));
		dst->properties[4].as.image = find_loaded_texture(data, out_model, src->emissive_texture.texture);
	}

	uint32_t mesh_count = 0;
	for (uint32_t index = 0; index < data->meshes_count; ++index) {
		cgltf_mesh *mesh = &data->meshes[index];

		mesh_count += mesh->primitives_count;
	}
	out_model->meshes = arena_push_array_zero(arena, MeshSource, mesh_count);
	out_model->mesh_count = mesh_count;
	out_model->mesh_to_material = arena_push_array_zero(arena, uint32_t, mesh_count);

	mesh_count = 0;
	for (uint32_t mesh_index = 0; mesh_index < data->meshes_count; ++mesh_index) {
		cgltf_mesh *mesh = &data->meshes[mesh_index];

		for (uint32_t primitive_index = 0; primitive_index < mesh->primitives_count; ++primitive_index) {
			cgltf_primitive *src_mesh = &mesh->primitives[primitive_index];

			uint32_t out_mesh_index = mesh_count++;
			MeshSource *dst_mesh = &out_model->meshes[out_mesh_index];

			// bool has_tangents = false;

			cgltf_accessor *iaccessor = src_mesh->indices;

			dst_mesh->index_count = iaccessor->count;
			dst_mesh->index_size = iaccessor->component_type == cgltf_component_type_r_32u ? 4 : 2;
			LOG_INFO("sizeof(indices) = %zu", dst_mesh->index_count * dst_mesh->index_size);
			ASSERT(
				(src_mesh->indices->component_type == cgltf_component_type_r_16u && dst_mesh->index_size == 2) ||
				(src_mesh->indices->component_type == cgltf_component_type_r_32u && dst_mesh->index_size == 4)

			);
			ASSERT(src_mesh->indices->buffer_view->size == (src_mesh->indices->count * src_mesh->indices->stride));

			dst_mesh->indices = arena_push(arena, src_mesh->indices->buffer_view->size, dst_mesh->index_size, true);
			cgltf_size unpacked = cgltf_accessor_unpack_indices(iaccessor, dst_mesh->indices, dst_mesh->index_size, dst_mesh->index_count);
			ASSERT(unpacked == dst_mesh->index_count);

			for (uint32_t attribute_index = 0; attribute_index < src_mesh->attributes_count; ++attribute_index) {
				cgltf_attribute *attribute = &src_mesh->attributes[attribute_index];
				cgltf_accessor *accessor = attribute->data;

				if (dst_mesh->vertices == NULL) {
					dst_mesh->vertex_count = accessor->count;
					dst_mesh->vertex_size = sizeof(Vertex);
					dst_mesh->vertices = arena_push_array_zero(arena, uint8_t, dst_mesh->vertex_count * dst_mesh->vertex_size);
				}

				ASSERT(dst_mesh->vertex_count == accessor->count);

				switch (attribute->type) {
					case cgltf_attribute_type_position: {
						for (uint32_t vertex_index = 0; vertex_index < dst_mesh->vertex_count; ++vertex_index) {
							Vertex *dst_vertex = &((Vertex *)dst_mesh->vertices)[vertex_index];

							cgltf_accessor_read_float(accessor, vertex_index, vector3f_elements(&dst_vertex->position), 3);
						}
						ASSERT(accessor->stride == 12);
					} break;
					case cgltf_attribute_type_texcoord: {
						ASSERT(accessor->component_type == cgltf_component_type_r_32f);
						for (uint32_t vertex_index = 0; vertex_index < dst_mesh->vertex_count; ++vertex_index) {
							Vertex *dst_vertex = &((Vertex *)dst_mesh->vertices)[vertex_index];

							cgltf_accessor_read_float(accessor, vertex_index, vector2f_elements(&dst_vertex->uv), 2);
						}
					} break;
					case cgltf_attribute_type_normal: {
						ASSERT(accessor->component_type == cgltf_component_type_r_32f);
						for (uint32_t vertex_index = 0; vertex_index < dst_mesh->vertex_count; ++vertex_index) {
							Vertex *dst_vertex = &((Vertex *)dst_mesh->vertices)[vertex_index];

							cgltf_accessor_read_float(accessor, vertex_index, vector3f_elements(&dst_vertex->normal), 3);
						}
					} break;
					case cgltf_attribute_type_tangent: {
						ASSERT(accessor->component_type == cgltf_component_type_r_32f);
						for (uint32_t vertex_index = 0; vertex_index < dst_mesh->vertex_count; ++vertex_index) {
							Vertex *dst_vertex = &((Vertex *)dst_mesh->vertices)[vertex_index];

							// has_tangents = true;

							cgltf_accessor_read_float(accessor, vertex_index, vector4f_elements(&dst_vertex->tangent), 4);
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

			if (src_mesh->material) {
				uint32_t index = src_mesh->material - data->materials;
				out_model->mesh_to_material[out_mesh_index] = index;
			}

			// if (has_tangents == false)
			// 	calculate_tangents(dst_mesh.vertices, dst_mesh.vertex_count, dst_mesh.indices, dst_mesh.index_count);
		}
	}

	logger_dedent();
	cgltf_free(data);

	arena_scratch_release(scratch);
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

ImageSource *find_loaded_texture(const cgltf_data *data, SModel *source, const cgltf_texture *gltf_tex) {
	if (!gltf_tex || !gltf_tex->image)
		return NULL;

	size_t index = gltf_tex->image - data->images;

	if (index < source->image_count) {
		return &source->images[index];
	}
	return NULL;
}

// void calculate_tangents(Vertex *vertices, uint32_t vertex_count, uint32_t *indices, uint32_t index_count) {
// 	for (uint32_t i = 0; i < vertex_count; i++) {
// 		memset(vertices[i].tangent, 0, sizeof(vector4));
// 	}
//
// 	for (uint32_t index = 0; index < index_count; index += 3) {
// 		uint32_t triangle[3] = { indices[index + 0], indices[index + 1], indices[index + 2] };
// 		Vertex *points[3] = { &vertices[triangle[0]], &vertices[triangle[1]], &vertices[triangle[2]] };
//
// 		vector3 edge1 = { 0 }, edge2 = { 0 };
// 		glm_vector3_sub(points[1]->position, points[0]->position, edge1);
// 		glm_vector3_sub(points[2]->position, points[0]->position, edge2);
//
// 		vector2 delta_1 = { 0 }, delta_2 = { 0 };
// 		glm_vector2_sub(points[1]->uv, points[0]->uv, delta_1);
// 		glm_vector2_sub(points[2]->uv, points[0]->uv, delta_2);
//
// 		float f = 1.0f / (delta_1[0] * delta_2[1] - delta_2[0] * delta_1[1]);
//
// 		vector3 tangent;
// 		tangent[0] = f * (delta_2[1] * edge1[0] - delta_1[1] * edge2[0]);
// 		tangent[1] = f * (delta_2[1] * edge1[1] - delta_1[1] * edge2[1]);
// 		tangent[2] = f * (delta_2[1] * edge1[2] - delta_1[1] * edge2[2]);
//
// 		glm_vector3_add(points[0]->tangent, tangent, points[0]->tangent);
// 		glm_vector3_add(points[1]->tangent, tangent, points[1]->tangent);
// 		glm_vector3_add(points[2]->tangent, tangent, points[2]->tangent);
//
// 		points[0]->tangent[3] = 1.0f;
// 		points[1]->tangent[3] = 1.0f;
// 		points[2]->tangent[3] = 1.0f;
// 	}
// }

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
