#include "importer.h"
#include "assets/asset_types.h"

#include "assets/mesh_source.h"
#include "common.h"
#include "core/strings.h"

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
/* static ImageSource *find_loaded_texture(const cgltf_data *data, SModel *scene, const cgltf_texture *gltf_tex); */

ShaderSource importer_load_shader(Arena *arena, String vertex_path, String fragment_path) {
	Span vfile = filesystem_read(arena, vertex_path);
	Span ffile = filesystem_read(arena, fragment_path);

	ShaderSource result = { vertex_path, fragment_path, vfile, ffile };
	return result;
}

ImageSource importer_load_image(Arena *arena, String path) {
	ArenaTemp scratch = arena_scratch_begin(arena);
	ImageSource result = { 0 };

	String filename = string_copy(scratch.arena, stringpath_filename(path));
	LOG_INFO("Loading %s...", filename.chars);

	logger_indent();
	uint8_t *pixels = stbi_load(path.chars, &result.width, &result.height, &result.channels, 4);
	if (pixels == NULL) {
		LOG_ERROR("Failed to load image [ %s ]", path.chars);
		static uint8_t magenta[] = { 255, 0, 255, 255 };
		result.width = result.height = 1;
		result.channels = 4;
		result.pixels = magenta;
		arena_scratch_end(scratch);
		return (ImageSource){ 0 };
	}
	result.channels = 4;
	uint32_t pixel_buffer_size = result.width * result.height * result.channels;
	result.pixels = arena_push_count(arena, pixel_buffer_size, uint8_t);
	memory_copy(result.pixels, pixels, pixel_buffer_size);
	stbi_image_free(pixels);

	LOG_DEBUG("%s: { width = %d, height = %d, channels = %d }", filename.chars, result.width, result.height, result.channels);
	LOG_INFO("%s loaded", filename.chars);
	logger_dedent();

	arena_scratch_end(scratch);
	return result;
}

static MaterialProperty default_properties[MATERIAL_PROPERTY_COUNT] = {
	{ .name = { .chars = "u_base_color_texture", .length = 20 }, .type = PROPERTY_TYPE_IMAGE, .as.uint32x1 = 0 },
	{ .name = { .chars = "u_metallic_roughness_texture", .length = 28 }, .type = PROPERTY_TYPE_IMAGE, .as.uint32x1 = 0 },
	{ .name = { .chars = "u_normal_texture", .length = 16 }, .type = PROPERTY_TYPE_IMAGE, .as.uint32x1 = 0 },
	{ .name = { .chars = "u_occlusion_texture", .length = 19 }, .type = PROPERTY_TYPE_IMAGE, .as.uint32x1 = 0 },
	{ .name = { .chars = "u_emissive_texture", .length = 18 }, .type = PROPERTY_TYPE_IMAGE, .as.uint32x1 = 0 },

	{ .name = { .chars = "base_color_factor", .length = 17 }, .type = PROPERTY_TYPE_FLOAT4, .as.float32x4 = { 1.0f, 1.0f, 1.0f, 1.0f } },
	{ .name = { .chars = "metallic_factor", .length = 15 }, .type = PROPERTY_TYPE_FLOAT1, .as.float32x1 = 0.0f },
	{ .name = { .chars = "roughness_factor", .length = 16 }, .type = PROPERTY_TYPE_FLOAT1, .as.float32x1 = 0.5f },
	{ .name = { .chars = "emissive_factor", .length = 15 }, .type = PROPERTY_TYPE_FLOAT3, .as.float32x3 = { 1.0f, 1.0f, 1.0f } },
};

