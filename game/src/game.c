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
#include "platform/filesystem.h"
#include "renderer/backend/vulkan_api.h"
#include "renderer/r_internal.h"
#include "scene.h"

#include "cgltf.h"
#include <math.h>

#define MAX_ENTITIES 1024

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
	// NOTE: Might want to allow for storing vertex/index buffer separately
	RhiBuffer buffer;

	size_t vertex_offset, vertex_count;
	size_t index_offset, index_count;
} Mesh;

typedef struct {
	RhiBuffer uniform_buffer;
	size_t offset, size;

	RhiTexture textures[16];
	uint32_t texture_count;
} Material;

// ENTITIES
typedef uint32_t Entity;

typedef struct {
	float3 position;
	float3 rotation;
	float3 scale;

	float4x4 local_matrix;
	float4x4 global_matrix;

	uint32_t parent_index;
	uint32_t child_index;
	uint32_t sibling_index;
	bool dirty;
} Transform3;
typedef Transform3 TransformComponent;

typedef struct {
	uint32_t mesh_group_index;
} MeshComponent;

typedef union {
	struct {
		float3 center;
		float3 extent;
	} aabb;
} ColliderComponent;

typedef enum {
	COMPONENT_FLAG_TRANSFORM = 1 << 0,
	COMPONENT_FLAG_MESH = 1 << 1,
	COMPONENT_FLAG_DRAWABLE = COMPONENT_FLAG_TRANSFORM | COMPONENT_FLAG_MESH,
	COMPONENT_FLAG_COLLIDABLE = 1 << 2,
} ComponentFlags;

// EDITOR
typedef enum {
	AXIS_MODE_XYZ,
	AXIS_MODE_XY,
	AXIS_MODE_YZ,
	AXIS_MODE_ZX,
	AXIS_MODE_X,
	AXIS_MODE_Y,
	AXIS_MODE_Z,
} AxisMode;

typedef enum {
	EDITOR_MODE_VIEWING,
	EDITOR_MODE_TRANSFORM,
} EditorMode;

typedef struct {
	uint32_t number_input;

	AxisMode axis_mode;
	bool axis_reverse;

	bool rotating;

	float2 mouse_start_position;
	TransformComponent cached;
} EditorTransformInfo;

typedef struct Editor {
	float sensitivity, pan_speed, zoom_speed;
	Camera camera;

	Entity selected_entity;

	EditorMode mode;
	EditorTransformInfo transform;

	struct {
		TransformComponent cached_transform;
		// TODO: add redo, extend undo/redo for all changes
		/* MeshComponent cached_mesh; */
		Entity entity;
	} undo_steps[32];
	uint32_t undo_count, undo_write_cursor;
} Editor;

typedef struct {
	Arena arena;
	bool initialized;

	VulkanContext *context;
	Window *display;

	RhiSampler linear_sampler;
	RhiTexture white;

	// These are assets too
	RhiShader unlit_shader, pbr_shader, postfx_shader;
	RhiShader screenline_shader, picker_shader;
	RhiShader blit_shader;

	RhiBuffer frame_uniform_buffer;
	RhiBuffer frame_storage_buffer;

	RhiBuffer scene_uniform_buffer;
	RhiBuffer scene_geometry_buffer;

	RhiTexture main_color_targets[2];
	/* RhiTexture main_depth_target; */
	uint32_t previous_target, current_target;
	RhiTexture picker_target;

	GameState state;
	Editor editor;

	Camera game_camera;
	float target_azimuth, target_theta;

	struct {
		RhiTexture *textures;
		Material *materials;
		Mesh *meshes;
		uint32x2 *mesh_groups; // aka model
		uint32_t *mesh_to_material;

		Interval3 *bounds;
	} assets;

	uint32_t entity_count;
	TransformComponent transforms[MAX_ENTITIES];
	MeshComponent meshes[MAX_ENTITIES];
	ColliderComponent colliders[MAX_ENTITIES];
	uint32_t components[MAX_ENTITIES];

	Entity test_collidable_entity;
	Entity player;

	Camera *camera;
} PermanentState;

Editor editor_make(Arena *arena);
void editor_update(PermanentState *state, Editor *editor, float dt);
void editor_draw(PermanentState *state, Editor *editor);

float4x4 to_world(PermanentState *pstate, Transform3 *transform) {
	if (transform->dirty) {
		transform->local_matrix = float4x4_compose(transform->position, transform->rotation, transform->scale);
		if (transform->parent_index)
			transform->global_matrix = float4x4_multiply(pstate->transforms[transform->parent_index].global_matrix, transform->local_matrix);
		else
			transform->global_matrix = transform->local_matrix;

		if (transform->child_index) {
			Transform3 *p = &pstate->transforms[transform->child_index];
			do {
				p->global_matrix = to_world(pstate, p);
				p = &pstate->transforms[p->sibling_index];

			} while (p->sibling_index);
		}

		transform->dirty = false;
	}

	return transform->global_matrix;
}

bool scene_serialize(String path, PermanentState *pstate) {
	File file = filesystem_open(path, FILE_MODE_WRITE);
	if (file.handle == NULL)
		return false;

	file_write_struct(&file, uint32_t, pstate->entity_count);
	file_write_count(&file, pstate->entity_count, pstate->transforms);
	file_write_count(&file, pstate->entity_count, pstate->meshes);
	file_write_count(&file, pstate->entity_count, pstate->components);

	file_close(&file);

	return true;
}

void scene_deserialize(String path, PermanentState *pstate) {
	ArenaTemp scratch = arena_scratch_begin(NULL);
	Span file = filesystem_read(scratch.arena, path);

	pstate->entity_count = *(uint32_t *)file.buffer;
	file.buffer += 4;

	for (uint32_t entity = 0; entity < pstate->entity_count; ++entity) {
		pstate->transforms[entity] = *(TransformComponent *)file.buffer;
		file.buffer += sizeof(TransformComponent);
	}

	for (uint32_t entity = 0; entity < pstate->entity_count; ++entity) {
		pstate->meshes[entity] = *(MeshComponent *)file.buffer;
		file.buffer += sizeof(MeshComponent);
	}

	for (uint32_t entity = 0; entity < pstate->entity_count; ++entity) {
		pstate->components[entity] = *(ComponentFlags *)file.buffer;
		file.buffer += sizeof(ComponentFlags);
	}

	arena_scratch_end(scratch);
}

