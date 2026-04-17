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
#include "event.h"
#include "events/platform_events.h"
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

typedef enum {
	GAME_STATE_PLAY,
	GAME_STATE_EDITOR,
} GameState;

typedef struct {
	size_t vertex_offset, vertex_count;
	size_t index_offset, index_count;
} Mesh;

typedef struct {
	float3 position, scale, rotation;

	float4x4 local, global;
} Transform3D;

typedef struct {
	float3 min, max;
} Interval3D;

typedef struct {
	uint32_t parent_index, mesh_index, mesh_count;

	Transform3D transform;
} SceneNode;

typedef struct Editor {
	float sensitivity, pan_speed, zoom_speed;
	Camera camera;

	bool grab;
	float2 grab_mouse_position;
	Transform3D cached_selection_transform;
} Editor;

typedef struct {
	Arena arena;
	bool initialized;

	VulkanContext *context;
	Window *display;

	RhiSampler linear_sampler;
	RhiTexture white;

	RhiShader unlit_shader, pbr_shader, terrain_shader;
	RhiShader line_shader, picker_shader;

	RhiBuffer frame_uniform_buffer;
	RhiBuffer frame_storage_buffer;

	RhiBuffer scene_uniform_buffer;
	RhiBuffer scene_geometry_buffer;

	RhiTexture picker_target;

	RhiBuffer quad_geometry;

	RhiTexture *images;

	Mesh *meshes;
	Interval3D *bounds;
	MaterialSource *materials;
	uint64x2 *material_data;
	uint32_t *mesh_to_material;

	SceneNode *nodes;

	uint32_t selected_node;

	GameState state;
	Editor editor;

	Camera game_camera;
	float target_azimuth, target_theta;

	struct {
		float3 position;
		Mesh mesh;
	} player;

	Camera *camera;
} PermanentState;

Editor editor_make(void);
void editor_update_and_draw(PermanentState *state, Editor *editor, float dt);

bool window_resize(EventCode code, void *event, void *receiver) {
	WindowResizeEvent *resize_event = event;
	PermanentState *pstate = receiver;

	vulkan_texture_resize(pstate->context, pstate->picker_target, resize_event->width, resize_event->height);
	return false;
}

