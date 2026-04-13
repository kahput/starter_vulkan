#include "assets/asset_types.h"
#include "assets/importer.h"
#include "assets/mesh_source.h"

#include "common.h"
#include "core/arena.h"
#include "core/cmath.h"
#include "core/debug.h"
#include "core/logger.h"
#include "core/r_types.h"
#include "core/strings.h"
#include "game_interface.h"
#include "input.h"
#include "input/input_types.h"
#include "platform.h"
#include "renderer/backend/vulkan_api.h"
#include "renderer/r_internal.h"
#include "scene.h"

#include "cgltf.h"
#include <math.h>

static MaterialProperty default_properties[] = {
	{ .name = { .chars = "u_base_color_texture", .length = 20 }, .type = PROPERTY_TYPE_IMAGE, .as.uint32x1 = 0 },
	{ .name = { .chars = "u_metallic_roughness_texture", .length = 28 }, .type = PROPERTY_TYPE_IMAGE, .as.uint32x1 = 0 },
	{ .name = { .chars = "u_normal_texture", .length = 16 }, .type = PROPERTY_TYPE_IMAGE, .as.uint32x1 = 0 },
	{ .name = { .chars = "u_occlusion_texture", .length = 19 }, .type = PROPERTY_TYPE_IMAGE, .as.uint32x1 = 0 },
	{ .name = { .chars = "u_emissive_texture", .length = 18 }, .type = PROPERTY_TYPE_IMAGE, .as.uint32x1 = 0 },

	{ .name = { .chars = "base_color_factor", .length = 17 }, .type = PROPERTY_TYPE_FLOAT4, .as.float32x4 = { 0.8f, 0.8f, 0.8f, 1.0f } },
	{ .name = { .chars = "metallic_factor", .length = 15 }, .type = PROPERTY_TYPE_FLOAT1, .as.float32x1 = 0.0f },
	{ .name = { .chars = "roughness_factor", .length = 16 }, .type = PROPERTY_TYPE_FLOAT1, .as.float32x1 = 0.5f },
	{ .name = { .chars = "emissive_factor", .length = 15 }, .type = PROPERTY_TYPE_FLOAT3, .as.float32x3 = { 1.0f, 1.0f, 1.0f } },
};

typedef struct Editor {
	float sensitivity, pan_speed, zoom_speed;
	Camera camera;
} Editor;

Editor editor_make(void);
void editor_update(Editor *editor, float dt);

typedef enum {
	GAME_STATE_PLAY,
	GAME_STATE_EDITOR,
} GameState;

typedef struct {
	RhiBuffer vertex_buffer;
	RhiBuffer index_buffer;

	uint32_t index_count;
	uint32_t vertex_count;
} Mesh;

typedef struct {
	uint32_t parent_index, mesh_index, mesh_count;

	float44 local_transform;
} SceneNode;

typedef struct {
	Arena arena;
	bool initialized;

	RhiSampler linear_sampler;
	RhiTexture white;

	RhiBuffer global_uniform_buffer;
	RhiShader pbr_shader;

	RhiBuffer quad_geometry;

	RhiTexture *images;
	RhiBuffer material_properties;

	Mesh *meshes;
	MaterialSource *materials;

	uint32_t *mesh_to_material;

	SceneNode *nodes;

	GameState state;
	Editor editor;
	Camera game_camera;

	Camera *camera;
} PermanentState;