SceneSource importer_load_gltf_scene(Arena *arena, String path) {
	SceneSource result = { 0 };

	cgltf_options options = { 0 };
	cgltf_data *data = NULL;
	cgltf_result cgltf_result = cgltf_parse_file(&options, path.chars, &data);

	if (cgltf_result == cgltf_result_success)
		cgltf_result = cgltf_load_buffers(&options, data, path.chars);

	if (cgltf_result == cgltf_result_success)
		cgltf_result = cgltf_validate(data);

	LOG_INFO("Loading %.*s", SARG(path));

	ArenaTemp scratch = arena_scratch_begin(arena);
	String directory = string_copy(scratch.arena, stringpath_directory(path));

	if (cgltf_result == cgltf_result_success) {
		result.image_count = data->images_count;
		result.images = arena_push_count(arena, result.image_count, ImageSource);
		for (uint32_t image_index = 0; image_index < data->images_count; ++image_index) {
			cgltf_image *src = &data->images[image_index];
			ImageSource *dst = &result.images[image_index];

			if (src->uri) {
				String image_path = stringpath_join(scratch.arena, directory, string_wrap(src->uri));
				*dst = importer_load_image(arena, image_path);
			} else if (src->buffer_view) {
				const uint8_t *buffer_data = cgltf_buffer_view_data(src->buffer_view);
				String mime_type = string_wrap(src->mime_type);

				String filename = string_replace(scratch.arena, stringpath_filename(path), S(".glb"), S(""));
				String name = string_format(scratch.arena, "%s_%s", filename.chars, (src->name ? src->name : "image"));

				const char *extension = NULL;
				if (string_find_first(string_wrap(src->mime_type), S("png")) != -1)
					extension = "png";
				else if (string_find_first(mime_type, S("jpg")) != -1 || string_find_first(mime_type, S("jpeg")) != -1)
					extension = "jpeg";
				else
					ASSERT_MESSAGE(false, "Expected png or jpeg");

				String output_path = string_format(arena, "%s/%s.%s", directory.chars, name.chars, extension);
				if (file_exists(output_path) == false) {
					dst->pixels = stbi_load_from_memory(buffer_data, src->buffer_view->size, &dst->width, &dst->height, &dst->channels, 4);
					stbi_write_png(output_path.chars, dst->width, dst->height, 4, dst->pixels, STBI_default);
					stbi_image_free(dst->pixels);
				} else
					*dst = importer_load_image(scratch.arena, output_path);
			}
		}

		result.material_count = data->materials_count;
		result.materials = arena_push_count(arena, result.material_count, MaterialSource);
		for (uint32_t material_index = 0; material_index < data->materials_count; ++material_index) {
			cgltf_material *src = &data->materials[material_index];
			MaterialSource *dst = &result.materials[material_index];

			dst->property_count = countof(default_properties);
			dst->properties = arena_push_count(arena, dst->property_count, MaterialProperty);

			memory_copy(dst->properties, default_properties, sizeof(default_properties));

			if (src->has_pbr_metallic_roughness) {
				cgltf_pbr_metallic_roughness *pbr = &src->pbr_metallic_roughness;
				memory_copy(&dst->properties[5].as.float32x4, pbr->base_color_factor, sizeof(float4));

				dst->properties[6].as.float32x1 = pbr->metallic_factor;
				dst->properties[7].as.float32x1 = pbr->roughness_factor;

				if (pbr->base_color_texture.texture)
					dst->properties[0].as.uint32x1 = cgltf_image_index(data, pbr->base_color_texture.texture->image) + 1;
				if (pbr->metallic_roughness_texture.texture)
					dst->properties[1].as.uint32x1 = cgltf_image_index(data, pbr->metallic_roughness_texture.texture->image) + 1;
			}

			if (src->normal_texture.texture)
				dst->properties[2].as.uint32x1 = cgltf_image_index(data, src->normal_texture.texture->image) + 1;
			if (src->occlusion_texture.texture)
				dst->properties[3].as.uint32x1 = cgltf_image_index(data, src->occlusion_texture.texture->image) + 1;

			memory_copy(&dst->properties[8].as.float32x3, src->emissive_factor, sizeof(float3));
			if (src->emissive_texture.texture)
				dst->properties[4].as.uint32x1 = cgltf_image_index(data, src->emissive_texture.texture->image) + 1;
		}

		// Meshes
		uint32_t *mesh_offsets = arena_push_count(scratch.arena, data->meshes_count, uint32_t);
		uint32_t mesh_offset = 0;

		for (uint32_t mesh_index = 0; mesh_index < data->meshes_count; ++mesh_index) {
			cgltf_mesh *mesh = &data->meshes[mesh_index];

			mesh_offsets[mesh_index] = mesh_offset;
			mesh_offset += mesh->primitives_count;
			LOG_INFO("Primitive count = %d", mesh->primitives_count);

			for (uint32_t primitive_index = 0; primitive_index < mesh->primitives_count; ++primitive_index) {
				cgltf_primitive *primitive = &mesh->primitives[primitive_index];
				result.mesh_count++;

				result.vertices_size += primitive->attributes->data->count * sizeof(Vertex);
				result.indices_size += primitive->indices->count * sizeof(uint32_t);
			}
		}

		result.meshes = arena_push_count(arena, result.mesh_count, MeshSource);
		result.mesh_to_material = arena_push_count(arena, result.mesh_count, uint32_t);
		result.bounding_boxes = arena_push_count(arena, result.mesh_count, Interval3);

		result.vertices = arena_push(arena, result.vertices_size, 64, true);
		result.indices = arena_push(arena, result.indices_size, 64, true);

		size_t vertices_offset = 0;
		size_t indices_offset = 0;
		for (uint32_t mesh_index = 0; mesh_index < data->meshes_count; ++mesh_index) {
			cgltf_mesh *mesh = &data->meshes[mesh_index];

			for (uint32_t primitive_index = 0; primitive_index < mesh->primitives_count; ++primitive_index) {
				cgltf_primitive *primitive = &mesh->primitives[primitive_index];
				uint32_t primitive_global_index = mesh_offsets[mesh_index] + primitive_index;
				MeshSource *mesh = &result.meshes[primitive_global_index];

				if (primitive->material)
					result.mesh_to_material[primitive_global_index] = cgltf_material_index(data, primitive->material) + 1;

				// TODO: Per mesh bounding box instead of per primitive?
				Interval3 *interval = &result.bounding_boxes[primitive_global_index];
				interval->min = float3_fill(FLOAT_MAX);
				interval->max = float3_fill(FLOAT_MIN);

				ASSERT(primitive->attributes && primitive->attributes->data && primitive->indices);
				mesh->vertex_count = primitive->attributes->data->count;
				mesh->vertex_size = sizeof(Vertex);

				mesh->vertices = result.vertices + vertices_offset;
				vertices_offset += mesh->vertex_count * mesh->vertex_size;

				for (uint32_t attribute_index = 0; attribute_index < primitive->attributes_count; ++attribute_index) {
					cgltf_attribute *attribute = &primitive->attributes[attribute_index];
					cgltf_accessor *accessor = attribute->data;

					ASSERT(accessor->count == mesh->vertex_count);

					size_t offset = 0;
					switch (attribute->type) {
						case cgltf_attribute_type_position:
							offset = offsetof(Vertex, position);
							break;
						case cgltf_attribute_type_normal:
							offset = offsetof(Vertex, normal);
							break;
						case cgltf_attribute_type_tangent:
							offset = offsetof(Vertex, tangent);
							break;
						case cgltf_attribute_type_texcoord:
							offset = offsetof(Vertex, uv);
							break;
						/* case cgltf_attribute_type_color: */
						/* case cgltf_attribute_type_joints: */
						/* case cgltf_attribute_type_weights: */
						default:
							LOG_WARN("Unsupported attribute type");
							continue;
							break;
					}

					uint8_t *vertices = mesh->vertices;
					for (uint32_t vertex_index = 0; vertex_index < accessor->count; ++vertex_index, vertices += mesh->vertex_size) {
						cgltf_accessor_read_float(accessor, vertex_index, (void *)(vertices + offset), cgltf_num_components(accessor->type));
						if (attribute->type == cgltf_attribute_type_position) {
							float3 position = *((float3 *)vertices);
							interval->min = float3_min(interval->min, position);
							interval->max = float3_max(interval->max, position);
						}
					}
				}

				// Indices
				cgltf_accessor *accessor = primitive->indices;

				mesh->index_count = accessor->count;
				mesh->index_size = sizeof(uint32_t);

				mesh->indices = result.indices + indices_offset;
				indices_offset += mesh->index_size * mesh->index_count;

				size_t written = cgltf_accessor_unpack_indices(accessor, mesh->indices, mesh->index_size, mesh->index_count);
			}
		}

		ASSERT(indices_offset == result.indices_size);
		ASSERT(vertices_offset == result.vertices_size);

		// NOTE: Should not directly access entities, rather return list of nodes/transforms
		// Node -> Entity
		for (uint32_t node_index = 0; node_index < data->nodes_count; ++node_index) {
			cgltf_node *cgltf_node = &data->nodes[node_index];

			int32_t mesh_index = -1;
			const char *name = NULL;
			if (cgltf_node->mesh) {
				mesh_index = cgltf_mesh_index(data, cgltf_node->mesh);
				name = data->meshes[mesh_index].name;
			}
			LOG_INFO("NODE[%d]('%s') -> Mesh[%d]('%s')", node_index, cgltf_node->name, mesh_index, name);

			/* Entity entity = pstate->entity_count++; */
			/* TransformComponent *transform = &pstate->transforms[entity]; */
			/* MeshComponent *mesh_group = &pstate->meshes[entity]; */

			/* if (cgltf_node->parent) */
			/* 	transform->parent_index = cgltf_node_index(data, cgltf_node->parent); */

			/* cgltf_node_transform_local(cgltf_node, transform->local.elements); */
			/* cgltf_node_transform_world(cgltf_node, transform->global.elements); */

			/* // TODO: quaternion */
			/* // node->transform.rotation = float3_wrap(cgltf_node->rotation); */
			/* transform->position = float3_wrap(cgltf_node->translation); */
			/* transform->scale = float3_wrap(cgltf_node->scale); */

			/* ASSERT(cgltf_node->mesh); */
			/* mesh_group->mesh_index = mesh_offsets[cgltf_mesh_index(data, cgltf_node->mesh)] + 1; */
			/* mesh_group->mesh_count = cgltf_node->mesh->primitives_count; */

			/* pstate->components[entity] = COMPONENT_FLAG_DRAWABLE; */
		}

	} else
		LOG_ERROR("Failed to load '%.*s'", SARG(path));

	cgltf_free(data);

	return result;
}