FrameInfo update_and_draw(GameContext *context, float dt) {
	PermanentState *pstate = context->permanent_memory;
	pstate->context = context->render;
	pstate->display = context->display;

	if (pstate->initialized == false) {
		pstate->arena = arena_wrap((uint8_t *)context->permanent_memory + sizeof(PermanentState), context->permanent_memory_size - sizeof(PermanentState));

		event_subscribe(EVENT_PLATFORM_WINDOW_RESIZED, window_resize, pstate);

		pstate->frame_uniform_buffer = vulkan_buffer_make(pstate->context, BUFFER_USAGE_UNIFORM, BUFFER_MEMORY_SHARED, MiB(8), NULL);
		pstate->frame_storage_buffer = vulkan_buffer_make(pstate->context, BUFFER_USAGE_STORAGE, BUFFER_MEMORY_SHARED, MiB(32), NULL);

		pstate->scene_uniform_buffer = vulkan_buffer_make(pstate->context, BUFFER_USAGE_UNIFORM, BUFFER_MEMORY_SHARED, MiB(32), NULL);
		pstate->scene_geometry_buffer = vulkan_buffer_make(pstate->context, BUFFER_USAGE_INDEX | BUFFER_USAGE_VERTEX, BUFFER_MEMORY_DEVICE, MiB(128), NULL);

		ArenaTemp scratch = arena_scratch_begin(NULL);

		pstate->unlit_shader =
			vulkan_shader_make(
				NULL,
				pstate->context,
				importer_load_shader(scratch.arena, S("assets/shaders/unlit.vert.spv"), S("assets/shaders/unlit.frag.spv")),
				NULL);
		pstate->pbr_shader =
			vulkan_shader_make(
				NULL,
				pstate->context,
				importer_load_shader(scratch.arena, S("assets/shaders/pbr.vert.spv"), S("assets/shaders/pbr.frag.spv")),
				NULL);
		pstate->terrain_shader =
			vulkan_shader_make(
				NULL,
				pstate->context,
				importer_load_shader(scratch.arena, S("assets/shaders/procedural.vert.spv"), S("assets/shaders/procedural.frag.spv")),
				NULL);
		pstate->line_shader =
			vulkan_shader_make(
				NULL,
				pstate->context,
				importer_load_shader(scratch.arena, S("assets/shaders/line.vert.spv"), S("assets/shaders/line.frag.spv")),
				NULL);
		pstate->line_shader =
			vulkan_shader_make(
				NULL,
				pstate->context,
				importer_load_shader(scratch.arena, S("assets/shaders/line.vert.spv"), S("assets/shaders/line.frag.spv")),
				NULL);
		pstate->picker_shader =
			vulkan_shader_make(
				NULL,
				pstate->context,
				importer_load_shader(scratch.arena, S("assets/shaders/picker.vert.spv"), S("assets/shaders/picker.frag.spv")),
				NULL);

		pstate->linear_sampler = vulkan_sampler_make(pstate->context, LINEAR_SAMPLER);
		pstate->white = vulkan_texture_make(pstate->context, 1, 1, TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8, TEXTURE_USAGE_SAMPLED, &(uint32_t){ 0xffffffff });

		uint32x2 window_size = window_size_pixel(context->display);
		pstate->picker_target = vulkan_texture_make(
			pstate->context,
			window_size.x, window_size.y,
			TEXTURE_TYPE_2D, TEXTURE_FORMAT_R32,
			TEXTURE_USAGE_SAMPLED | TEXTURE_USAGE_RENDER_TARGET | TEXTURE_USAGE_READBACK,
			NULL);

		Vertex quad_vertices[] = {
			{ .position = { -1.0f, 1.0f, 0.0f } },
			{ .position = { -1.0f, -1.0f, 0.0f } },
			{ .position = { 1.0f, 1.0f, 0.0f } },

			{ .position = { 1.0f, 1.0f, 0.0f } },
			{ .position = { -1.0f, -1.0f, 0.0f } },
			{ .position = { 1.0f, -1.0f, 0.0f } }
		};
		pstate->quad_geometry = vulkan_buffer_make(pstate->context, BUFFER_USAGE_VERTEX, BUFFER_MEMORY_DEVICE, sizeof(quad_vertices), quad_vertices);

		// Load model
		String path = S("assets/models/custom/room.glb");
		String directory = stringpath_directory(path);

		MeshSource *mesh_sources = NULL;
		uint32_t *mesh_to_material = NULL;

		// 0 == default
		MaterialSource *materials = NULL;
		MaterialSource default_material = {
			.properties = arena_push_count(&pstate->arena, countof(default_properties), MaterialProperty),
			.property_count = countof(default_properties),
		};
		memory_copy(default_material.properties, default_properties, sizeof(default_properties));
		arena_darray_put(scratch.arena, materials, MaterialSource, default_material);

		// 0 == invalid
		ImageSource *images = NULL;
		arena_darray_push(scratch.arena, images, ImageSource);

		SceneNode *nodes = NULL;
		Interval3D *bounds = NULL;
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
					dst->properties = arena_push_count(&pstate->arena, dst->property_count, MaterialProperty);

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

						// TODO: Per mesh bounding box instead of per primitive?
						Interval3D *interval = arena_darray_push(scratch.arena, bounds, Interval3D);
						interval->min = float3_fill(FLOAT_MAX);
						interval->max = float3_fill(FLOAT_MIN);

						ASSERT(primitive->attributes && primitive->attributes->data);
						mesh->vertex_count = primitive->attributes->data->count;
						mesh->vertex_size = sizeof(Vertex);
						mesh->vertices = (uint8_t *)arena_push_count(&pstate->arena, mesh->vertex_count, Vertex);

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
							for (uint32_t vertex_index = 0; vertex_index < accessor->count; ++vertex_index, vertices += mesh->vertex_size) {
								cgltf_accessor_read_float(accessor, vertex_index, (void *)(vertices + offset), cgltf_num_components(accessor->type));
								if (offset == offsetof(Vertex, position)) {
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
						mesh->indices = (uint8_t *)arena_push_count(&pstate->arena, mesh->index_count, uint32_t);
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

					cgltf_node_transform_local(cgltf_node, node->transform.local.elements);
					cgltf_node_transform_world(cgltf_node, node->transform.global.elements);

					// TODO: quaternion
					// node->transform.rotation = float3_wrap(cgltf_node->rotation);
					node->transform.position = float3_wrap(cgltf_node->translation);
					node->transform.scale = float3_wrap(cgltf_node->scale);

					ASSERT(cgltf_node->mesh);
					node->mesh_index = mesh_offsets[cgltf_mesh_index(data, cgltf_node->mesh)];
					node->mesh_count = cgltf_node->mesh->primitives_count;
				}

			} else
				LOG_ERROR("Failed to load '%.*s'", SARG(path));

			pstate->meshes = arena_push_count(&pstate->arena, arena_array_count(mesh_sources) + 1, Mesh);
			for (uint32_t mesh_index = 0; mesh_index < arena_array_count(mesh_sources); ++mesh_index) {
				Mesh *mesh = &pstate->meshes[mesh_index];
				MeshSource source = mesh_sources[mesh_index];

				size_t vertices_size = source.vertex_size * source.vertex_count;
				size_t indices_size = source.index_size * source.index_count;

				// Nothing uploaded yet, only the offset is incremented
				mesh->vertex_offset = vulkan_buffer_push(
					pstate->context,
					pstate->scene_geometry_buffer,
					vertices_size);
				mesh->index_offset = vulkan_buffer_push(
					pstate->context,
					pstate->scene_geometry_buffer,
					indices_size);

				// NOTE: This creates oneshot command buffer for each upload, batch instead
				vulkan_buffer_write(pstate->context,
					pstate->scene_geometry_buffer,
					mesh->vertex_offset, vertices_size, source.vertices);
				vulkan_buffer_write(pstate->context,
					pstate->scene_geometry_buffer,
					mesh->index_offset, indices_size, source.indices);

				mesh->index_count = source.index_count;
				mesh->vertex_count = source.vertex_count;
			}

			pstate->images = arena_push_count(&pstate->arena, arena_array_count(images), RhiTexture);
			for (uint32_t image_index = 1; image_index < arena_array_count(images); ++image_index) {
				ImageSource *src = &images[image_index];
				RhiTexture *dst = &pstate->images[image_index];

				*dst = vulkan_texture_make(
					pstate->context,
					src->width, src->height,
					TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8_SRGB,
					TEXTURE_USAGE_SAMPLED, src->pixels);
			}

			ASSERT(materials);
			uint64x2 *material_data = NULL;
			for (uint32_t material_index = 0; material_index < arena_array_count(materials); ++material_index) {
				MaterialSource *material = &materials[material_index];

				MaterialParameters parameters = {
					.base_color_factor = material->properties[5].as.float32x4,
					.metallic_factor = material->properties[6].as.float32x1,
					.roughness_factor = material->properties[7].as.float32x1,
					.emissive_factor = material->properties[8].as.float32x3
				};

				size_t size = sizeof(MaterialParameters);
				size_t offset = vulkan_buffer_push(pstate->context, pstate->scene_uniform_buffer, size);
				arena_darray_put(scratch.arena, material_data, uint64x2, { offset, size });
				vulkan_buffer_write_all(pstate->context, pstate->scene_uniform_buffer, offset, size, &parameters);
			}

			MeshSource player_src = mesh_source_cube(scratch.arena, 0, 0.5f, 0);
			size_t player_geometry_size = player_src.vertex_count * player_src.vertex_size;
			Mesh player_mesh = {
				.vertex_count = player_src.vertex_count,
				.vertex_offset = vulkan_buffer_push(pstate->context, pstate->scene_geometry_buffer, player_geometry_size),
			};
			vulkan_buffer_write(pstate->context, pstate->scene_geometry_buffer, player_mesh.vertex_offset, player_geometry_size, player_src.vertices);
			pstate->player.position = (float3){ 0 },
			pstate->player.mesh = player_mesh;

			pstate->nodes = arena_array_copy(&pstate->arena, nodes, SceneNode);
			pstate->materials = arena_array_copy(&pstate->arena, materials, MaterialSource);
			pstate->mesh_to_material = arena_array_copy(&pstate->arena, mesh_to_material, uint32_t);
			pstate->material_data = arena_array_copy(&pstate->arena, material_data, uint64x2);
			pstate->bounds = arena_array_copy(&pstate->arena, bounds, Interval3D);

			cgltf_free(data);
		}

		pstate->game_camera = (Camera){
			.position = { 0.0f, 20.0f, 30.0f },
			.up = { 0.0f, 1.0f, 0.0f },
			.target = { 0.0f, 0.0f, 0.0f },
			.fov = 45.f,

			.projection = CAMERA_PROJECTION_PERSPECTIVE
		};
		pstate->target_azimuth = C_PIf * 3 / 2.f;
		pstate->target_theta = C_PIf / 3.f;

		pstate->editor = editor_make();

		pstate->camera = &pstate->editor.camera;
		pstate->state = GAME_STATE_EDITOR;

		arena_scratch_end(scratch);

		pstate->initialized = true;
	}

	ArenaTemp scratch = arena_scratch_begin(NULL);

	// Procedural terrain
	/* MeshSourceList list = { 0 }; */
	/* uint32_t size = 128; */
	/* for (uint32_t z = 0; z < size; ++z) { */
	/* for (uint32_t x = 0; x < size; x++) { */
	/* for (int32_t face_index = 0; face_index < 6; ++face_index) { */
	/* float x_offset = (float)x - ((float)size * .5f); */
	/* float z_offset = (float)z - ((float)size * .5f); */

	/* if (face_index == CUBE_FACE_RIGHT && x > 0 && x + 1 < size) continue; */
	/* if (face_index == CUBE_FACE_LEFT && x > 0 && x < size - 1) continue; */
	/* if (face_index == CUBE_FACE_FRONT && z > 0 && z + 1 < size) continue; */
	/* if (face_index == CUBE_FACE_BACK && z > 0 && z < size - 1) continue; */

	/* if (x == 0 || x + 1 == size || z == 0 || z + 1 == size) { */
	/* for (uint32_t y = 0; y < 3; ++y) { */
	/* if (face_index == CUBE_FACE_TOP && y + 1 < 3) continue; */
	/* if (face_index == CUBE_FACE_BOTTOM) continue; */
	/* MeshSource wall = mesh_source_cube_face_create( */
	/* scratch.arena, x_offset, (float)y - 1, z_offset, face_index); */
	/* mesh_source_list_push(scratch.arena, &list, wall); */
	/* } */
	/* if (face_index == CUBE_FACE_TOP) continue; */
	/* } */

	/* MeshSource source = mesh_source_cube_face_create( */
	/* scratch.arena, x_offset, -1.f, z_offset, face_index); */
	/* mesh_source_list_push(scratch.arena, &list, source); */
	/* } */
	/* } */
	/* } */
	/* ASSERT(list.total_vertices_size < MiB(32)); */
	/* Span terrain_data_span = vulkan_buffer_push(pstate->context, pstate->frame_storage_buffer, list.total_vertices_size); */
	/* Arena terrain_data_arena = arena_wrap_span(terrain_data_span); */
	/* MeshSource terrain_src = mesh_source_list_flatten(&terrain_data_arena, &list); */

	if (pstate->state == GAME_STATE_PLAY) {
		Camera *camera = &pstate->game_camera;
		float3 *player_position = &pstate->player.position;

		float spring_length = 16.f;
		float move_speed = 5.f;
		float camera_sensitivity = 0.001f;

		float yaw_delta = input_mouse_dx() * camera_sensitivity;
		float pitch_delta = input_mouse_dy() * camera_sensitivity;

		pstate->target_azimuth = fmodf(pstate->target_azimuth + yaw_delta, C_PIf * 2.f);
		if (pstate->target_azimuth < 0)
			pstate->target_azimuth += C_PIf * 2.f;

		pstate->target_theta = clampf(pstate->target_theta - pitch_delta, C_PIf / 4.f, C_PIf / 2.f);

		float3 to_camera = float3_subtract(camera->position, *player_position);

		float radius = float3_length(to_camera);
		if (radius < EPSILON)
			radius = EPSILON;

		float current_theta = acosf(to_camera.y / radius);
		float current_azimuth = atan2f(to_camera.z, to_camera.x); // [-pi, pi]

		if (current_azimuth < 0)
			current_azimuth += C_PIf * 2.f;

		float difference = pstate->target_azimuth - current_azimuth;
		if (difference > C_PI)
			difference -= C_PI * 2.f;
		if (difference < -C_PI)
			difference += C_PI * 2.f;

		float lerp = 10.0f * dt;

		current_azimuth += lerp * difference;
		current_theta += lerp * (pstate->target_theta - current_theta);

		camera->position = (float3){
			(spring_length * sinf(current_theta) * cosf(current_azimuth)) + player_position->x,
			spring_length * cosf(current_theta),
			(spring_length * sinf(current_theta) * sinf(current_azimuth)) + player_position->z,
		};

		float3 camera_position = camera->position;
		camera_position.y = 0.0f;
		float3 camera_target = camera->target;
		camera_target.y = 0.0f;

		float3 forward, right;

		forward = float3_normalize_safe(float3_subtract(camera_target, camera_position), EPSILON);

		right = float3_cross(forward, camera->up);
		right = float3_normalize_safe(right, EPSILON);

		float3 direction = { 0, 0, 0 };

		// Logic: direction += forward * scalar
		float forward_input = (float)(input_key_down(KEY_CODE_W) - input_key_down(KEY_CODE_S));
		direction = float3_add(direction, float3_scale(forward, forward_input));

		float right_input = (float)(input_key_down(KEY_CODE_D) - input_key_down(KEY_CODE_A));
		direction = float3_add(direction, float3_scale(right, right_input));

		if (float3_length(direction) > EPSILON)
			direction = float3_normalize(direction);

		// Apply movement to player_position (passed by pointer)
		*player_position = float3_add(*player_position, float3_scale(direction, move_speed * dt));

		camera->target.x = player_position->x;
		camera->target.z = player_position->z;
	}

	if (input_key_pressed(KEY_CODE_TAB)) {
		pstate->state = !pstate->state;
		if (pstate->state == GAME_STATE_EDITOR) {
			window_set_cursor_locked(context->display, false);
			pstate->camera = &pstate->editor.camera;
		} else {
			window_set_cursor_locked(context->display, true);
			pstate->camera = &pstate->game_camera;
		}
	}

	float2 window_size = float2_from_uint2(window_size_pixel(context->display));
	if (vulkan_frame_begin(pstate->context, window_size.x, window_size.y)) {
		Camera *camera = pstate->camera;

		Matrix4f projection = float4x4_perspective(deg2radf(camera->fov), window_size.x / window_size.y, 0.01f, 1000.f);
		projection.elements[5] *= -1;
		Matrix4f view = float4x4_lookat(camera->position, camera->target, camera->up);

		typedef struct {
			float4x4 projection, view;
			float4 camera_position;
			float2 viewport;
		} GlobalData;

		size_t global_offset = vulkan_buffer_push(pstate->context, pstate->frame_uniform_buffer, sizeof(GlobalData));
		vulkan_buffer_write(
			pstate->context, pstate->frame_uniform_buffer,
			global_offset + offsetof(GlobalData, projection),
			sizeof_member(GlobalData, projection), &projection);
		vulkan_buffer_write(
			pstate->context, pstate->frame_uniform_buffer,
			global_offset + offsetof(GlobalData, view),
			sizeof_member(GlobalData, view), &view);
		vulkan_buffer_write(
			pstate->context, pstate->frame_uniform_buffer,
			global_offset + offsetof(GlobalData, camera_position),
			sizeof_member(GlobalData, camera_position), &camera->position);
		vulkan_buffer_write(
			pstate->context, pstate->frame_uniform_buffer,
			global_offset + offsetof(GlobalData, viewport),
			sizeof_member(GlobalData, viewport), &window_size);

		size_t light_offset = vulkan_buffer_push(pstate->context, pstate->frame_storage_buffer, sizeof(float4));
		float4 light_position = { 0.0f, 20.0f, -30.0f, 1.0f };
		vulkan_buffer_write(pstate->context, pstate->frame_storage_buffer, light_offset, sizeof(float4), &light_position);

		RhiUniformSet global_set = vulkan_uniformset_push(pstate->context, pstate->pbr_shader, 0);
		vulkan_uniformset_bind_buffer_range(pstate->context, global_set, 0, global_offset, sizeof(GlobalData), pstate->frame_uniform_buffer);
		vulkan_uniformset_bind_buffer_range(pstate->context, global_set, 1, light_offset, sizeof(float4), pstate->frame_storage_buffer);

		DrawListDesc main_pass = {
			.name = S("main_pass"),
			.color_attachments[0] = {
			  .clear.color = { 0.00f, 0.00f, 0.001f, 1.0f },
			  .store = STORE,
			  .load = CLEAR,
			},
			.color_attachment_count = 1,
			.depth_attachment = {
			  .store = DONT_CARE,
			  .load = CLEAR,
			},
			.use_depth = true,
			.msaa_level = 8,
		};

		if (vulkan_drawlist_begin(pstate->context, main_pass)) {
			PipelineDesc pipeline = DEFAULT_PIPELINE;
			/* pipeline.polygon_mode = POLYGON_MODE_LINE; */
			pipeline.cull_mode = CULL_MODE_BACK;
			vulkan_shader_bind(pstate->context, pstate->pbr_shader, pipeline);
			vulkan_uniformset_bind(pstate->context, global_set);

			for (uint32_t node_index = 0; node_index < arena_array_count(pstate->nodes); ++node_index) {
				SceneNode *node = &pstate->nodes[node_index];
				float4x4 transform = node->transform.global;

				uint32_t id = node_index + 1;
				vulkan_push_constants(pstate->context, sizeof(float4x4), sizeof(uint32_t), &id);

				for (uint32_t index = 0; index < node->mesh_count; ++index) {
					Mesh *mesh = &pstate->meshes[node->mesh_index + index];
					uint32_t material_index = pstate->mesh_to_material[indexof(pstate->meshes, mesh)];
					MaterialSource *material = &pstate->materials[material_index];
					uint64x2 span = pstate->material_data[material_index];

					RhiUniformSet group = vulkan_uniformset_push(pstate->context, pstate->unlit_shader, 1);
					vulkan_uniformset_bind_buffer_range(pstate->context, group, 0, span.x, span.y, pstate->scene_uniform_buffer);
					for (uint32_t index = 0; index < 5; ++index) {
						RhiTexture texture = pstate->white;
						if (material->properties[index].as.uint32x1) {
							texture = pstate->images[material->properties[index].as.uint32x1];
						}
						vulkan_uniformset_bind_texture(
							pstate->context,
							group,
							1 + index,
							texture,
							pstate->linear_sampler);
					}

					vulkan_uniformset_bind(pstate->context, group);

					vulkan_push_constants(pstate->context, 0, sizeof(float4x4), &transform);

					vulkan_buffer_bind_vertex(pstate->context, pstate->scene_geometry_buffer, mesh->vertex_offset);
					if (mesh->index_count > 0) {
						vulkan_buffer_bind_index(pstate->context, pstate->scene_geometry_buffer, mesh->index_offset);
						vulkan_renderer_draw_indexed(pstate->context, mesh->index_count);
					} else
						vulkan_renderer_draw(pstate->context, mesh->vertex_count);
				}
			}

			float4x4 player_transform = float4x4_translation(pstate->player.position);
			MaterialSource *defalut_mat = &pstate->materials[0];
			RhiUniformSet group = vulkan_uniformset_push(pstate->context, pstate->unlit_shader, 1);
			vulkan_uniformset_bind_buffer_range(pstate->context, group, 0, 0, sizeof(MaterialParameters), pstate->scene_uniform_buffer);
			for (uint32_t index = 0; index < 5; ++index) {
				RhiTexture texture = pstate->white;
				vulkan_uniformset_bind_texture(
					pstate->context,
					group,
					1 + index,
					texture,
					pstate->linear_sampler);
			}

			vulkan_push_constants(pstate->context, 0, sizeof(player_transform), player_transform.elements);

			Mesh *mesh = &pstate->player.mesh;
			vulkan_buffer_bind_vertex(pstate->context, pstate->scene_geometry_buffer, mesh->vertex_offset);
			if (mesh->index_count > 0) {
				vulkan_buffer_bind_index(pstate->context, pstate->scene_geometry_buffer, mesh->index_offset);
				vulkan_renderer_draw_indexed(pstate->context, mesh->index_count);
			} else
				vulkan_renderer_draw(pstate->context, mesh->vertex_count);
			vulkan_renderer_draw(pstate->context, pstate->player.mesh.vertex_count);

			vulkan_shader_bind(pstate->context, pstate->terrain_shader, pipeline);
			float4x4 transform = float4x4_translation((float3){ 0.0f, 0.0f, 0.0f });

			/* vulkan_push_constants(pstate->context, 0, sizeof(float4x4), &transform); */
			/* // TODO: Add procedural mesh to scene */
			/* vulkan_push_constants(pstate->context, sizeof(float4x4), sizeof(uint32_t), &(uint32){ 0 }); */
			/* RhiUniformSet terrain_info = vulkan_uniformset_push(pstate->context, pstate->terrain_shader, 1); */
			/* vulkan_uniformset_bind_buffer_span(pstate->context, terrain_info, 0, terrain_data_span, pstate->frame_storage_buffer); */
			/* vulkan_uniformset_bind(pstate->context, terrain_info); */

			/* vulkan_renderer_draw(pstate->context, terrain_src.vertex_count); */

			vulkan_drawlist_end(pstate->context);
		}

		if (pstate->state == GAME_STATE_EDITOR)
			editor_update_and_draw(pstate, &pstate->editor, dt);

		vulkan_frame_end(pstate->context);
	}
	vulkan_buffer_reset(pstate->context, pstate->frame_storage_buffer);
	vulkan_buffer_reset(pstate->context, pstate->frame_uniform_buffer);

	/* LOG_INFO("FPS = %.2f", 1 / dt); */

	arena_scratch_end(scratch);

	return (FrameInfo){ 0 };
}