FrameInfo update_and_render(GameContext *context, float dt) {
	PermanentState *pstate = context->permanent_memory;

	if (pstate->initialized == false) {
		pstate->arena = arena_wrap((uint8_t *)context->permanent_memory + sizeof(PermanentState), context->permanent_memory_size - sizeof(PermanentState));

		pstate->global_uniform_buffer = vulkan_buffer_make(context->render, BUFFER_TYPE_UNIFORM, BUFFER_MEMORY_SHARED, sizeof(Matrix4f) * 2, NULL);

		ArenaTemp scratch = arena_scratch_begin(NULL);

		pstate->pbr_shader =
			vulkan_shader_make(
				NULL,
				context->render,
				importer_load_shader(scratch.arena, S("assets/shaders/pbr.vert.spv"), S("assets/shaders/pbr.frag.spv")),
				NULL);
		pstate->linear_sampler = vulkan_sampler_make(context->render, LINEAR_SAMPLER);
		pstate->white = vulkan_texture_make(context->render, 1, 1, TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8, TEXTURE_USAGE_SAMPLED, &(uint32_t){ 0xffffffff });

		Vertex quad_vertices[] = {
			{ .position = { -1.0f, 1.0f, 0.0f } },
			{ .position = { -1.0f, -1.0f, 0.0f } },
			{ .position = { 1.0f, 1.0f, 0.0f } },

			{ .position = { 1.0f, 1.0f, 0.0f } },
			{ .position = { -1.0f, -1.0f, 0.0f } },
			{ .position = { 1.0f, -1.0f, 0.0f } }
		};
		pstate->quad_geometry = vulkan_buffer_make(context->render, BUFFER_TYPE_VERTEX, BUFFER_MEMORY_DEVICE, sizeof(quad_vertices), quad_vertices);

		// Load model
		String path = S("assets/models/custom/room.glb");
		String directory = stringpath_directory(path);

		MeshSource *mesh_sources = NULL;
		uint32_t *mesh_to_material = NULL;

		// 0 == default
		MaterialSource *materials = NULL;
		MaterialSource defaul_material = {
			.properties = arena_push_count(scratch.arena, countof(default_properties), MaterialProperty),
			.property_count = countof(default_properties),
		};
		memory_copy(defaul_material.properties, default_properties, sizeof(default_properties));
		arena_darray_put(scratch.arena, materials, MaterialSource, defaul_material);

		// 0 == invalid
		ImageSource *images = NULL;
		arena_darray_push(scratch.arena, images, ImageSource);

		SceneNode *nodes = NULL;
		{
			cgltf_options options = { 0 };
			cgltf_data *data = NULL;
			cgltf_result cgltf_result = cgltf_parse_file(&options, path.chars, &data);

			if (cgltf_result == cgltf_result_success)
				cgltf_result = cgltf_load_buffers(&options, data, path.chars);

			if (cgltf_result == cgltf_result_success)
				cgltf_result = cgltf_validate(data);

			LOG_INFO("Loading %.*s", SARG(path));

			if (cgltf_result == cgltf_result_success) {
				for (uint32_t image_index = 0; image_index < data->images_count; ++image_index) {
					cgltf_image *src = &data->images[image_index];
					ImageSource *dst = arena_darray_push(scratch.arena, images, ImageSource);

					String image_path = stringpath_join(scratch.arena, directory, string_wrap(src->uri));
					LOG_INFO("'%s' -> '%.*s'", src->name, SARG(image_path));
					*dst = importer_load_image(scratch.arena, image_path);
				}

				for (uint32_t material_index = 0; material_index < data->materials_count; ++material_index) {
					cgltf_material *src = &data->materials[material_index];
					MaterialSource *dst = arena_darray_push(scratch.arena, materials, MaterialSource);

					dst->property_count = countof(default_properties);
					dst->properties = arena_push_count(scratch.arena, dst->property_count, MaterialProperty);

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
				LOG_INFO("Material count = %d", arena_array_count(materials));

				// Meshes
				uint32_t *mesh_offsets = arena_push_count(scratch.arena, data->meshes_count, uint32_t);
				uint32_t mesh_offset = 0;
				for (uint32_t mesh_index = 0; mesh_index < data->meshes_count; ++mesh_index) {
					cgltf_mesh *mesh = &data->meshes[mesh_index];

					mesh_offsets[mesh_index] = mesh_offset;
					mesh_offset += mesh->primitives_count;

					LOG_INFO("\n%s {\n  primitive_count = %d \n}",
						mesh->name, mesh->primitives_count);
					for (uint32_t primitive_index = 0; primitive_index < mesh->primitives_count; ++primitive_index) {
						cgltf_primitive *primitive = &mesh->primitives[primitive_index];
						MeshSource *mesh = arena_darray_push(scratch.arena, mesh_sources, MeshSource);
						if (primitive->material)
							arena_darray_put(scratch.arena, mesh_to_material, uint32_t, cgltf_material_index(data, primitive->material) + 1);
						else
							arena_darray_put(scratch.arena, mesh_to_material, uint32_t, 0);

						ASSERT(primitive->attributes && primitive->attributes->data);
						mesh->vertex_count = primitive->attributes->data->count;
						mesh->vertex_size = sizeof(Vertex);
						mesh->vertices = (uint8_t *)arena_push_count(scratch.arena, mesh->vertex_count, Vertex);

						for (uint32_t attribute_index = 0; attribute_index < primitive->attributes_count; ++attribute_index) {
							cgltf_attribute *attribute = &primitive->attributes[attribute_index];
							cgltf_accessor *accessor = attribute->data;

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
									ASSERT_MESSAGE(false, "Unsupported attribute type");
									break;
							}

							uint8_t *vertices = mesh->vertices;
							for (uint32_t vertex_index = 0; vertex_index < accessor->count; ++vertex_index, vertices += mesh->vertex_size)
								cgltf_accessor_read_float(accessor, vertex_index, (void *)(vertices + offset), cgltf_num_components(accessor->type));
						}

						// Indices
						cgltf_accessor *accessor = primitive->indices;

						mesh->index_count = accessor->count;
						mesh->index_size = sizeof(uint32_t);
						mesh->indices = (uint8_t *)arena_push_count(scratch.arena, mesh->index_count, uint32_t);
						size_t written = cgltf_accessor_unpack_indices(accessor, mesh->indices, mesh->index_size, mesh->index_count);
					}
				}

				// Nodes
				for (uint32_t node_index = 0; node_index < data->nodes_count; ++node_index) {
					cgltf_node *cgltf_node = &data->nodes[node_index];
					SceneNode *node = arena_darray_push(scratch.arena, nodes, SceneNode);

					if (cgltf_node->parent)
						node->parent_index = cgltf_node_index(data, cgltf_node->parent);
					else
						node->parent_index = UINT32_MAX;

					cgltf_node_transform_local(cgltf_node, node->local_transform.elements);

					ASSERT(cgltf_node->mesh);
					node->mesh_index = mesh_offsets[cgltf_mesh_index(data, cgltf_node->mesh)];
					node->mesh_count = cgltf_node->mesh->primitives_count;
				}

			} else
				LOG_ERROR("Failed to load '%.*s'", SARG(path));

			pstate->meshes = arena_push_count(&pstate->arena, arena_array_count(mesh_sources), Mesh);
			for (uint32_t mesh_index = 0; mesh_index < arena_array_count(mesh_sources); ++mesh_index) {
				Mesh *mesh = &pstate->meshes[mesh_index];
				MeshSource source = mesh_sources[mesh_index];

				mesh->vertex_buffer = vulkan_buffer_make(
					context->render,
					BUFFER_TYPE_VERTEX, BUFFER_MEMORY_DEVICE,
					source.vertex_size * source.vertex_count, source.vertices);

				mesh->index_buffer = vulkan_buffer_make(
					context->render,
					BUFFER_TYPE_INDEX, BUFFER_MEMORY_DEVICE,
					source.index_size * source.index_count, source.indices);

				mesh->index_count = source.index_count;
				mesh->vertex_count = source.vertex_count;
			}

			pstate->images = arena_push_count(&pstate->arena, arena_array_count(images), RhiTexture);
			for (uint32_t image_index = 1; image_index < arena_array_count(images); ++image_index) {
				ImageSource *src = &images[image_index];
				RhiTexture *dst = &pstate->images[image_index];

				*dst = vulkan_texture_make(
					context->render,
					src->width, src->height,
					TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8_SRGB,
					TEXTURE_USAGE_SAMPLED, src->pixels);
			}

			pstate->material_properties = vulkan_buffer_make_array(
				context->render,
				BUFFER_TYPE_UNIFORM, BUFFER_MEMORY_SHARED,
				arena_array_count(materials),
				sizeof(MaterialParameters));

			ASSERT(materials);
			for (uint32_t material_index = 0; material_index < arena_array_count(materials); ++material_index) {
				MaterialSource *material = &materials[material_index];

				MaterialParameters parameters = {
					.base_color_factor = material->properties[5].as.float32x4,
					.metallic_factor = material->properties[6].as.float32x1,
					.roughness_factor = material->properties[7].as.float32x1,
					.emissive_factor = material->properties[8].as.float32x3
				};

				vulkan_buffer_array_index_write_all(
					context->render,
					pstate->material_properties,
					material_index,
					sizeof(MaterialParameters),
					&parameters);
			}

			pstate->nodes = arena_array_copy(&pstate->arena, nodes, SceneNode);
			pstate->materials = arena_array_copy(&pstate->arena, materials, MaterialSource);
			pstate->mesh_to_material = arena_array_copy(&pstate->arena, mesh_to_material, uint32_t);

			cgltf_free(data);
		}

		pstate->game_camera = (Camera){
			.position = { 0.0f, 20.0f, -30.0f },
			.up = { 0.0f, 1.0f, 0.0f },
			.target = { 0.0f, 0.0f, 0.0f },
			.fov = 45.f,

			.projection = CAMERA_PROJECTION_PERSPECTIVE
		};
		pstate->editor = editor_make();

		pstate->camera = &pstate->editor.camera;

		arena_scratch_end(scratch);

		pstate->initialized = true;
	}

	if (input_mouse_down(MOUSE_BUTTON_MIDDLE))
		window_set_cursor_locked(context->display, true);
	else
		window_set_cursor_locked(context->display, false);

	if (input_key_pressed(KEY_CODE_TAB)) {
		pstate->state = !pstate->state;
		if (pstate->state == GAME_STATE_EDITOR) {
			pstate->camera = &pstate->editor.camera;
		} else {
			pstate->camera = &pstate->game_camera;
		}
	}

	if (pstate->state == GAME_STATE_EDITOR)
		editor_update(&pstate->editor, dt);

	uint2 window_size = window_size_pixel(context->display);
	ArenaTemp scratch = arena_scratch_begin(NULL);
	if (vulkan_frame_begin(context->render, window_size.x, window_size.y)) {
		Camera *camera = pstate->camera;

		Matrix4f projection = float44_perspective(DEG2RAD(camera->fov), (float)window_size.x / (float)window_size.y, 0.01f, 1000.f);
		projection.elements[5] *= -1;
		Matrix4f view = float44_lookat(camera->position, camera->target, camera->up);

		vulkan_buffer_write(context->render, pstate->global_uniform_buffer, 0, sizeof(Matrix4f), &projection);
		vulkan_buffer_write(context->render, pstate->global_uniform_buffer, sizeof(Matrix4f), sizeof(Matrix4f), &view);

		RhiUniformSet global_set = vulkan_uniformset_push(context->render, pstate->pbr_shader, 0);
		vulkan_uniformset_bind_buffer(context->render, global_set, 0, pstate->global_uniform_buffer);

		DrawListDesc desc = {
			.color_attachments[0] = {
			  .clear.color = { 1.0f, 1.0f, 1.0f, 1.0f },
			  .store = STORE,
			  .load = CLEAR,
			},
			.color_attachment_count = 1,
			.depth_attachment = {
			  .store = STORE,
			  .load = CLEAR,
			},
			.use_depth = true
		};

		if (vulkan_drawlist_begin(context->render, desc)) {
			PipelineDesc pipeline = DEFAULT_PIPELINE;
			vulkan_shader_bind(context->render, pstate->pbr_shader, pipeline);
			vulkan_uniformset_bind(context->render, global_set);

			for (uint32_t node_index = 0; node_index < arena_array_count(pstate->nodes); ++node_index) {
				SceneNode *node = &pstate->nodes[node_index];
				float44 transform = node->local_transform;

				SceneNode *p = node;
				while (p->parent_index != UINT32_MAX) {
					p = &pstate->nodes[p->parent_index];
					transform = float44_multiply(transform, p->local_transform);
				}

				for (uint32_t index = 0; index < node->mesh_count; ++index) {
					Mesh *mesh = &pstate->meshes[node->mesh_index + index];
					uint32_t material_index = pstate->mesh_to_material[indexof(pstate->meshes, mesh)];
					MaterialSource *material = &pstate->materials[material_index];

					RhiUniformSet group = vulkan_uniformset_push(context->render, pstate->pbr_shader, 1);
					vulkan_uniformset_bind_buffer_array_index(
						context->render, group, 0,
						pstate->material_properties, material_index);
					for (uint32_t index = 0; index < 5; ++index) {
						RhiTexture texture = pstate->white;
						if (material->properties[index].as.uint32x1) {
							texture = pstate->images[material->properties[index].as.uint32x1];
						}
						vulkan_uniformset_bind_texture(
							context->render,
							group,
							1 + index,
							texture,
							pstate->linear_sampler);
					}

					vulkan_uniformset_bind(context->render, group);

					vulkan_push_constants(context->render, 0, sizeof(float44), &transform);
					vulkan_buffer_bind(context->render, mesh->vertex_buffer, 0);
					if (mesh->index_count > 0) {
						vulkan_buffer_bind(context->render, mesh->index_buffer, 0);
						vulkan_renderer_draw_indexed(context->render, mesh->index_count);
					} else
						vulkan_renderer_draw(context->render, mesh->vertex_count);
				}
			}

			vulkan_drawlist_end(context->render);
		}

		vulkan_frame_end(context->render);
	}

	arena_scratch_end(scratch);

	return (FrameInfo){ 0 };
}