/* bool importer_load_gltf(Arena *arena, String path, ModelSource *out_model) { */
/* 	cgltf_options options = { 0 }; */
/* 	cgltf_data *data = NULL; */
/* 	cgltf_result result = cgltf_parse_file(&options, path.chars, &data); */

/* 	if (result == cgltf_result_success) */
/* 		result = cgltf_load_buffers(&options, data, path.chars); */

/* 	if (result == cgltf_result_success) */
/* 		result = cgltf_validate(data); */

/* 	if (result != cgltf_result_success) { */
/* 		LOG_ERROR("Importer: Failed to load model"); */
/* 		return NULL; */
/* 	} */

/* 	LOG_INFO("Loading %s...", path.chars); */
/* 	logger_indent(); */

/* 	ArenaTemp scratch = arena_scratch(arena); */
/* 	String base_directory = string_push_copy(scratch.arena, string_path_folder(path)); */

/* 	out_model->image_count = data->images_count; */
/* 	out_model->images = arena_push_count(arena, out_model->image_count, ImageSource); */

/* 	for (uint32_t texture_index = 0; texture_index < out_model->image_count; ++texture_index) { */
/* 		cgltf_image *src = &data->images[texture_index]; */
/* 		ImageSource *dst = &out_model->images[texture_index]; */