GameInterface game_hookup(void) {
	GameInterface interface = (GameInterface){
		.on_update = update_and_draw,
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

void editor_update_and_draw(PermanentState *pstate, Editor *editor, float dt) {
	Camera *camera = &editor->camera;
	float2 mouse_delta = (float2){ input_mouse_dx(), input_mouse_dy() };

	if (input_mouse_down(MOUSE_BUTTON_MIDDLE))
		window_set_cursor_locked(pstate->display, true);
	else
		window_set_cursor_locked(pstate->display, false);

	// State transition
	if (pstate->selected_node && input_key_pressed(KEY_CODE_G)) {
		editor->grab = !editor->grab;
		if (editor->grab) {
			editor->cached_selection_transform = pstate->nodes[pstate->selected_node - 1].transform;
			editor->grab_mouse_position = float2_from_double2(input_mouse_position());
		}
	}

	if (editor->grab) {
		float2 mouse_position = float2_from_double2(input_mouse_position());
		float2 offset = float2_negate(float2_subtract(mouse_position, editor->grab_mouse_position));

		float3 camera_target_offset = float3_subtract(camera->target, camera->position);
		float3 camera_forward = float3_normalize(camera_target_offset);
		float3 camera_right = float3_cross(camera->up, camera_forward);
		float3 camera_up = float3_cross(camera_right, camera_forward);

		SceneNode *node = &pstate->nodes[pstate->selected_node - 1];

		float2 screen_size = float2_from_uint2(window_size_pixel(pstate->display));

		// TODO: The postion/rotation/scale are in local space, extract/calculate the world position
		float distance = float3_dot(float3_subtract(editor->cached_selection_transform.position, camera->position), camera_forward);
		LOG_INFO("Distance = %.2f", distance);
		float fov_radians = deg2radf(camera->fov);
		float frustum_height = 2 * tanf(fov_radians * 0.5f) * distance;
		float frustum_width = frustum_height * (screen_size.x / screen_size.y);

		float2 units_per_pixel = {
			.x = frustum_width / screen_size.x,
			.y = frustum_height / screen_size.y,
		};

		float3 move_x = float3_scale(camera_right, offset.x * units_per_pixel.x);
		float3 move_y = float3_scale(camera_up, -offset.y * units_per_pixel.y);

		node->transform.position = float3_add(
			float3_add(editor->cached_selection_transform.position, move_x),
			move_y);

		float4x4 translation = float4x4_translation(node->transform.position);
		float4x4 scale = float4x4_scaling(node->transform.scale);
		float4x4 rotation = float4x4_identity();

		node->transform.global = float4x4_multiply(translation, float4x4_multiply(rotation, scale));

		if (input_mouse_pressed(MOUSE_BUTTON_LEFT))
			editor->grab = false;

		if (input_mouse_pressed(MOUSE_BUTTON_RIGHT)) {
			editor->grab = false;

			SceneNode *node = &pstate->nodes[pstate->selected_node - 1];
			Transform3D *transform = &node->transform;

			node->transform = editor->cached_selection_transform;
		}

	} else {
		if (input_mouse_pressed(MOUSE_BUTTON_LEFT)) {
			double2 mouse_position = input_mouse_position();
			uint32_t node_index = 0;
			vulkan_texture_read_pixel(pstate->context, pstate->picker_target, (uint32_t)mouse_position.x, (uint32_t)mouse_position.y, &node_index);
			LOG_INFO("Node index = %d", node_index);

			pstate->selected_node = node_index;
		}

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

			float camera_xz = float2_length((float2){ camera_position.x, camera_position.z });
			float current_theta = atan2f(camera_xz, camera_position.y);
			// tan(theta) = o / a = y / x;
			float current_azimuth = atan2f(camera_position.z, camera_position.x);

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

	// Draw
	DrawListDesc picker_pass = {
		.color_attachments[0] = {
		  .texture = pstate->picker_target,
		  .clear.color = { 0 },
		  .load = CLEAR,
		  .store = STORE,
		},
		.color_attachment_count = 1,
		.use_depth = true,
	};

	if (vulkan_drawlist_begin(pstate->context, picker_pass)) {
		PipelineDesc pipeline = DEFAULT_PIPELINE;
		vulkan_shader_bind(pstate->context, pstate->picker_shader, pipeline);

		uint32_t node_count = arena_array_count(pstate->nodes);
		for (uint32_t node_index = 0; node_index < arena_array_count(pstate->nodes); ++node_index) {
			SceneNode *node = &pstate->nodes[node_index];
			float4x4 transform = node->transform.global;

			uint32_t id = node_index + 1;
			vulkan_push_constants(pstate->context, sizeof(float4x4), sizeof(uint32_t), &id);

			for (uint32_t index = 0; index < node->mesh_count; ++index) {
				Mesh *mesh = &pstate->meshes[node->mesh_index + index];
				vulkan_push_constants(pstate->context, 0, sizeof(float4x4), &transform);

				vulkan_buffer_bind_vertex(pstate->context, pstate->scene_geometry_buffer, mesh->vertex_offset);
				if (mesh->index_count > 0) {
					vulkan_buffer_bind_index(pstate->context, pstate->scene_geometry_buffer, mesh->index_offset);
					vulkan_renderer_draw_indexed(pstate->context, mesh->index_count);
				} else
					vulkan_renderer_draw(pstate->context, mesh->vertex_count);
			}
		}
		vulkan_drawlist_end(pstate->context);
	}

	DrawListDesc debug_lines_pass = {
		.color_attachments[0] = {
		  .load = LOAD,
		  .store = STORE,
		},
		.color_attachment_count = 1,
	};

	if (vulkan_drawlist_begin(pstate->context, debug_lines_pass)) {
		PipelineDesc pipeline = DEFAULT_PIPELINE;
		vulkan_shader_bind(pstate->context, pstate->line_shader, pipeline);

		if (pstate->selected_node) {
			SceneNode *node = &pstate->nodes[pstate->selected_node - 1];

			uint32_t line_segment_count = 12 * node->mesh_count;
			size_t line_segment_size = 2 * sizeof(float4);
			size_t size = line_segment_count * line_segment_size;
			size_t offset = vulkan_buffer_push(pstate->context, pstate->frame_storage_buffer, size);

			for (uint32_t index = 0; index < node->mesh_count; ++index) {
				Mesh *mesh = &pstate->meshes[node->mesh_index + index];
				Interval3D *bounds = &pstate->bounds[node->mesh_index + index];

				float3 bounding_box_size = float3_subtract(bounds->max, bounds->min);
				float3 min = float3_subtract(bounds->min, float3_scale(bounding_box_size, 0.01f));
				float3 max = float3_add(bounds->max, float3_scale(bounding_box_size, 0.01f));

				float thickness = 3.0f;

				float4 points[] = {
					{ min.x, min.y, min.z, thickness },
					{ min.x, max.y, min.z, thickness },

					{ min.x, min.y, max.z, thickness },
					{ min.x, max.y, max.z, thickness },

					{ max.x, min.y, min.z, thickness },
					{ max.x, max.y, min.z, thickness },

					{ max.x, min.y, max.z, thickness },
					{ max.x, max.y, max.z, thickness },

					{ min.x, min.y, min.z, thickness },
					{ min.x, min.y, max.z, thickness },

					{ min.x, min.y, min.z, thickness },
					{ max.x, min.y, min.z, thickness },

					{ max.x, min.y, max.z, thickness },
					{ max.x, min.y, min.z, thickness },

					{ max.x, min.y, max.z, thickness },
					{ min.x, min.y, max.z, thickness },

					{ min.x, max.y, min.z, thickness },
					{ min.x, max.y, max.z, thickness },

					{ min.x, max.y, min.z, thickness },
					{ max.x, max.y, min.z, thickness },

					{ max.x, max.y, max.z, thickness },
					{ max.x, max.y, min.z, thickness },

					{ max.x, max.y, max.z, thickness },
					{ min.x, max.y, max.z, thickness },
				};

				vulkan_buffer_write(
					pstate->context,
					pstate->frame_storage_buffer,
					offset + 12 * line_segment_size * index,
					sizeof(points), points);
			}

			float4x4 transform = node->transform.global;
			vulkan_push_constants(pstate->context, 0, sizeof(float4x4), &transform);

			RhiUniformSet group = vulkan_uniformset_push(pstate->context, pstate->line_shader, 1);
			vulkan_uniformset_bind_buffer_range(pstate->context, group, 0, offset, size, pstate->frame_storage_buffer);
			vulkan_uniformset_bind(pstate->context, group);

			vulkan_renderer_draw(pstate->context, line_segment_count * 2 * 6);
		}

		vulkan_drawlist_end(pstate->context);
	}
}