bool window_resize(EventCode code, void *event, void *receiver) {
	WindowResizeEvent *resize_event = event;
	PermanentState *pstate = receiver;

	vulkan_renderer_on_resize(pstate->context, resize_event->width, resize_event->height);
	vulkan_texture_resize(pstate->context, pstate->picker_target, resize_event->width, resize_event->height);
	for (uint32_t index = 0; index < countof(pstate->main_color_targets); ++index)
		vulkan_texture_resize(pstate->context, pstate->main_color_targets[index], resize_event->width, resize_event->height);

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

		ShaderSource source = { 0 };
		source = importer_load_shader(scratch.arena, S("assets/shaders/unlit.vert.spv"), S("assets/shaders/unlit.frag.spv"));
		pstate->unlit_shader =
			vulkan_shader_make(
				NULL,
				pstate->context,
				source.vertex, source.fragment,
				NULL);
		source = importer_load_shader(scratch.arena, S("assets/shaders/pbr.vert.spv"), S("assets/shaders/pbr.frag.spv"));
		pstate->pbr_shader =
			vulkan_shader_make(
				NULL,
				pstate->context,
				source.vertex, source.fragment,
				NULL);
		source = importer_load_shader(scratch.arena, S("assets/shaders/line.vert.spv"), S("assets/shaders/line.frag.spv"));
		pstate->screenline_shader =
			vulkan_shader_make(
				NULL,
				pstate->context,
				source.vertex, source.fragment,
				NULL);
		source = importer_load_shader(scratch.arena, S("assets/shaders/picker.vert.spv"), S("assets/shaders/picker.frag.spv"));
		pstate->picker_shader =
			vulkan_shader_make(
				NULL,
				pstate->context,
				source.vertex, source.fragment,
				NULL);
		source = importer_load_shader(scratch.arena, S("assets/shaders/postfx.vert.spv"), S("assets/shaders/postfx.frag.spv"));
		pstate->postfx_shader =
			vulkan_shader_make(
				NULL,
				pstate->context,
				source.vertex, source.fragment,
				NULL);
		source = importer_load_shader(scratch.arena, S("assets/shaders/blit.vert.spv"), S("assets/shaders/blit.frag.spv"));
		pstate->blit_shader =
			vulkan_shader_make(
				NULL,
				pstate->context,
				source.vertex, source.fragment,
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
		for (uint32_t index = 0; index < countof(pstate->main_color_targets); ++index) {
			pstate->main_color_targets[index] = vulkan_texture_make(
				pstate->context,
				window_size.x, window_size.y,
				TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8_SRGB,
				TEXTURE_USAGE_SAMPLED | TEXTURE_USAGE_RENDER_TARGET,
				NULL);
		}
		/* pstate->main_depth_target = vulkan_texture_make( */
		/* 	pstate->context, */
		/* 	window_size.x, window_size.y, */
		/* 	TEXTURE_TYPE_2D, TEXTURE_FORMAT_DEPTH, */
		/* 	TEXTURE_USAGE_SAMPLED | TEXTURE_USAGE_RENDER_TARGET, */
		/* 	NULL); */

		// DEFAULTS & INVALID
		pstate->entity_count++;

		// TODO: Import the node transforms & cache shared textures
		SceneSource models[] = {
			importer_load_gltf_scene(scratch.arena, S("assets/models/modular_dungeon/room-large.glb")),
			importer_load_gltf_scene(scratch.arena, S("assets/models/modular_dungeon/room-small.glb")),
			importer_load_gltf_scene(scratch.arena, S("assets/models/modular_dungeon/corridor.glb")),
			importer_load_gltf_scene(scratch.arena, S("assets/models/modular_dungeon/gate-door.glb")),
		};

		// Prep Upload
		RhiTexture *textures = NULL;
		Material *materials = NULL;

		Mesh *meshes = NULL;
		uint32_t *mesh_to_material = NULL;
		Interval3 *bounds = NULL;
		uint32x2 *mesh_groups = NULL;

		// Defaults
		arena_darray_push(scratch.arena, mesh_groups, uint32x2); // 0 == invalid
		arena_darray_push(scratch.arena, textures, RhiTexture); // 0 == default
		Material *default_mat = arena_darray_push(scratch.arena, materials, Material); // 0 == default
		MaterialParameters parameters = {
			.base_color_factor = default_properties[5].as.float32x4,
			.metallic_factor = default_properties[6].as.float32x1,
			.roughness_factor = default_properties[7].as.float32x1,
			.emissive_factor = default_properties[8].as.float32x3
		};
		for (uint32_t texture_index = 0; texture_index < 5; ++texture_index) {
			default_mat->textures[texture_index] = textures[0];
			default_mat->texture_count++;
		}
		size_t size = sizeof(MaterialParameters);
		default_mat->uniform_buffer = pstate->scene_uniform_buffer;
		size_t offset = vulkan_buffer_push(pstate->context, default_mat->uniform_buffer, size, NULL);
		default_mat->offset = offset, default_mat->size = size;
		vulkan_buffer_write_all(pstate->context, default_mat->uniform_buffer, default_mat->offset, default_mat->size, &parameters);

		Arena *geometry_upload_arena = arena_partition(scratch.arena, MiB(32));
		for (uint32_t model_index = 0; model_index < countof(models); ++model_index) {
			SceneSource *model = &models[model_index];

			uint32_t mesh_offset = arena_array_count(meshes);
			uint32_t material_offset = arena_array_count(materials);
			uint32_t texture_offset = arena_array_count(textures);

			for (uint32_t image_index = 0; image_index < model->image_count; ++image_index) {
				ImageSource *src = &model->images[image_index];
				RhiTexture *dst = arena_darray_push(scratch.arena, textures, RhiTexture);

				ASSERT(src->pixels);
				*dst = vulkan_texture_make(
					pstate->context,
					src->width, src->height,
					TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8_SRGB,
					TEXTURE_USAGE_SAMPLED, src->pixels);
			}

			for (uint32_t material_index = 0; material_index < model->material_count; ++material_index) {
				MaterialSource *src = &model->materials[material_index];
				Material *dst = arena_darray_push(scratch.arena, materials, Material);
				dst->uniform_buffer = pstate->scene_uniform_buffer;

				MaterialParameters parameters = {
					.base_color_factor = src->properties[5].as.float32x4,
					.metallic_factor = src->properties[6].as.float32x1,
					.roughness_factor = src->properties[7].as.float32x1,
					.emissive_factor = src->properties[8].as.float32x3
				};

				for (uint32_t texture_index = 0; texture_index < 5; ++texture_index) {
					MaterialProperty *property = &src->properties[texture_index];
					dst->textures[texture_index] = textures[0];
					if (property->as.uint32x1)
						dst->textures[texture_index] = textures[(property->as.uint32x1 - 1) + texture_offset];

					dst->texture_count++;
				}

				size_t size = sizeof(MaterialParameters);
				size_t offset = vulkan_buffer_push(pstate->context, dst->uniform_buffer, size, NULL);
				dst->offset = offset, dst->size = size;
				vulkan_buffer_write_all(pstate->context, dst->uniform_buffer, dst->offset, dst->size, &parameters);
			}

			size_t vertex_offset = geometry_upload_arena->offset;
			size_t index_offset = vertex_offset + model->vertices_size;
			for (uint32_t mesh_index = 0; mesh_index < model->mesh_count; ++mesh_index) {
				MeshSource *src = &model->meshes[mesh_index];
				Mesh *dst = arena_darray_push(scratch.arena, meshes, Mesh);

				size_t vertices_size = src->vertex_size * src->vertex_count;
				size_t indices_size = src->index_size * src->index_count;

				if (vertices_size == 0 && indices_size == 0)
					continue;

				dst->buffer = pstate->scene_geometry_buffer;

				dst->vertex_offset = vertex_offset;
				dst->index_offset = index_offset;
				vertex_offset += vertices_size;
				index_offset += indices_size;

				dst->index_count = src->index_count;
				dst->vertex_count = src->vertex_count;

				uint32_t material_index = model->mesh_to_material[mesh_index];
				material_index -= material_index ? 1 : 0;
				arena_darray_put(scratch.arena, mesh_to_material, uint32_t, material_index + material_offset);
				arena_darray_put(scratch.arena, bounds, Interval3, model->bounding_boxes[mesh_index]);
			}

			arena_darray_put(scratch.arena, mesh_groups, uint32x2, { mesh_offset, model->mesh_count });

			arena_push_copy(geometry_upload_arena, model->vertices, model->vertices_size, 1);
			arena_push_copy(geometry_upload_arena, model->indices, model->indices_size, 1);
		}

		// Add cube asset for player
		{ // This is the code necessary for pushing a single mesh resource
			MeshSource player_src = mesh_source_cube(scratch.arena, 0, 0.5f, 0);
			Interval3 player_bounds = {
				.min = { -0.5f, 0, -0.5f },
				.max = { 0.5f, 1, 0.5f },
			};

			uint32x2 group = { arena_array_count(meshes), 1 };
			arena_darray_put(scratch.arena, mesh_groups, uint32x2, group);

			Mesh *player_mesh = arena_darray_push(scratch.arena, meshes, Mesh);
			player_mesh->buffer = pstate->scene_geometry_buffer;
			player_mesh->vertex_count = player_src.vertex_count;
			player_mesh->vertex_offset = geometry_upload_arena->offset;
			arena_push_copy(geometry_upload_arena, player_src.vertices, player_src.vertex_size * player_src.vertex_count, 1);

			arena_darray_put(scratch.arena, mesh_to_material, uint32_t, 0);
			arena_darray_put(scratch.arena, bounds, Interval3, player_bounds);
		}

		pstate->assets.textures = arena_array_copy(&pstate->arena, textures, RhiTexture);
		pstate->assets.meshes = arena_array_copy(&pstate->arena, meshes, Mesh);
		pstate->assets.materials = arena_array_copy(&pstate->arena, materials, Material);
		pstate->assets.mesh_to_material = arena_array_copy(&pstate->arena, mesh_to_material, uint32_t);
		pstate->assets.bounds = arena_array_copy(&pstate->arena, bounds, Interval3);
		pstate->assets.mesh_groups = arena_array_copy(&pstate->arena, mesh_groups, uint32x2);

		// Upload all geometry once
		vulkan_buffer_push(pstate->context, pstate->scene_geometry_buffer, geometry_upload_arena->offset, geometry_upload_arena->base);

		pstate->game_camera = (Camera){
			.position = { 0.0f, 20, 30 },
			.up = { 0.0f, 1.0f, 0.0f },
			.target = { 0.0f, 0.0f, 0.0f },
			.fov = 45.f,

			.projection = CAMERA_PROJECTION_ORTHOGRAPHIC
		};
		pstate->target_azimuth = C_PIf * 0.5f;
		pstate->target_theta = C_PIf / 3.f;

		// Test entity
		Entity ce = pstate->test_collidable_entity = pstate->entity_count++;
		pstate->transforms[ce] = (TransformComponent){
			.position = { 10.f, 1.0f, -6.0f },
			.scale = FLOAT3_ONE,
			.dirty = true,
		};
		pstate->colliders[ce] = (ColliderComponent){
			.aabb = {
			  .center = FLOAT3_ZERO,
			  .extent = { 1.0f, 2.0f, 4.0f },
			},
		};
		pstate->components[ce] = COMPONENT_FLAG_TRANSFORM | COMPONENT_FLAG_COLLIDABLE;

		pstate->editor = editor_make(&pstate->arena);

		pstate->camera = &pstate->editor.camera;
		pstate->state = GAME_STATE_EDITOR;

		/* if (file_exists(S("assets/scene/test.scene"))) */
		/* 	scene_deserialize(S("assets/scene/test.scene"), pstate); */

		arena_scratch_end(scratch);

		pstate->initialized = true;
	}

	ArenaTemp scratch = arena_scratch_begin(NULL);

	switch (pstate->state) {
		case GAME_STATE_PLAY: {
			Entity player = pstate->player;
			if (player == 0) {
				player = pstate->player = pstate->entity_count++;

				float3 start_position = FLOAT3_ZERO;
				pstate->transforms[player] = (TransformComponent){
					.position = FLOAT3_ZERO,
					.scale = FLOAT3_ONE,
					.dirty = true,
				};
				pstate->meshes[player] = (MeshComponent){
					.mesh_group_index = arena_array_count(pstate->assets.mesh_groups) - 1,
					/* .material_index = 0, */
				};
				pstate->components[player] = COMPONENT_FLAG_DRAWABLE;
			}

			Camera *camera = &pstate->game_camera;
			TransformComponent *transform = &pstate->transforms[pstate->player];

			float3 move = {
				.x = input_key_down(KEY_CODE_D) - input_key_down(KEY_CODE_A),
				.y = 0,
				.z = input_key_down(KEY_CODE_S) - input_key_down(KEY_CODE_W),
			};
			move = float3_normalize_safe(move, EPSILON);
			float player_move_speed = 10.f;
			if (float3_length_squared(move) > 0) {
				float3 old_position = transform->position;
				float3 new_position = float3_add(transform->position, float3_scale(move, player_move_speed * dt));

				float3 ro = old_position;
				float3 rd = float3_normalize_safe(float3_subtract(new_position, old_position), EPSILON);

				Transform3 *collision_transform = &pstate->transforms[pstate->test_collidable_entity];
				ColliderComponent *shape = &pstate->colliders[pstate->test_collidable_entity];

				float3 center = float4x4_transform(to_world(pstate, collision_transform), shape->aabb.center);
				float3 min = float3_subtract(center, float3_add(shape->aabb.extent, float3_fill(0.5f)));
				float3 max = float3_add(center, float3_add(shape->aabb.extent, float3_fill(0.5f)));

				// Collision detection
				float3 plane_left_normal =
					float3_normalize_safe(
						float3_cross(
							float3_subtract((float3){ min.x, min.y, max.z }, min),
							float3_subtract((float3){ min.x, max.y, min.z }, min)),
						EPSILON);
				float ray_normal_relative_movement = float3_dot(plane_left_normal, rd);

				float time_to_collide = 1.0f;
				if (fabsf(ray_normal_relative_movement) > EPSILON) {
					float plane_offset_along_normal = float3_dot((float3){ min.x, min.y, max.z }, plane_left_normal);
					float ray_offset_along_normal = float3_dot(ro, plane_left_normal);
					float ray_to_plane_distance = plane_offset_along_normal - ray_offset_along_normal;
					float ray_to_plane = ray_to_plane_distance / ray_normal_relative_movement;
					float3 ray_plane_collsion_point = float3_add(ro, float3_scale(rd, ray_to_plane));

					if ((ray_plane_collsion_point.x < min.x || ray_plane_collsion_point.x > max.x) ||
						(ray_plane_collsion_point.y < min.y || ray_plane_collsion_point.y > max.y) ||
						(ray_plane_collsion_point.z < min.z || ray_plane_collsion_point.z > max.z)) {
						// no hit
					} else if (ray_to_plane >= 0) {
						time_to_collide = ray_to_plane;
					}

					/* LOG_INFO("Plane normal = %.2f, %.2f, %.2f", plane_left_normal.x, plane_left_normal.y, plane_left_normal.z); */
					/* LOG_INFO("normal relative movement %.2f", normal_relative_movement); */
					/* LOG_INFO("plane offset along normal %.2f", plane_offset_along_normal); */
					/* LOG_INFO("ray offset along normal %.2f", ray_offset_along_normal); */
					/* LOG_INFO("ray to plane distance along normal %.2f", ray_to_plane_distance); */
					LOG_INFO("ray to plane %.2f", ray_to_plane);
					/* float3_print(ray_plane_collsion_point); */
				}

				// Collision resolution
				float3 move_delta = float3_scale(move, player_move_speed * dt);
				if (time_to_collide < (player_move_speed * dt)) {
					LOG_INFO("Should be the last time");
				}
				float actual_t = time_to_collide / (player_move_speed * dt);
				float t = minf(1.0f, actual_t);

				transform->dirty = true;
				transform->position = float3_add(transform->position, float3_scale(move_delta, t - 0.001f));
			}

			camera->target = transform->position;
			camera->position.x = transform->position.x;
			camera->position.z = transform->position.z + 20;
			// .position = { 0.0f, 20, 30 },
		} break;

		case GAME_STATE_EDITOR: {
			editor_update(pstate, &pstate->editor, dt);
		} break;
	}

	if (input_key_pressed(KEY_CODE_TAB)) {
		pstate->state = !pstate->state;
		if (pstate->state == GAME_STATE_EDITOR) {
			window_set_cursor_locked(context->display, false);
			pstate->camera = &pstate->editor.camera;

		} else {
			/* window_set_cursor_locked(context->display, true); */
			pstate->camera = &pstate->game_camera;
		}
	}

	float2 window_size = float2_from_uint2(window_size_pixel(context->display));
	if (vulkan_frame_begin(pstate->context, window_size.x, window_size.y)) {
		Camera *camera = pstate->camera;

		float4x4 projection = float4x4_identity();
		if (camera->projection == CAMERA_PROJECTION_PERSPECTIVE) {
			projection = float4x4_perspective(deg2radf(camera->fov), window_size.x / window_size.y, 0.01f, 1000.f);
		} else if (camera->projection == CAMERA_PROJECTION_ORTHOGRAPHIC) {
			float ortho_size = 96.0f;

			float aspect = window_size.x / window_size.y;
			projection = float4x4_orthographic(
				-window_size.x / ortho_size, window_size.x / ortho_size,
				-window_size.y / ortho_size, window_size.y / ortho_size,
				0.1f, 1000.f);
		}
		projection.elements[5] *= -1;
		float4x4 view = float4x4_lookat(camera->position, camera->target, camera->up);

		typedef struct {
			float4x4 projection, view;
			float4 camera_position;
			float2 viewport;
		} GlobalData;

		size_t global_offset = vulkan_buffer_push(pstate->context, pstate->frame_uniform_buffer, sizeof(GlobalData), NULL);
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

		float4 light_position = { 0.0f, 20.0f, -30.0f, 1.0f };
		size_t light_offset = vulkan_buffer_push(pstate->context, pstate->frame_storage_buffer, sizeof(float4), &light_position);

		RhiUniformSet global_set = vulkan_uniformset_push(pstate->context, pstate->pbr_shader, 0);
		vulkan_uniformset_bind_buffer_range(pstate->context, global_set, 0, global_offset, sizeof(GlobalData), pstate->frame_uniform_buffer);
		vulkan_uniformset_bind_buffer_range(pstate->context, global_set, 1, light_offset, sizeof(float4), pstate->frame_storage_buffer);

		DrawListDesc main_pass = {
			.name = S("main_pass"),
			.color_attachments[0] = {
			  .target = pstate->main_color_targets[pstate->current_target],
			  .clear.color = { 0.00f, 0.00f, 0.001f, 1.0f },
			  .store = STORE,
			  .load = CLEAR,
			},
			.color_attachment_count = 1,
			.use_depth = true,
			.msaa_level = 8,
		};

		if (vulkan_drawlist_begin(pstate->context, main_pass)) {
			PipelineDesc pipeline = DEFAULT_PIPELINE;
			/* pipeline.polygon_mode = POLYGON_MODE_LINE; */
			pipeline.cull_mode = CULL_MODE_BACK;

			// Entities
			vulkan_shader_bind(pstate->context, pstate->pbr_shader, pipeline);
			vulkan_uniformset_bind(pstate->context, global_set);
			for (Entity entity = 0; entity < pstate->entity_count; ++entity) {
				if (FLAG_GET(pstate->components[entity], COMPONENT_FLAG_DRAWABLE) == false)
					continue;
				TransformComponent *transform = &pstate->transforms[entity];
				MeshComponent *mesh_component = &pstate->meshes[entity];

				if (mesh_component->mesh_group_index == 0 || mesh_component->mesh_group_index > arena_array_count(pstate->assets.mesh_groups))
					continue;
				uint32x2 mesh_group = pstate->assets.mesh_groups[mesh_component->mesh_group_index];

				for (uint32_t mesh_index = mesh_group.x; mesh_index < mesh_group.x + mesh_group.y; ++mesh_index) {
					Mesh *mesh = &pstate->assets.meshes[mesh_index];
					uint32_t material_index = pstate->assets.mesh_to_material[mesh_index];
					Material *material = &pstate->assets.materials[material_index];

					RhiUniformSet group = vulkan_uniformset_push(pstate->context, pstate->pbr_shader, 1);
					vulkan_uniformset_bind_buffer_range(pstate->context, group, 0, material->offset, material->size, material->uniform_buffer);
					for (uint32_t texture_index = 0; texture_index < material->texture_count; ++texture_index) {
						RhiTexture texture = material->textures[texture_index];
						if (texture.id == 0)
							texture = pstate->white;

						vulkan_uniformset_bind_texture(
							pstate->context,
							group,
							1 + texture_index,
							texture,
							pstate->linear_sampler);
					}

					vulkan_uniformset_bind(pstate->context, group);

					float4x4 model_matrix = to_world(pstate, transform);
					vulkan_push_constants(pstate->context, 0, sizeof(float4x4), model_matrix.elements);

					vulkan_buffer_bind_vertex(pstate->context, mesh->buffer, mesh->vertex_offset);
					if (mesh->index_count > 0) {
						vulkan_buffer_bind_index(pstate->context, mesh->buffer, mesh->index_offset);
						vulkan_renderer_draw_indexed(pstate->context, mesh->index_count);
					} else
						vulkan_renderer_draw(pstate->context, mesh->vertex_count);
				}
			}

			// Collision shapes
			vulkan_shader_bind(pstate->context, pstate->screenline_shader, pipeline);
			for (Entity entity = 0; entity < pstate->entity_count; ++entity) {
				if (FLAG_GET(pstate->components[entity], COMPONENT_FLAG_TRANSFORM) == false ||
					FLAG_GET(pstate->components[entity], COMPONENT_FLAG_COLLIDABLE) == false)
					continue;
				TransformComponent *transform = &pstate->transforms[entity];
				ColliderComponent *shape = &pstate->colliders[entity];

				float3 min = float3_subtract(shape->aabb.center, shape->aabb.extent);
				float3 max = float3_add(shape->aabb.center, shape->aabb.extent);

				float thickness = 1.5f;

				float4 outline[] = {
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

				size_t size = sizeof(outline);

				RhiBuffer buffer = pstate->frame_storage_buffer;
				size_t storage_offset = vulkan_buffer_push(pstate->context, buffer, size, NULL);
				size_t uniform_offset = vulkan_buffer_push(pstate->context, pstate->frame_uniform_buffer, sizeof(float4), &(float4){ 1.0f, 0.2f, 0.1f, 1.0f });

				vulkan_buffer_write(
					pstate->context,
					pstate->frame_storage_buffer,
					storage_offset,
					size, outline);

				float4x4 model_matrix = to_world(pstate, transform);
				vulkan_push_constants(pstate->context, 0, sizeof(float4x4), model_matrix.elements);

				RhiUniformSet group = vulkan_uniformset_push(pstate->context, pstate->screenline_shader, 1);
				vulkan_uniformset_bind_buffer_range(pstate->context, group, 0, storage_offset, size, buffer);
				vulkan_uniformset_bind_buffer_range(pstate->context, group, 1, uniform_offset, sizeof(float4), pstate->frame_uniform_buffer);
				vulkan_uniformset_bind(pstate->context, group);

				vulkan_renderer_draw(pstate->context, (sizeof(outline) / sizeof(float4)) * 6);
			}

			vulkan_drawlist_end(pstate->context);
		}

		pstate->previous_target = pstate->current_target;
		pstate->current_target = (pstate->current_target + 1) % countof(pstate->main_color_targets);

		DrawListDesc postfx = {
			.color_attachments[0] = {
			  .target = pstate->main_color_targets[pstate->current_target],
			  .clear.color = { 1.0f, 0.0f, 1.0f, 1.0f },
			  .load = CLEAR,
			  .store = STORE,
			},
			.color_attachment_count = 1,
		};

		vulkan_texture_prepare_sample(pstate->context, pstate->main_color_targets[pstate->previous_target]);
		if (vulkan_drawlist_begin(pstate->context, postfx)) {
			PipelineDesc pipeline = DEFAULT_PIPELINE;
			vulkan_shader_bind(pstate->context, pstate->postfx_shader, pipeline);

			RhiUniformSet set = vulkan_uniformset_push(pstate->context, pstate->postfx_shader, 0);
			vulkan_uniformset_bind_texture(pstate->context, set, 0, pstate->main_color_targets[pstate->previous_target], pstate->linear_sampler);
			vulkan_uniformset_bind(pstate->context, set);

			vulkan_renderer_draw(pstate->context, 6);

			vulkan_drawlist_end(pstate->context);
		}

		if (pstate->state == GAME_STATE_EDITOR)
			editor_draw(pstate, &pstate->editor);

		DrawListDesc present_pass = {
			.color_attachments[0] = {
			  .clear.color = { 1.0f, 0.0f, 1.0f, 1.0f },
			  .load = CLEAR,
			  .store = STORE,
			},
			.color_attachment_count = 1,
		};

		vulkan_texture_prepare_sample(pstate->context, pstate->main_color_targets[pstate->previous_target]);
		if (vulkan_drawlist_begin(pstate->context, present_pass)) {
			PipelineDesc pipeline = {
				.cull_mode = CULL_MODE_BACK,
				.front_face = FRONT_FACE_COUNTER_CLOCKWISE,
				.polygon_mode = POLYGON_MODE_FILL,
			};
			vulkan_shader_bind(pstate->context, pstate->blit_shader, pipeline);

			RhiUniformSet set = vulkan_uniformset_push(pstate->context, pstate->blit_shader, 0);
			vulkan_uniformset_bind_texture(pstate->context, set, 0, pstate->main_color_targets[pstate->previous_target], pstate->linear_sampler);
			vulkan_uniformset_bind(pstate->context, set);

			vulkan_renderer_draw(pstate->context, 6);

			vulkan_drawlist_end(pstate->context);
		}

		pstate->previous_target = 0;
		pstate->current_target = 0;

		vulkan_frame_end(pstate->context);
	}

	vulkan_buffer_reset(pstate->context, pstate->frame_storage_buffer);
	vulkan_buffer_reset(pstate->context, pstate->frame_uniform_buffer);
	pstate->current_target = pstate->previous_target = 0;

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

Editor editor_make(Arena *arena) {
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
		},
		.undo_write_cursor = 0,
	};

	return result;
}
void editor_update(PermanentState *pstate, Editor *editor, float dt) {
	Camera *camera = &editor->camera;
	float2 mouse_delta = (float2){ input_mouse_dx(), input_mouse_dy() };

	if (input_mouse_down(MOUSE_BUTTON_MIDDLE))
		window_set_cursor_locked(pstate->display, true);
	else
		window_set_cursor_locked(pstate->display, false);

	bool entity_updated = false;
	if (editor->selected_entity) {
		bool is_rotating = false;
		if (input_key_pressed(KEY_CODE_G) || (is_rotating = input_key_pressed(KEY_CODE_R))) {
			editor->mode = editor->mode == EDITOR_MODE_TRANSFORM ? EDITOR_MODE_VIEWING : EDITOR_MODE_TRANSFORM;
			if (editor->mode == EDITOR_MODE_TRANSFORM) {
				editor->transform.rotating = is_rotating;
				editor->transform.cached = pstate->transforms[editor->selected_entity];
				editor->transform.mouse_start_position = float2_from_double2(input_mouse_position());
			}
		}

		float3 position = pstate->transforms[editor->selected_entity].position;
		/* LOG_INFO("EntityPosition = %.2f, %.2f, %.2f", position.x, position.y, position.z); */

		if (editor->mode == EDITOR_MODE_TRANSFORM) {
			bool exclude = input_key_down(KEY_CODE_LEFTCTRL);

			bool x_axis = input_key_pressed(KEY_CODE_X);
			bool y_axis = input_key_pressed(KEY_CODE_Y);
			bool z_axis = input_key_pressed(KEY_CODE_Z);

			if (exclude == false) {
				if (x_axis)
					editor->transform.axis_mode = AXIS_MODE_X;
				if (y_axis)
					editor->transform.axis_mode = AXIS_MODE_Y;
				if (z_axis)
					editor->transform.axis_mode = AXIS_MODE_Z;
			}
		}

		uint32_t group_count = arena_array_count(pstate->assets.mesh_groups);
		MeshComponent *add_entity_mesh = &pstate->meshes[editor->selected_entity];
		if (input_key_pressed(KEY_CODE_RIGHTBRACKET)) {
			add_entity_mesh->mesh_group_index = (add_entity_mesh->mesh_group_index % (group_count - 1)) + 1;
		}
		if (input_key_pressed(KEY_CODE_LEFTBRACKET)) {
			add_entity_mesh->mesh_group_index = add_entity_mesh->mesh_group_index > 1
												  ? add_entity_mesh->mesh_group_index - 1
												  : group_count - 1;
		}

		if (input_key_down(KEY_CODE_LEFTSHIFT) && input_key_pressed(KEY_CODE_D)) {
			TransformComponent transform_selected = pstate->transforms[editor->selected_entity];
			MeshComponent mesh_selected = pstate->meshes[editor->selected_entity];

			float3 place_position = transform_selected.position;

			Entity entity = pstate->entity_count++;
			ASSERT(entity < MAX_ENTITIES);
			pstate->transforms[entity] = (TransformComponent){
				.position = transform_selected.position,
				.scale = transform_selected.scale,
				.rotation = transform_selected.rotation,
				.dirty = true,
			};
			pstate->meshes[entity] = (MeshComponent){
				.mesh_group_index = mesh_selected.mesh_group_index,
				/* .material_index = mesh_selected.material_index */
			};

			pstate->components[entity] = COMPONENT_FLAG_DRAWABLE;
			editor->selected_entity = entity;
		}

		// TODO: Make undo-able
		if (input_key_pressed(KEY_CODE_DELETE)) {
			if (editor->selected_entity != pstate->entity_count - 1) {
				Entity last = pstate->entity_count - 1;
				Entity target = editor->selected_entity;

				memory_copy_struct(&pstate->transforms[target], &pstate->transforms[last]);
				memory_copy_struct(&pstate->meshes[target], &pstate->meshes[last]);
				memory_copy_struct(&pstate->components[target], &pstate->components[last]);

				memory_zero_struct(pstate->transforms[last]);
				memory_zero_struct(pstate->meshes[last]);
				memory_zero_struct(pstate->components[last]);
			}

			pstate->entity_count--;
			editor->selected_entity = 0;
		}
	}

	if (input_key_down(KEY_CODE_LEFTCTRL) && input_key_pressed(KEY_CODE_S)) {
		if (pstate->entity_count > 0) {
			String path = S("assets/scene/test.scene");
			filesystem_make_directory(stringpath_directory(path));

			if (scene_serialize(path, pstate))
				LOG_INFO("Serialized scene to '%.*s'", SARG(path));
		}
	}

	if (input_key_down(KEY_CODE_LEFTSHIFT) && input_key_pressed(KEY_CODE_A)) {
		float3 place_position = { 0 };

		Entity entity = pstate->entity_count++;
		ASSERT(entity < MAX_ENTITIES);
		pstate->transforms[entity] = (TransformComponent){
			.position = place_position,
			.scale = FLOAT3_ONE,
			.dirty = true,
		};
		pstate->meshes[entity] = (MeshComponent){
			.mesh_group_index = 1,
		};

		pstate->components[entity] = COMPONENT_FLAG_DRAWABLE;
		editor->selected_entity = entity;
	}

	float2 screen_size = float2_from_uint2(window_size_pixel(pstate->display));
	float2 mouse_position = float2_from_double2(input_mouse_position());
	float2 mouse_offset = float2_negate(float2_subtract(mouse_position, editor->transform.mouse_start_position));

	float3 camera_target_offset = float3_subtract(camera->target, camera->position);
	float3 camera_forward = float3_normalize(camera_target_offset);

	float3 camera_right = float3_cross(camera->up, camera_forward);
	float3 camera_up = float3_cross(camera_right, camera_forward);

	switch (editor->mode) {
		case EDITOR_MODE_VIEWING: {
			if (input_mouse_pressed(MOUSE_BUTTON_LEFT)) {
				double2 mouse_position = input_mouse_position();
				uint32_t entity = 0;
				vulkan_texture_read_pixel(pstate->context, pstate->picker_target, (uint32_t)mouse_position.x, (uint32_t)mouse_position.y, &entity);
				LOG_INFO("Entity = %d", entity);

				editor->selected_entity = entity;
			}

			if (input_mouse_down(MOUSE_BUTTON_MIDDLE) && input_key_down(KEY_CODE_LEFTSHIFT)) {
				float2 shift = float2_scale(mouse_delta, editor->pan_speed);
				shift.y *= -1;

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

			if (editor->undo_count &&
				input_key_down(KEY_CODE_LEFTCTRL) && input_key_pressed(KEY_CODE_Z)) {
				int32_t last_step = editor->undo_write_cursor - 1;
				if (last_step < 0)
					last_step = countof(editor->undo_steps) - 1;

				Entity entity = editor->undo_steps[last_step].entity;
				TransformComponent *transform = &pstate->transforms[editor->undo_steps[last_step].entity];
				pstate->transforms[entity] = editor->undo_steps[last_step].cached_transform;
				/* pstate->meshes[entity] = editor->undo_steps[last_step].cached_mesh; */

				editor->undo_write_cursor = last_step;
				editor->undo_count--;
			}
		} break;
		case EDITOR_MODE_TRANSFORM: {
			TransformComponent *transform = &pstate->transforms[editor->selected_entity];
			float distance = float3_dot(float3_subtract(editor->transform.cached.position, camera->position), camera_forward);
			float fov_radians = deg2radf(camera->fov);
			float frustum_height = 2 * tanf(fov_radians * 0.5f) * distance;
			float frustum_width = frustum_height * (screen_size.x / screen_size.y);

			float2 units_per_pixel = {
				.x = frustum_width / screen_size.x,
				.y = frustum_height / screen_size.y,
			};

			for (uint32_t key = KEY_CODE_0; key < KEY_CODE_9 + 1; ++key) {
				if (input_key_pressed(key)) {
					if (editor->transform.number_input == 0)
						*transform = editor->transform.cached;
					editor->transform.number_input *= 10;
					editor->transform.number_input += key - KEY_CODE_0;
				}
			}
			if (input_key_pressed(KEY_CODE_MINUS))
				editor->transform.axis_reverse = true;

			if (editor->transform.rotating && editor->transform.axis_mode) {
				// Angle 0 is the angle from the
				if (input_key_pressed(KEY_CODE_BACKSPACE))
					editor->transform.number_input /= 10;

				float3 rotation = { 0 };
				float scale = deg2radf(editor->transform.number_input) * (editor->transform.axis_reverse ? -1.f : 1.f);
				if (editor->transform.axis_mode == AXIS_MODE_X)
					rotation = float3_scale(FLOAT3_X, scale);
				else if (editor->transform.axis_mode == AXIS_MODE_Z)
					rotation = float3_scale(FLOAT3_Z, scale);
				else if (editor->transform.axis_mode == AXIS_MODE_Y)
					rotation = float3_scale(FLOAT3_Y, scale);
				transform->rotation = float3_add(editor->transform.cached.rotation, rotation);

			} else {
				if (editor->transform.axis_mode == AXIS_MODE_XYZ && editor->transform.number_input == 0) {
					// TODO: The postion/rotation/scale are in local space, extract/calculate the world position
					float3 move_x = float3_scale(camera_right, mouse_offset.x * units_per_pixel.x);
					float3 move_y = float3_scale(camera_up, -mouse_offset.y * units_per_pixel.y);

					transform->position = float3_add(
						float3_add(editor->transform.cached.position, move_x),
						move_y);
				} else if (editor->transform.axis_mode && editor->transform.number_input == 0) {
					// TODO: Move on camera view's relation to the global axis

					float3 axis = { 0 };
					if (editor->transform.axis_mode == AXIS_MODE_X)
						axis = FLOAT3_X;
					else if (editor->transform.axis_mode == AXIS_MODE_Z)
						axis = FLOAT3_Z;
					else if (editor->transform.axis_mode == AXIS_MODE_Y)
						axis = FLOAT3_Y;

					float2 sign = {
						float3_dot(axis, camera_right) < 0.0f ? -1.0f : 1.0f,
						float3_dot(axis, camera_up) < 0.0f ? 1.0f : -1.0f
					};
					float3 move_x = float3_scale(axis, sign.x * mouse_offset.x * units_per_pixel.x);
					float3 move_y = float3_scale(axis, sign.y * mouse_offset.y * units_per_pixel.y);

					transform->position = float3_add(
						float3_add(editor->transform.cached.position, move_x),
						move_y);
				} else if (editor->transform.number_input || editor->transform.axis_reverse) {
					if (input_key_pressed(KEY_CODE_BACKSPACE))
						editor->transform.number_input /= 10;

					float3 move = { 0 };
					float scale = editor->transform.number_input * (editor->transform.axis_reverse ? -1.f : 1.f);
					if (editor->transform.axis_mode == AXIS_MODE_X)
						move = float3_scale(FLOAT3_X, scale);
					else if (editor->transform.axis_mode == AXIS_MODE_Z)
						move = float3_scale(FLOAT3_Z, scale);
					else if (editor->transform.axis_mode == AXIS_MODE_Y)
						move = float3_scale(FLOAT3_Y, scale);
					transform->position = float3_add(editor->transform.cached.position, move);
				}
			}

			if (input_key_down(KEY_CODE_LEFTCTRL)) {
				transform->position.x = (int32_t)transform->position.x;
				transform->position.y = (int32_t)transform->position.y;
				transform->position.z = (int32_t)transform->position.z;
			}
			transform->dirty = true;

			if (input_mouse_pressed(MOUSE_BUTTON_LEFT) || input_key_pressed(KEY_CODE_ENTER)) {
				editor->undo_steps[editor->undo_write_cursor].entity = editor->selected_entity;
				editor->undo_steps[editor->undo_write_cursor].cached_transform = editor->transform.cached;

				editor->undo_write_cursor = (editor->undo_write_cursor + 1) % countof(editor->undo_steps);
				editor->undo_count++;
				editor->undo_count = MIN(countof(editor->undo_steps), editor->undo_count);
			}
			if (input_mouse_pressed(MOUSE_BUTTON_RIGHT) || input_key_pressed(KEY_CODE_ESCAPE))
				*transform = editor->transform.cached;
		} break;
	}

	if (input_mouse_pressed(MOUSE_BUTTON_LEFT) ||
		input_key_pressed(KEY_CODE_ENTER) ||
		input_mouse_pressed(MOUSE_BUTTON_RIGHT) ||
		input_key_pressed(KEY_CODE_ESCAPE)

	) {
		editor->transform = (EditorTransformInfo){ 0 };
		editor->mode = EDITOR_MODE_VIEWING;
	}
}
void editor_draw(PermanentState *pstate, Editor *editor) {
	Camera *camera = pstate->camera;

	float2 window_size = float2_from_uint2(window_size_pixel(pstate->display));
	float4x4 projection = float4x4_perspective(deg2radf(camera->fov), window_size.x / window_size.y, 0.01f, 1000.f);
	projection.elements[5] *= -1;
	float4x4 view = float4x4_lookat(camera->position, camera->target, camera->up);

	typedef struct {
		float4x4 projection, view;
		float4 camera_position;
		float2 viewport;
	} GlobalData;

	size_t global_offset = vulkan_buffer_push(pstate->context, pstate->frame_uniform_buffer, sizeof(GlobalData), NULL);
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

	float4 light_position = { 0.0f, 20.0f, -30.0f, 1.0f };
	size_t light_offset = vulkan_buffer_push(pstate->context, pstate->frame_storage_buffer, sizeof(float4), &light_position);

	RhiUniformSet global_set = vulkan_uniformset_push(pstate->context, pstate->screenline_shader, 0);
	vulkan_uniformset_bind_buffer_range(pstate->context, global_set, 0, global_offset, sizeof(GlobalData), pstate->frame_uniform_buffer);
	vulkan_uniformset_bind_buffer_range(pstate->context, global_set, 1, light_offset, sizeof(float4), pstate->frame_storage_buffer);

	DrawListDesc picker_pass = {
		.color_attachments[0] = {
		  .target = pstate->picker_target,
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
		vulkan_uniformset_bind(pstate->context, global_set);

		for (Entity entity = 0; entity < pstate->entity_count; ++entity) {
			if (FLAG_GET(pstate->components[entity], COMPONENT_FLAG_DRAWABLE) == false)
				continue;

			TransformComponent *transform = &pstate->transforms[entity];
			MeshComponent *mesh_component = &pstate->meshes[entity];

			if (mesh_component->mesh_group_index == 0 || mesh_component->mesh_group_index > arena_array_count(pstate->assets.mesh_groups))
				continue;
			uint32x2 mesh_group = pstate->assets.mesh_groups[mesh_component->mesh_group_index];

			vulkan_push_constants(pstate->context, sizeof(float4x4), sizeof(uint32_t), &entity);

			for (uint32_t mesh_index = mesh_group.x; mesh_index < mesh_group.x + mesh_group.y; ++mesh_index) {
				Mesh *mesh = &pstate->assets.meshes[mesh_index];
				float4x4 model_matrix = to_world(pstate, transform);
				vulkan_push_constants(pstate->context, 0, sizeof(float4x4), model_matrix.elements);

				vulkan_buffer_bind_vertex(pstate->context, mesh->buffer, mesh->vertex_offset);
				if (mesh->index_count > 0) {
					vulkan_buffer_bind_index(pstate->context, mesh->buffer, mesh->index_offset);
					vulkan_renderer_draw_indexed(pstate->context, mesh->index_count);
				} else
					vulkan_renderer_draw(pstate->context, mesh->vertex_count);
			}
		}
		vulkan_drawlist_end(pstate->context);
	}

	DrawListDesc debug_lines_pass = {
		.color_attachments[0] = {
		  .target = pstate->main_color_targets[pstate->current_target],
		  .load = LOAD,
		  .store = STORE,
		},
		.color_attachment_count = 1,
	};

	if (vulkan_drawlist_begin(pstate->context, debug_lines_pass)) {
		PipelineDesc pipeline = DEFAULT_PIPELINE;
		vulkan_shader_bind(pstate->context, pstate->screenline_shader, pipeline);

		if (editor->selected_entity) {
			TransformComponent *transform = &pstate->transforms[editor->selected_entity];
			MeshComponent *mesh_component = &pstate->meshes[editor->selected_entity];

			ASSERT(mesh_component->mesh_group_index || mesh_component->mesh_group_index < arena_array_count(pstate->assets.mesh_groups));
			uint32x2 mesh_group = pstate->assets.mesh_groups[mesh_component->mesh_group_index];

			uint32_t line_segment_count = 24 * mesh_group.y;
			size_t line_segment_size = 2 * sizeof(float4);
			size_t size = line_segment_count * line_segment_size;

			RhiBuffer buffer = pstate->frame_storage_buffer;
			size_t offset = vulkan_buffer_push(pstate->context, buffer, size, NULL);

			for (uint32_t mesh_index = mesh_group.x, index = 0; mesh_index < mesh_group.x + mesh_group.y; ++mesh_index, ++index) {
				Mesh *mesh = &pstate->assets.meshes[mesh_index];
				Interval3 *bounds = &pstate->assets.bounds[mesh_index];

				float3 bounding_box_size = float3_subtract(bounds->max, bounds->min);
				float3 min = bounds->min;
				float3 max = bounds->max;

				float thickness = 1.5f;
				float pick_length = minf(bounding_box_size.z, minf(bounding_box_size.x, bounding_box_size.y)) * 0.25f;

				float4 selection[] = {
					// 0
					{ min.x, min.y, min.z, thickness },
					{ min.x + pick_length, min.y, min.z, thickness },

					{ min.x, min.y, min.z, thickness },
					{ min.x, min.y + pick_length, min.z, thickness },

					{ min.x, min.y, min.z, thickness },
					{ min.x, min.y, min.z + pick_length, thickness },

					// 1
					{ max.x, min.y, min.z, thickness },
					{ max.x - pick_length, min.y, min.z, thickness },

					{ max.x, min.y, min.z, thickness },
					{ max.x, min.y + pick_length, min.z, thickness },

					{ max.x, min.y, min.z, thickness },
					{ max.x, min.y, min.z + pick_length, thickness },

					// 2
					{ min.x, max.y, min.z, thickness },
					{ min.x + pick_length, max.y, min.z, thickness },

					{ min.x, max.y, min.z, thickness },
					{ min.x, max.y - pick_length, min.z, thickness },

					{ min.x, max.y, min.z, thickness },
					{ min.x, max.y, min.z + pick_length, thickness },

					// 3
					{ max.x, max.y, min.z, thickness },
					{ max.x - pick_length, max.y, min.z, thickness },

					{ max.x, max.y, min.z, thickness },
					{ max.x, max.y - pick_length, min.z, thickness },

					{ max.x, max.y, min.z, thickness },
					{ max.x, max.y, min.z + pick_length, thickness },

					// 4
					{ min.x, min.y, max.z, thickness },
					{ min.x + pick_length, min.y, max.z, thickness },

					{ min.x, min.y, max.z, thickness },
					{ min.x, min.y + pick_length, max.z, thickness },

					{ min.x, min.y, max.z, thickness },
					{ min.x, min.y, max.z - pick_length, thickness },

					// 5
					{ max.x, min.y, max.z, thickness },
					{ max.x - pick_length, min.y, max.z, thickness },

					{ max.x, min.y, max.z, thickness },
					{ max.x, min.y + pick_length, max.z, thickness },

					{ max.x, min.y, max.z, thickness },
					{ max.x, min.y, max.z - pick_length, thickness },

					// 6
					{ min.x, max.y, max.z, thickness },
					{ min.x + pick_length, max.y, max.z, thickness },

					{ min.x, max.y, max.z, thickness },
					{ min.x, max.y - pick_length, max.z, thickness },

					{ min.x, max.y, max.z, thickness },
					{ min.x, max.y, max.z - pick_length, thickness },

					// 7
					{ max.x, max.y, max.z, thickness },
					{ max.x - pick_length, max.y, max.z, thickness },

					{ max.x, max.y, max.z, thickness },
					{ max.x, max.y - pick_length, max.z, thickness },

					{ max.x, max.y, max.z, thickness },
					{ max.x, max.y, max.z - pick_length, thickness },
				};

				float4 outline[] = {
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

				size_t point_array_size = sizeof(selection);
				void *point_array = selection;

				vulkan_buffer_write(
					pstate->context,
					pstate->frame_storage_buffer,
					offset + point_array_size * index,
					point_array_size, point_array);
			}

			float4x4 model_matrix = to_world(pstate, transform);
			vulkan_push_constants(pstate->context, 0, sizeof(float4x4), model_matrix.elements);

			size_t uniform_offset = vulkan_buffer_push(pstate->context, pstate->frame_uniform_buffer, sizeof(float4), &(float4){ 1.0f, 1.0f, 1.0f, 1.0f });

			RhiUniformSet group = vulkan_uniformset_push(pstate->context, pstate->screenline_shader, 1);
			vulkan_uniformset_bind_buffer_range(pstate->context, group, 0, offset, size, buffer);
			vulkan_uniformset_bind_buffer_range(pstate->context, group, 1, uniform_offset, sizeof(float4), pstate->frame_uniform_buffer);
			vulkan_uniformset_bind(pstate->context, group);

			vulkan_renderer_draw(pstate->context, line_segment_count * 2 * 6);
		}

		vulkan_drawlist_end(pstate->context);
	}

	pstate->previous_target = pstate->current_target;
	pstate->current_target = (pstate->current_target + 1) % countof(pstate->main_color_targets);
}