/* 		if (src->buffer_view) { */
/* 			uint8_t *buffer_data = (uint8_t *)src->buffer_view->buffer->data + src->buffer_view->offset; */
/* 			String mime_type = string_wrap(src->mime_type); */

/* 			String filename = string_push_replace(scratch.arena, string_path_filename(path), S(".glb"), S("")); */
/* 			String name = string_pushf(scratch.arena, "%s_%s", filename.chars, (src->name ? src->name : "image")); */
/* 			if (string_find_first(string_wrap(src->mime_type), S("png")) != -1) { */
/* 				dst->path = string_pushf(arena, "%s/%s.png", base_directory.chars, name.chars); */
/* 				if (filesystem_file_exists(dst->path) == false) { */
/* 					uint8_t *pixels = stbi_load_from_memory(buffer_data, src->buffer_view->size, &dst->width, &dst->height, &dst->channels, 4); */
/* 					stbi_write_png(dst->path.chars, dst->width, dst->height, 4, pixels, STBI_default); */
/* 					stbi_image_free(pixels); */
/* 				} */
/* 			} else if (string_find_first(mime_type, S("jpg")) != -1 || string_find_first(mime_type, S("jpeg")) != -1) { */
/* 				dst->path = string_pushf(arena, "%s/%s.jpg", base_directory.chars, name.chars); */
/* 				if (filesystem_file_exists(dst->path) == false) { */
/* 					uint8_t *pixels = stbi_load_from_memory(buffer_data, src->buffer_view->size, &dst->width, &dst->height, &dst->channels, 4); */
/* 					stbi_write_jpg(dst->path.chars, dst->width, dst->height, dst->channels, pixels, 0); */
/* 					stbi_image_free(pixels); */
/* 				} */
/* 			} */
/* 		} else if (src->uri) { */
/* 			dst->path = string_pushf(arena, "%s/%s", base_directory.chars, src->uri); */
/* 		} */
/* 	} */