GameInterface game_hookup(void) {
	GameInterface interface = (GameInterface){
		.on_update = update_and_render,
	};
	return interface;
}

Editor editor_make(void) {
	Editor result = {
		.pan_speed = 0.005f,
		.zoom_speed = 0.05f,
		.sensitivity = 0.005f,
		.camera = {
		  .position = { 0.0f, 20, 30 },
		  .target = { 0.0f, 0.0, 0.0f },
		  .up = { 0.0, 1.0f, 0.0f },
		  .fov = 45.f,

		  .projection = CAMERA_PROJECTION_PERSPECTIVE,
		}
	};

	return result;
}

void editor_update(Editor *editor, float dt) {
	Camera *camera = &editor->camera;
	float2 mouse_delta = (float2){ input_mouse_dx(), input_mouse_dy() };

	if (input_mouse_down(MOUSE_BUTTON_MIDDLE) && input_key_down(KEY_CODE_LEFTSHIFT)) {
		float2 shift = float2_scale(mouse_delta, editor->pan_speed);
		shift.y *= -1;

		float3 camera_target_offset = float3_subtract(camera->target, camera->position);
		float3 camera_forward = float3_normalize(camera_target_offset);

		float3 camera_right = float3_cross(camera->up, camera_forward);
		float3 camera_up = float3_cross(camera_right, camera_forward);

		camera->position = float3_add(camera->position, float3_scale(camera_right, shift.x));
		camera->position = float3_add(camera->position, float3_scale(camera_up, shift.y));

		camera->target = float3_add(camera->position, camera_target_offset);

	} else if (input_mouse_down(MOUSE_BUTTON_MIDDLE) && input_key_down(KEY_CODE_LEFTCTRL)) {
		float zoom = mouse_delta.y * editor->zoom_speed;
		float3 camera_forward = float3_normalize(float3_subtract(camera->target, camera->position));
		camera->position = float3_add(camera->position, float3_scale(camera_forward, -zoom));
	} else if (input_mouse_down(MOUSE_BUTTON_MIDDLE)) {
		float yaw_delta = mouse_delta.x * editor->sensitivity;
		float pitch_delta = mouse_delta.y * editor->sensitivity;
		/*
		 * x = RADIUS * cos(azimuth) * sin(theta) + offset.x;
		 * y = RADIUS * cos(theta) + offset.y
		 * z = RADIUS * sin(azimuth) * sin(theta) + offset.z;
		 */

		float3 camera_position = float3_subtract(camera->position, camera->target);
		float r = MAX(float3_length(camera_position), EPSILON);

		float current_theta = 0;
		// Manual (theta) vs atan2 (azimuth)
		float2 camera_xz = { camera_position.x, camera_position.z };
		if (camera_position.y > 0)
			current_theta = atanf(float2_length(camera_xz) / camera_position.y);
		else if (camera_position.y < 0)
			current_theta = C_PIf + atanf(float2_length(camera_xz) / camera_position.y);
		else if (camera_position.y < EPSILON && camera_position.y > -EPSILON)
			current_theta = C_PIf * 0.5f;
		else
			ASSERT(false);
		float current_azimuth = atan2f(camera_position.z, camera_position.x); // [-pi, pi]

		current_theta = CLAMP(current_theta - pitch_delta, EPSILON * 2, C_PIf - EPSILON * 2);
		current_azimuth += yaw_delta;

		// Apply move
		camera->position = float3_add(
			(float3){
			  .x = r * sinf(current_theta) * cosf(current_azimuth),
			  .y = r * cosf(current_theta),
			  .z = r * sinf(current_theta) * sinf(current_azimuth),
			},
			camera->target);
	}
}