/* 	out_model->material_count = data->materials_count; */
/* 	out_model->materials = arena_push_count(arena, out_model->material_count, MaterialSource); */
/* 	LOG_INFO("Number of materials: %d", out_model->material_count); */

/* 	for (uint32_t index = 0; index < out_model->material_count; ++index) { */
/* 		cgltf_material *src = &data->materials[index]; */
/* 		MaterialSource *dst = &out_model->materials[index]; */

/* 		dst->property_count = MATERIAL_PROPERTY_COUNT; */
/* 		dst->properties = arena_push_count(arena, dst->property_count, MaterialProperty); */

/* 		memory_copy(dst->properties, default_properties, sizeof(default_properties)); */

/* 		if (src->has_pbr_metallic_roughness) { */
/* 			cgltf_pbr_metallic_roughness *pbr = &src->pbr_metallic_roughness; */
/* 			memory_copy(&dst->properties[5].as.float4, pbr->base_color_factor, sizeof(float4)); */

/* 			dst->properties[6].as.float1 = pbr->metallic_factor; */
/* 			dst->properties[7].as.float1 = pbr->roughness_factor; */

/* 			dst->properties[0].as.image = find_loaded_texture(data, out_model, pbr->base_color_texture.texture); */
/* 			dst->properties[1].as.image = find_loaded_texture(data, out_model, pbr->metallic_roughness_texture.texture); */
/* 		} */

/* 		dst->properties[2].as.image = find_loaded_texture(data, out_model, src->normal_texture.texture); */
/* 		dst->properties[3].as.image = find_loaded_texture(data, out_model, src->occlusion_texture.texture); */

/* 		memory_copy(&dst->properties[8].as.float3, src->emissive_factor, sizeof(float3)); */
/* 		dst->properties[4].as.image = find_loaded_texture(data, out_model, src->emissive_texture.texture); */
/* 	} */

/* 	uint32_t mesh_count = 0; */
/* 	for (uint32_t index = 0; index < data->meshes_count; ++index) { */
/* 		cgltf_mesh *mesh = &data->meshes[index]; */

/* 		mesh_count += mesh->primitives_count; */
/* 	} */
/* 	out_model->meshes = arena_push_count(arena, mesh_count, MeshSource); */
/* 	out_model->mesh_count = mesh_count; */
/* 	out_model->mesh_to_material = arena_push_count(arena, mesh_count, uint32_t); */

/* 	mesh_count = 0; */
/* 	for (uint32_t mesh_index = 0; mesh_index < data->meshes_count; ++mesh_index) { */
/* 		cgltf_mesh *mesh = &data->meshes[mesh_index]; */

/* 		for (uint32_t primitive_index = 0; primitive_index < mesh->primitives_count; ++primitive_index) { */
/* 			cgltf_primitive *src_mesh = &mesh->primitives[primitive_index]; */

/* 			uint32_t out_mesh_index = mesh_count++; */
/* 			MeshSource *dst_mesh = &out_model->meshes[out_mesh_index]; */

/* 			// bool has_tangents = false; */

/* 			cgltf_accessor *iaccessor = src_mesh->indices; */

/* 			dst_mesh->index_count = iaccessor->count; */
/* 			dst_mesh->index_size = iaccessor->component_type == cgltf_component_type_r_32u ? 4 : 2; */
/* 			LOG_INFO("sizeof(indices) = %zu", dst_mesh->index_count * dst_mesh->index_size); */
/* 			ASSERT( */
/* 				(src_mesh->indices->component_type == cgltf_component_type_r_16u && dst_mesh->index_size == 2) || */
/* 				(src_mesh->indices->component_type == cgltf_component_type_r_32u && dst_mesh->index_size == 4) */

/* 			); */
/* 			ASSERT(src_mesh->indices->buffer_view->size == (src_mesh->indices->count * src_mesh->indices->stride)); */

/* 			dst_mesh->indices = arena_push(arena, src_mesh->indices->buffer_view->size, dst_mesh->index_size, true); */
/* 			cgltf_size unpacked = cgltf_accessor_unpack_indices(iaccessor, dst_mesh->indices, dst_mesh->index_size, dst_mesh->index_count); */
/* 			ASSERT(unpacked == dst_mesh->index_count); */

/* 			for (uint32_t attribute_index = 0; attribute_index < src_mesh->attributes_count; ++attribute_index) { */
/* 				cgltf_attribute *attribute = &src_mesh->attributes[attribute_index]; */
/* 				cgltf_accessor *accessor = attribute->data; */

/* 				if (dst_mesh->vertices == NULL) { */
/* 					dst_mesh->vertex_count = accessor->count; */
/* 					dst_mesh->vertex_size = sizeof(Vertex); */
/* 					dst_mesh->vertices = arena_push_count(arena, dst_mesh->vertex_count * dst_mesh->vertex_size, uint8_t); */
/* 				} */

/* 				ASSERT(dst_mesh->vertex_count == accessor->count); */

/* 				switch (attribute->type) { */
/* 					case cgltf_attribute_type_position: { */
/* 						for (uint32_t vertex_index = 0; vertex_index < dst_mesh->vertex_count; ++vertex_index) { */
/* 							Vertex *dst_vertex = &((Vertex *)dst_mesh->vertices)[vertex_index]; */

/* 							cgltf_accessor_read_float(accessor, vertex_index, vector3f_elements(&dst_vertex->position), 3); */
/* 						} */
/* 						ASSERT(accessor->stride == 12); */
/* 					} break; */
/* 					case cgltf_attribute_type_texcoord: { */
/* 						ASSERT(accessor->component_type == cgltf_component_type_r_32f); */
/* 						for (uint32_t vertex_index = 0; vertex_index < dst_mesh->vertex_count; ++vertex_index) { */
/* 							Vertex *dst_vertex = &((Vertex *)dst_mesh->vertices)[vertex_index]; */

/* 							cgltf_accessor_read_float(accessor, vertex_index, vector2f_elements(&dst_vertex->uv), 2); */
/* 						} */
/* 					} break; */
/* 					case cgltf_attribute_type_normal: { */
/* 						ASSERT(accessor->component_type == cgltf_component_type_r_32f); */
/* 						for (uint32_t vertex_index = 0; vertex_index < dst_mesh->vertex_count; ++vertex_index) { */
/* 							Vertex *dst_vertex = &((Vertex *)dst_mesh->vertices)[vertex_index]; */

/* 							cgltf_accessor_read_float(accessor, vertex_index, vector3f_elements(&dst_vertex->normal), 3); */
/* 						} */
/* 					} break; */
/* 					case cgltf_attribute_type_tangent: { */
/* 						ASSERT(accessor->component_type == cgltf_component_type_r_32f); */
/* 						for (uint32_t vertex_index = 0; vertex_index < dst_mesh->vertex_count; ++vertex_index) { */
/* 							Vertex *dst_vertex = &((Vertex *)dst_mesh->vertices)[vertex_index]; */

/* 							// has_tangents = true; */

/* 							cgltf_accessor_read_float(accessor, vertex_index, vector4f_elements(&dst_vertex->tangent), 4); */
/* 						} */
/* 					} break; */
/* 					case cgltf_attribute_type_invalid: */
/* 					case cgltf_attribute_type_color: */
/* 					case cgltf_attribute_type_joints: */
/* 					case cgltf_attribute_type_weights: */
/* 					case cgltf_attribute_type_custom: */
/* 					case cgltf_attribute_type_max_enum: */
/* 						break; */
/* 				} */
/* 			} */

/* 			if (src_mesh->material) { */
/* 				uint32_t index = src_mesh->material - data->materials; */
/* 				out_model->mesh_to_material[out_mesh_index] = index; */
/* 			} */

/* 			// if (has_tangents == false) */
/* 			// 	calculate_tangents(dst_mesh.vertices, dst_mesh.vertex_count, dst_mesh.indices, dst_mesh.index_count); */
/* 		} */
/* 	} */

/* 	logger_dedent(); */
/* 	cgltf_free(data); */

/* 	arena_scratch_end(scratch); */
/* 	return true; */
/* } */
