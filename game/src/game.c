#include "assets.h"
#include "assets/asset_types.h"
#include "assets/importer.h"
#include "assets/json_parser.h"
#include "assets/mesh_source.h"

#include "common.h"
#include "core/arena.h"
#include "core/cmath.h"
#include "core/debug.h"
#include "core/identifiers.h"
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

#include "ecs.h"
#include "cgltf.h"

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
	TRS_MODE_NONE,
	TRS_MODE_TRANSLATION,
	TRS_MODE_ROTATION,
	TRS_MODE_SCALING
} TRSMode;

typedef enum {
	EDITOR_MODE_VIEWING,
	EDITOR_MODE_TRANSFORM,
} EditorMode;

typedef struct {
	uint32_t number_input;

	AxisMode axis;
	bool axis_reverse;

	TRSMode mode;

	float2 mouse_start_position;
	TransformComponent cached;
} EditorTransformInfo;

typedef struct Editor {
	float sensitivity, pan_speed, zoom_speed;
	Camera camera;

	Entity active_entity;
	Entity selected_entities[64];
	uint32_t selected_entity_count;

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
	RhiShader unlit_shader, phong_shader, postfx_shader;
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

	float3 game_camera_start_offset;
	Camera game_camera;

	struct {
		RhiShader *shaders;
		RhiTexture *textures;
		Material *materials;
		Mesh *meshes;
		uint32_t *mesh_to_material;

		uint32x2 *mesh_groups; // aka model
		Interval3 *mesh_group_bounds; // per model
	} assets;

	ECS *world;
	Entity player, selection;

	bool debug_draw_collisions;

	Camera *camera;
} PermanentState;

Editor editor_make(Arena *arena);
void editor_update(PermanentState *state, Editor *editor, float dt);
void editor_draw(PermanentState *state, Editor *editor);


void calculate_transforms_recursive(ECS *world, Entity entity, float4x4 parent_global) {
	TransformComponent *t = ecs_find(world, entity, TransformComponent);

	// Calculate local (if dirty) and global
	t->local_matrix = float4x4_compose(t->position, t->rotation, t->scale);
	t->global_matrix = float4x4_multiply(parent_global, t->local_matrix);
	t->dirty = false;

	// Recurse down children if this entity has a hierarchy
	HierarchyComponent *node = ecs_find(world, entity, HierarchyComponent);
	if (node && node->first_child) {
		Entity child = node->first_child;
		while (child) {
			calculate_transforms_recursive(world, child, t->global_matrix);
			child = ecs_find(world, child, HierarchyComponent)->next_sibling;
		}
	}
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

		AssetStore store = asset_store_make(scratch.arena);
		asset_store_track_directory(&store, S("assets/"));

		UUID unlit_generated = uuid_generate();
		UUID unlit = asset_store_find(&store, ASSET_TYPE_SHADER, S("unlit.glsl"));

		ShaderSource source = { 0 };
		source = importer_load_shader(scratch.arena, S("assets/shaders/unlit.vert.spv"), S("assets/shaders/unlit.frag.spv"));
		pstate->unlit_shader =
			vulkan_shader_make(
				NULL,
				pstate->context,
				source.vertex, source.fragment,
				NULL);
		source = importer_load_shader(scratch.arena, S("assets/shaders/phong.vert.spv"), S("assets/shaders/phong.frag.spv"));
		pstate->phong_shader =
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
				TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA16F,
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
		pstate->world = ecs_make(&pstate->arena);

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
		Interval3 *mesh_group_bounds = NULL;
		uint32x2 *mesh_groups = NULL;

		// Defaults
		arena_darray_push(scratch.arena, mesh_groups, uint32x2); // 0 == invalid
		arena_darray_push(scratch.arena, mesh_group_bounds, Interval3); // 0 == invalid
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
			Interval3 largest = {
				.min = float3_fill(FLOAT_MAX),
				.max = float3_fill(FLOAT_MIN),
			};
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

				Interval3 mesh_bounding_box = model->bounding_boxes[mesh_index];
				largest.min = float3_min(largest.min, mesh_bounding_box.min);
				largest.max = float3_max(largest.max, mesh_bounding_box.max);
			}

			arena_darray_put(scratch.arena, mesh_groups, uint32x2, { mesh_offset, model->mesh_count });
			arena_darray_put(scratch.arena, mesh_group_bounds, Interval3, largest);

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
			arena_darray_put(scratch.arena, mesh_group_bounds, Interval3, player_bounds);
		}

		pstate->assets.textures = arena_array_copy(&pstate->arena, textures, RhiTexture);
		pstate->assets.meshes = arena_array_copy(&pstate->arena, meshes, Mesh);
		pstate->assets.materials = arena_array_copy(&pstate->arena, materials, Material);
		pstate->assets.mesh_to_material = arena_array_copy(&pstate->arena, mesh_to_material, uint32_t);
		pstate->assets.mesh_group_bounds = arena_array_copy(&pstate->arena, mesh_group_bounds, Interval3);
		pstate->assets.mesh_groups = arena_array_copy(&pstate->arena, mesh_groups, uint32x2);

		// Upload all geometry once
		vulkan_buffer_push(pstate->context, pstate->scene_geometry_buffer, geometry_upload_arena->offset, geometry_upload_arena->base);

		arena_scratch_end(scratch);

		float yaw = deg2radf(54.736f);
		float pitch = deg2radf(45.f);
		float arm_length = 40.f;
		pstate->game_camera = (Camera){
			.position = {
			  sinf(pitch) * cosf(yaw) * arm_length,
			  sinf(pitch) * arm_length,
			  sinf(pitch) * sinf(yaw) * arm_length,
			},
			.up = { 0.0f, 1.0f, 0.0f },
			.target = { 0.0f, 0.0f, 0.0f },
			.fov = 45.f,

			.projection = CAMERA_PROJECTION_ORTHOGRAPHIC
		};
		pstate->game_camera_start_offset = pstate->game_camera.position;

		// Test entity
		/* Entity collidable_entity = scene_entity_spawn(pstate, FLOAT3_ZERO); */
		/* pstate->transforms[collidable_entity] = (TransformComponent){ */
		/* .position = { 10.f, 1.0f, -5.5f }, */
		/* .scale = FLOAT3_ONE, */
		/* .dirty = true, */
		/* }; */
		/* pstate->colliders[collidable_entity] = (ColliderComponent){ */
		/* .aabb = { */
		/* .center = FLOAT3_ZERO, */
		/* .extent = { 1.0f, 2.0f, 4.0f }, */
		/* }, */
		/* }; */
		/* pstate->components[collidable_entity] = COMPONENT_FLAG_TRANSFORM | COMPONENT_FLAG_COLLIDABLE; */
		/* collidable_entity = scene_entity_spawn(pstate, FLOAT3_ZERO); */
		/* pstate->transforms[collidable_entity] = (TransformComponent){ */
		/* .position = { 5.5f, 1.0f, -10.0f }, */
		/* .scale = FLOAT3_ONE, */
		/* .dirty = true, */
		/* }; */
		/* pstate->colliders[collidable_entity] = (ColliderComponent){ */
		/* .aabb = { */
		/* .center = FLOAT3_ZERO, */
		/* .extent = { 4.0f, 2.0f, 1.0f }, */
		/* }, */
		/* }; */
		/* pstate->components[collidable_entity] = COMPONENT_FLAG_TRANSFORM | COMPONENT_FLAG_COLLIDABLE; */

		pstate->editor = editor_make(&pstate->arena);

		pstate->camera = &pstate->editor.camera;
		pstate->state = GAME_STATE_EDITOR;

		pstate->initialized = true;
	}

	ArenaTemp scratch = arena_scratch_begin(NULL);

	if (input_key_pressed(KEY_CODE_P))
		pstate->game_camera.projection = !pstate->game_camera.projection;
	if (input_key_pressed(KEY_CODE_M))
		pstate->debug_draw_collisions = !pstate->debug_draw_collisions;

	switch (pstate->state) {
		case GAME_STATE_PLAY: {
			Entity player = pstate->player;
			if (player == 0) {
				player = pstate->player = ecs_spawn(pstate->world, FLOAT3_ZERO);
				ecs_put(pstate->world, player, MeshComponent,
					{ .mesh_group_index = arena_array_count(pstate->assets.mesh_groups) - 1 });
			}

			Entity selection = pstate->selection;
			if (selection == 0) {
				/* JsonNode *root = json_parse(scratch.arena, */
				/* 	string_wrap_buffer( */
				/* 		filesystem_read(scratch.arena, */
				/* 			S("selection_entity.prefab")))); */
				/* selection = pstate->selection = deserialize_entity(pstate, root); */

				// selection mesh
				if (selection == 0) {
					selection = pstate->selection = ecs_spawn(pstate->world, FLOAT3_ZERO);

					MeshComponent shared_mesh = (MeshComponent){
						.mesh_group_index = arena_array_count(pstate->assets.mesh_groups) - 1,
						/* .material_index = 0, */
					};
					Entity mesh_part = ecs_spawn(pstate->world, FLOAT3_ZERO);
					ecs_put(pstate->world, mesh_part, TransformComponent,
						{
						  .position = { 0.5f, 0.0f, 0.4f },
						  .scale = { 0.1f, 0.1f, 0.3f },
						  .rotation = FLOAT3_ZERO,
						  .dirty = true,
						});
					ecs_put(pstate->world, mesh_part, MeshComponent, shared_mesh);
					ecs_hierarchy_parent(pstate->world, selection, mesh_part);

					mesh_part = ecs_spawn(pstate->world, FLOAT3_ZERO);
					ecs_put(pstate->world, mesh_part, TransformComponent,
						{
						  .position = { 0.4f, 0.0f, -0.5f },
						  .scale = { 0.1f, 0.1f, 0.3f },
						  .rotation = { 0.0f, 90.0f, 0.0f },
						  .dirty = true,
						});
					ecs_put(pstate->world, mesh_part, MeshComponent, shared_mesh);
					ecs_hierarchy_parent(pstate->world, selection, mesh_part);

					mesh_part = ecs_spawn(pstate->world, FLOAT3_ZERO);
					ecs_put(pstate->world, mesh_part, TransformComponent,
						{
						  .position = { -0.5f, 0.0f, -0.4f },
						  .scale = { 0.1f, 0.1f, 0.3f },
						  .rotation = { 0.0f, -180.0f, 0.0f },
						  .dirty = true,
						});
					ecs_put(pstate->world, mesh_part, MeshComponent, shared_mesh);
					ecs_hierarchy_parent(pstate->world, selection, mesh_part);

					mesh_part = ecs_spawn(pstate->world, FLOAT3_ZERO);
					ecs_put(pstate->world, mesh_part, TransformComponent,
						{
						  .position = { -0.4f, 0.0f, 0.5f },
						  .scale = { 0.1f, 0.1f, 0.3f },
						  .rotation = { 0.0f, -90.0f, 0.0f },
						  .dirty = true,
						});
					ecs_put(pstate->world, mesh_part, MeshComponent, shared_mesh);
					ecs_hierarchy_parent(pstate->world, selection, mesh_part);

					mesh_part = ecs_spawn(pstate->world, FLOAT3_ZERO);
					ecs_put(pstate->world, mesh_part, TransformComponent,
						{
						  .position = { 0.4f, 0.0f, 0.5f },
						  .scale = { 0.1f, 0.1f, 0.3f },
						  .rotation = { 0.0f, 90.0f, 0.0f },
						  .dirty = true,
						});
					ecs_put(pstate->world, mesh_part, MeshComponent, shared_mesh);
					ecs_hierarchy_parent(pstate->world, selection, mesh_part);

					mesh_part = ecs_spawn(pstate->world, FLOAT3_ZERO);
					ecs_put(pstate->world, mesh_part, TransformComponent,
						{
						  .position = { 0.5f, 0.0f, -0.4f },
						  .scale = { 0.1f, 0.1f, 0.3f },
						  .rotation = { 0.0f, -180.0f, 0.0f },
						  .dirty = true,
						});
					ecs_put(pstate->world, mesh_part, MeshComponent, shared_mesh);
					ecs_hierarchy_parent(pstate->world, selection, mesh_part);

					mesh_part = ecs_spawn(pstate->world, FLOAT3_ZERO);
					ecs_put(pstate->world, mesh_part, TransformComponent,
						{
						  .position = { -0.4f, 0.0f, -0.5f },
						  .scale = { 0.1f, 0.1f, 0.3f },
						  .rotation = { 0.0f, -90.0f, 0.0f },
						  .dirty = true,
						});
					ecs_put(pstate->world, mesh_part, MeshComponent, shared_mesh);
					ecs_hierarchy_parent(pstate->world, selection, mesh_part);

					mesh_part = ecs_spawn(pstate->world, FLOAT3_ZERO);
					ecs_put(pstate->world, mesh_part, TransformComponent,
						{
						  .position = { -0.5f, 0.0f, 0.4f },
						  .scale = { 0.1f, 0.1f, 0.3f },
						  .rotation = { 0.0f, 0.0f, 0.0f },
						  .dirty = true,
						});
					ecs_put(pstate->world, mesh_part, MeshComponent, shared_mesh);
					ecs_hierarchy_parent(pstate->world, selection, mesh_part);
				}

				/* ArenaTemp json_temp = arena_scratch_begin(scratch.arena); */
				/* JsonExporter exporter = json_exporter_make(json_temp.arena); */
				/* serialize_entity(pstate, &exporter, selection); */

				/* File file = filesystem_open(S("selection_entity.prefab"), FILE_MODE_WRITE); */

				/* file_write(&file, 1, exporter.arena->offset - exporter.start_offset, (uint8_t *)exporter.arena->base + exporter.start_offset); */
				/* file_close(&file); */

				/* arena_scratch_end(json_temp); */
			}

			Camera *camera = &pstate->game_camera;
			TransformComponent *transform = ecs_find(pstate->world, player, TransformComponent);

			float3 camera_target_offset = float3_subtract(camera->target, camera->position);
			float3 camera_forward = float3_normalize(camera_target_offset);

			float3 camera_right = float3_cross(camera->up, camera_forward);
			float3 camera_up = float3_cross(camera_right, camera_forward);

			/* float3 move = { */
			/* 	.x = input_key_down(KEY_CODE_D) - input_key_down(KEY_CODE_A), */
			/* 	.y = 0, */
			/* 	.z = input_key_down(KEY_CODE_S) - input_key_down(KEY_CODE_W), */
			/* }; */
			float3 world_up = { 0.0f, 1.0f, 0.0f };
			float3 forward = float3_normalize_safe(
				float3_subtract(
					camera_forward,
					float3_scale(world_up, float3_dot(camera_forward, world_up))),
				EPSILON);
			float3 right = float3_cross(forward, world_up);

			float3 move_y = float3_scale(forward, input_key_down(KEY_CODE_W) - input_key_down(KEY_CODE_S));
			float3 move_x = float3_scale(right, input_key_down(KEY_CODE_D) - input_key_down(KEY_CODE_A));
			float3 move = float3_add(move_x, move_y);

			move = float3_normalize_safe(move, EPSILON);
			float player_move_speed = 10.f;
			if (float3_length_squared(move) > 0) {
				float3 move_delta = float3_scale(move, player_move_speed * dt);

				float t_remaining = 1.0f;
				for (uint32_t iteration = 0; iteration < 4 && t_remaining > 0.0f; ++iteration) {
					// Collision detection
					float3 ro = transform->position;
					float3 rd = float3_normalize_safe(move_delta, EPSILON);

					Raycast3Result nearest = RAY3_NO_HIT;
					EcsIterator iterator = ecs_query(pstate->world, ecs_type_id(TransformComponent), ecs_type_id(ColliderComponent));
					Entity entity;
					while ((entity = ecs_next(&iterator))) {
						Transform3 *collision_transform = ecs_find(pstate->world, entity, Transform3);
						ColliderComponent *shape = ecs_find(pstate->world, entity, ColliderComponent);
						float4x4 trs = collision_transform->global_matrix;

						float3 translation = { trs.elements[12], trs.elements[13], trs.elements[14] };
						float3 center = float3_add(shape->aabb.center, translation);

						// NOTE: Minkowski sum
						float3 scaling = float4x4_transform(trs,
							(float4){ shape->aabb.extent.x, shape->aabb.extent.y, shape->aabb.extent.z, 0.0f });
						float3 extent = {
							.x = fabsf(scaling.x) + 0.5f,
							.y = fabsf(scaling.y) + 0.5f,
							.z = fabsf(scaling.z) + 0.5f,
						};

						Raycast3Result result = raycast_aabb3(ro, rd, center, extent);
						if (result.t < nearest.t)
							nearest = result;
					}

					// Collision resolution
					float t_actual = nearest.t / (player_move_speed * dt);
					float t_min = fminf(1.0f, t_actual);

					transform->dirty = true;
					transform->position = float3_add(transform->position, float3_scale(move_delta, t_min - 0.001f));

					move_delta = float3_subtract(move_delta, float3_scale(nearest.normal, float3_dot(move_delta, nearest.normal)));
					t_remaining -= t_min;
				}
			}

			camera->target = transform->position;
			camera->position.x = pstate->game_camera_start_offset.x + transform->position.x;
			camera->position.z = pstate->game_camera_start_offset.z + transform->position.z;

			// select
			float2 mouse_position = float2_from_double2(input_mouse_position());
			float2 window_size = float2_from_uint2(window_size_pixel(context->display));

			float2 mouse_offset = float2_subtract(float2_scale(window_size, 0.5f), mouse_position);
			float aspect = window_size.x / window_size.y;
			{
				// Normalized device coordinates, -1..1
				float2 ndc = {
					(mouse_position.x / window_size.x) * 2.0f - 1.0f,
					(mouse_position.y / window_size.y) * 2.0f - 1.0f,
				};
			}
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

	// Update transforms
	EcsIterator it = ecs_query(pstate->world, COMPONENT_TYPE_TransformComponent);
	Entity entity;

	while ((entity = ecs_next(&it))) {
		HierarchyComponent *node = ecs_find(pstate->world, entity, HierarchyComponent);

		// If it has NO parent, it is a root. Start a recursive descent here.
		if (node == NULL || node->parent == 0) {
			TransformComponent *t = ecs_find(pstate->world, entity, TransformComponent);

			t->local_matrix = float4x4_compose(t->position, t->rotation, t->scale);
			t->global_matrix = t->local_matrix; // Roots have no parent matrix
			t->dirty = false;

			if (node && node->first_child) {
				Entity child = node->first_child;
				while (child) {
					calculate_transforms_recursive(pstate->world, child, t->global_matrix);
					child = ecs_find(pstate->world, child, HierarchyComponent)->next_sibling;
				}
			}
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

		float3 light_position = { 0.0f, 20.0f, -30.0f };
		light_position = float3_scale(float3_normalize(light_position), 20);
		float4 light = { light_position.x, light_position.y, light_position.z, 1.0f };
		size_t light_offset = vulkan_buffer_push(pstate->context, pstate->frame_storage_buffer, sizeof(float4), &light);

		RhiUniformSet global_set = vulkan_uniformset_push(pstate->context, pstate->phong_shader, 0);
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
			vulkan_shader_bind(pstate->context, pstate->phong_shader, pipeline);
			vulkan_uniformset_bind(pstate->context, global_set);
			EcsIterator iterator = ecs_query(pstate->world, ecs_type_id(TransformComponent), ecs_type_id(MeshComponent));

			Entity entity;
			while ((entity = ecs_next(&iterator))) {
				TransformComponent *transform = ecs_find(pstate->world, entity, TransformComponent);
				MeshComponent *mesh_component = ecs_find(pstate->world, entity, MeshComponent);

				if (mesh_component->mesh_group_index == 0 || mesh_component->mesh_group_index > arena_array_count(pstate->assets.mesh_groups))
					continue;
				uint32x2 mesh_group = pstate->assets.mesh_groups[mesh_component->mesh_group_index];

				for (uint32_t mesh_index = mesh_group.x; mesh_index < mesh_group.x + mesh_group.y; ++mesh_index) {
					Mesh *mesh = &pstate->assets.meshes[mesh_index];
					uint32_t material_index = pstate->assets.mesh_to_material[mesh_index];
					Material *material = &pstate->assets.materials[material_index];

					RhiUniformSet group = vulkan_uniformset_push(pstate->context, pstate->phong_shader, 1);
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

					float4x4 model_matrix = transform->global_matrix;
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
			if (pstate->debug_draw_collisions) {
				vulkan_shader_bind(pstate->context, pstate->screenline_shader, pipeline);
				EcsIterator iterator = ecs_query(pstate->world, ecs_type_id(TransformComponent), ecs_type_id(ColliderComponent));

				Entity entity;
				while ((entity = ecs_next(&iterator))) {
					TransformComponent *transform = ecs_find(pstate->world, entity, TransformComponent);
					ColliderComponent *shape = ecs_find(pstate->world, entity, ColliderComponent);

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

					float4x4 model_matrix = transform->global_matrix;
					vulkan_push_constants(pstate->context, 0, sizeof(float4x4), model_matrix.elements);

					RhiUniformSet group = vulkan_uniformset_push(pstate->context, pstate->screenline_shader, 1);
					vulkan_uniformset_bind_buffer_range(pstate->context, group, 0, storage_offset, size, buffer);
					vulkan_uniformset_bind_buffer_range(pstate->context, group, 1, uniform_offset, sizeof(float4), pstate->frame_uniform_buffer);
					vulkan_uniformset_bind(pstate->context, group);

					vulkan_renderer_draw(pstate->context, (sizeof(outline) / sizeof(float4)) * 6);
				}
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

void editor_select_entity(Editor *editor, Entity entity, bool multi) {
	ASSERT(editor->selected_entity_count < countof(editor->selected_entities));
	if (entity == 0) {
		editor->selected_entity_count = editor->active_entity = 0;
		return;
	}

	editor->selected_entity_count = multi ? editor->selected_entity_count : 0;
	for (uint32_t index = 0; index < editor->selected_entity_count; ++index) {
		if (editor->selected_entities[index] == entity) {
			editor->selected_entities[index] = editor->active_entity = entity;
			return;
		}
	}

	editor->selected_entities[editor->selected_entity_count++] = entity;
	editor->active_entity = entity;
}

void editor_update(PermanentState *pstate, Editor *editor, float dt) {
	Camera *camera = &editor->camera;
	float2 mouse_delta = (float2){ input_mouse_dx(), input_mouse_dy() };

	if (input_mouse_down(MOUSE_BUTTON_MIDDLE))
		window_set_cursor_locked(pstate->display, true);
	else
		window_set_cursor_locked(pstate->display, false);

	bool entity_updated = false;
	if (editor->selected_entity_count > 0) {
		// enter TRS mode
		TRSMode was = editor->transform.mode;
		if (input_key_pressed(KEY_CODE_G))
			editor->transform.mode = TRS_MODE_TRANSLATION;
		if (input_key_pressed(KEY_CODE_R))
			editor->transform.mode = TRS_MODE_ROTATION;
		if (input_key_down(KEY_CODE_LEFTSHIFT) == false && input_key_pressed(KEY_CODE_S))
			editor->transform.mode = TRS_MODE_SCALING;

		if (editor->transform.mode && was != editor->transform.mode) {
			editor->mode = EDITOR_MODE_TRANSFORM;
			editor->transform.axis = AXIS_MODE_XYZ;
			editor->transform.cached = *ecs_find(pstate->world, editor->active_entity, TransformComponent);
			editor->transform.mouse_start_position = float2_from_double2(input_mouse_position());
		}

		// enter axis constraint mode
		if (editor->mode == EDITOR_MODE_TRANSFORM) {
			bool exclude = input_key_down(KEY_CODE_LEFTCTRL);

			bool x_axis = input_key_pressed(KEY_CODE_X);
			bool y_axis = input_key_pressed(KEY_CODE_Y);
			bool z_axis = input_key_pressed(KEY_CODE_Z);

			if (exclude == false) {
				if (x_axis)
					editor->transform.axis = AXIS_MODE_X;
				if (y_axis)
					editor->transform.axis = AXIS_MODE_Y;
				if (z_axis)
					editor->transform.axis = AXIS_MODE_Z;
			}
		}

		// add/swap mesh component
		if (input_key_pressed(KEY_CODE_Q) || input_key_pressed(KEY_CODE_E)) {
			uint32_t group_count = arena_array_count(pstate->assets.mesh_groups);
			MeshComponent *entity_mesh = ecs_push(pstate->world, editor->active_entity, MeshComponent);

			if (input_key_pressed(KEY_CODE_Q))
				entity_mesh->mesh_group_index = entity_mesh->mesh_group_index > 1
					? entity_mesh->mesh_group_index - 1
					: group_count - 1;

			if (input_key_pressed(KEY_CODE_E))
				entity_mesh->mesh_group_index = (entity_mesh->mesh_group_index % (group_count - 1)) + 1;
		}

		// Add collision component as a child
		{
			if (input_key_pressed(KEY_CODE_C)) {
				Entity collider = ecs_spawn(pstate->world, FLOAT3_ZERO);
				ecs_put(pstate->world, collider, ColliderComponent,
					{
					  .aabb = {
						.center = FLOAT3_ZERO,
						.extent = FLOAT3_ONE,
					  },
					});
				ecs_hierarchy_parent(pstate->world, editor->active_entity, collider);
				editor_select_entity(editor, collider, false);
			}
		}

		// duplicate entity & children
		// NOTE: Only copies the first layer down
		if (input_key_down(KEY_CODE_LEFTSHIFT) && input_key_pressed(KEY_CODE_D)) {
			Entity new_entity = ecs_hierarchical_copy(pstate->world, editor->active_entity);
			editor_select_entity(editor, new_entity, false);
		}

		// TODO: Make undo-able
		// delete entity
		if (input_key_pressed(KEY_CODE_DELETE)) {
			ecs_despawn(pstate->world, editor->active_entity);
			editor_select_entity(editor, 0, false);
		}
	}

	// serialize scene
	/* if (input_key_down(KEY_CODE_LEFTCTRL) && input_key_pressed(KEY_CODE_S)) { */
	/* 	if (pstate->entity_count > 0) { */
	/* 		String path = S("assets/scene/test.scene"); */
	/* 		filesystem_make_directory(stringpath_directory(path)); */

	/* 		if (scene_serialize(path, pstate)) */
	/* 			LOG_INFO("scene serialized to '%.*s'", SARG(path)); */
	/* 	} */
	/* } */

	// add entity
	if (input_key_down(KEY_CODE_LEFTSHIFT) && input_key_pressed(KEY_CODE_A)) {
		Entity entity = ecs_spawn(pstate->world, FLOAT3_ZERO);
		editor_select_entity(editor, entity, false);
	}

	float2 screen_size = float2_from_uint2(window_size_pixel(pstate->display));
	float2 mouse_position = float2_from_double2(input_mouse_position());
	float2 mouse_offset = float2_negate(float2_subtract(mouse_position, editor->transform.mouse_start_position));

	float3 camera_target_offset = float3_subtract(camera->target, camera->position);
	float3 camera_forward = float3_normalize(camera_target_offset);

	float3 camera_right = float3_cross(camera->up, camera_forward);
	float3 camera_up = float3_cross(camera_right, camera_forward);

	// update editor
	switch (editor->mode) {
		case EDITOR_MODE_VIEWING: {
			if (input_mouse_pressed(MOUSE_BUTTON_LEFT)) {
				double2 mouse_position = input_mouse_position();
				uint32_t entity = 0;
				vulkan_texture_read_pixel(pstate->context, pstate->picker_target, (uint32_t)mouse_position.x, (uint32_t)mouse_position.y, &entity);

				editor_select_entity(editor, entity, input_key_down(KEY_CODE_LEFTSHIFT));
				LOG_INFO("acitve = %d, count = %d", entity, editor->selected_entity_count);
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
				TransformComponent *transform = ecs_find(pstate->world, editor->undo_steps[last_step].entity, TransformComponent);
				ecs_put(pstate->world, entity, TransformComponent, editor->undo_steps[last_step].cached_transform);
				/* pstate->meshes[entity] = editor->undo_steps[last_step].cached_mesh; */

				editor->undo_write_cursor = last_step;
				editor->undo_count--;
			}
		} break;
		case EDITOR_MODE_TRANSFORM: {
			TransformComponent *transform = ecs_find(pstate->world, editor->active_entity, TransformComponent);
			float distance = float3_dot(float3_subtract(editor->transform.cached.position, camera->position), camera_forward);
			float fov_radians = deg2radf(camera->fov);
			float frustum_height = 2 * tanf(fov_radians * 0.5f) * distance;
			float frustum_width = frustum_height * (screen_size.x / screen_size.y);

			float2 units_per_pixel = {
				.x = frustum_width / screen_size.x,
				.y = frustum_height / screen_size.y,
			};
			float2 pixels_per_unit = {
				.x = screen_size.x / frustum_width,
				.y = screen_size.x / frustum_height,
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

			switch (editor->transform.mode) {
				case TRS_MODE_TRANSLATION: {
					if (editor->transform.axis == AXIS_MODE_XYZ && editor->transform.number_input == 0) {
						float3 move_x = float3_scale(camera_right, mouse_offset.x * units_per_pixel.x);
						float3 move_y = float3_scale(camera_up, -mouse_offset.y * units_per_pixel.y);

						transform->position = float3_add(
							float3_add(editor->transform.cached.position, move_x),
							move_y);
					} else if (editor->transform.axis && editor->transform.number_input == 0) {
						float3 axis = { 0 };
						if (editor->transform.axis == AXIS_MODE_X)
							axis = FLOAT3_X;
						else if (editor->transform.axis == AXIS_MODE_Z)
							axis = FLOAT3_Z;
						else if (editor->transform.axis == AXIS_MODE_Y)
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
					} else if (editor->transform.number_input) {
						if (input_key_pressed(KEY_CODE_BACKSPACE))
							editor->transform.number_input /= 10;

						float3 move = { 0 };
						float scale = editor->transform.number_input * (editor->transform.axis_reverse ? -1.f : 1.f);
						if (editor->transform.axis == AXIS_MODE_X)
							move = float3_scale(FLOAT3_X, scale);
						else if (editor->transform.axis == AXIS_MODE_Z)
							move = float3_scale(FLOAT3_Z, scale);
						else if (editor->transform.axis == AXIS_MODE_Y)
							move = float3_scale(FLOAT3_Y, scale);
						transform->position = float3_add(editor->transform.cached.position, move);
					}

					if (input_key_down(KEY_CODE_LEFTCTRL)) {
						transform->position.x = (int32_t)transform->position.x;
						transform->position.y = (int32_t)transform->position.y;
						transform->position.z = (int32_t)transform->position.z;
					}

				} break;
				case TRS_MODE_ROTATION: {
					// TODO: Rotation is kind of janky
					if (editor->transform.axis == AXIS_MODE_XYZ) {
					} else if (editor->transform.number_input) {
						// Angle 0 is the angle from the
						if (input_key_pressed(KEY_CODE_BACKSPACE))
							editor->transform.number_input /= 10;

						float3 rotation = { 0 };
						float scale = editor->transform.number_input * (editor->transform.axis_reverse ? -1.f : 1.f);
						if (editor->transform.axis == AXIS_MODE_X)
							rotation = float3_scale(FLOAT3_X, scale);
						else if (editor->transform.axis == AXIS_MODE_Z)
							rotation = float3_scale(FLOAT3_Z, scale);
						else if (editor->transform.axis == AXIS_MODE_Y)
							rotation = float3_scale(FLOAT3_Y, scale);
						transform->rotation = float3_add(editor->transform.cached.rotation, rotation);
					} else {
						float3 axis = { 0 };
						if (editor->transform.axis == AXIS_MODE_X)
							axis = FLOAT3_X;
						else if (editor->transform.axis == AXIS_MODE_Z)
							axis = FLOAT3_Z;
						else if (editor->transform.axis == AXIS_MODE_Y)
							axis = FLOAT3_Y;

						float2 sign = {
							float3_dot(axis, camera_right) < 0.0f ? -1.0f : 1.0f,
							float3_dot(axis, camera_up) < 0.0f ? 1.0f : -1.0f
						};
						float3 move_x = float3_scale(axis, sign.x * mouse_offset.x * units_per_pixel.x);
						float3 move_y = float3_scale(axis, sign.y * mouse_offset.y * units_per_pixel.y);

						transform->rotation = float3_add(
							float3_add(editor->transform.cached.rotation, move_x),
							move_y);
					}

					if (input_key_down(KEY_CODE_LEFTCTRL)) {
						float snap = 15.0f;
						transform->rotation.x = (int32_t)(transform->rotation.x / snap) * snap;
						transform->rotation.y = (int32_t)(transform->rotation.y / snap) * snap;
						transform->rotation.z = (int32_t)(transform->rotation.z / snap) * snap;
					}
				} break;
				case TRS_MODE_SCALING: {
					float2 object_to_screen = {
						(screen_size.x * 0.5f) + -1 * (float3_dot(camera_right, transform->position) * pixels_per_unit.x),
						(screen_size.y * 0.5f) + -1 * (float3_dot(camera_up, transform->position) * pixels_per_unit.y),
					};
					float2 start_delta = float2_subtract(object_to_screen, editor->transform.mouse_start_position);
					start_delta.x /= screen_size.x;
					start_delta.y /= screen_size.y;
					float2 current_delta = float2_subtract(object_to_screen, mouse_position);
					current_delta.x /= screen_size.x;
					current_delta.y /= screen_size.y;

					if (editor->transform.axis == AXIS_MODE_XYZ) {
						transform->scale = float3_scale(editor->transform.cached.scale, float2_length(current_delta) / float2_length(start_delta));
					} else if (editor->transform.number_input) {
						if (input_key_pressed(KEY_CODE_BACKSPACE))
							editor->transform.number_input /= 10;

						float3 scaling = { 0 };
						float scale = editor->transform.number_input * (editor->transform.axis_reverse ? -1.f : 1.f);
						if (editor->transform.axis == AXIS_MODE_X)
							scaling = float3_scale(FLOAT3_X, scale);
						else if (editor->transform.axis == AXIS_MODE_Z)
							scaling = float3_scale(FLOAT3_Z, scale);
						else if (editor->transform.axis == AXIS_MODE_Y)
							scaling = float3_scale(FLOAT3_Y, scale);
						transform->scale = float3_add(editor->transform.cached.scale, scaling);
					} else {
						float scale = float2_length(current_delta) / float2_length(start_delta);
						if (editor->transform.axis == AXIS_MODE_X)
							transform->scale.x = editor->transform.cached.scale.x * scale;
						if (editor->transform.axis == AXIS_MODE_Z)
							transform->scale.y = editor->transform.cached.scale.y * scale;
						if (editor->transform.axis == AXIS_MODE_Y)
							transform->scale.z = editor->transform.cached.scale.z * scale;
					}

					if (input_key_down(KEY_CODE_LEFTCTRL)) {
						transform->scale.x = (int32_t)transform->scale.x;
						transform->scale.y = (int32_t)transform->scale.y;
						transform->scale.z = (int32_t)transform->scale.z;
					}
				} break;
				case TRS_MODE_NONE: {
					ASSERT(false);
				} break;
			}

			transform->dirty = true;

			if (input_mouse_pressed(MOUSE_BUTTON_LEFT) || input_key_pressed(KEY_CODE_ENTER)) {
				editor->undo_steps[editor->undo_write_cursor].entity = editor->active_entity;
				editor->undo_steps[editor->undo_write_cursor].cached_transform = editor->transform.cached;

				editor->undo_write_cursor = (editor->undo_write_cursor + 1) % countof(editor->undo_steps);
				editor->undo_count++;
				editor->undo_count = MIN(countof(editor->undo_steps), editor->undo_count);
			}
			if (input_mouse_pressed(MOUSE_BUTTON_RIGHT) || input_key_pressed(KEY_CODE_ESCAPE))
				*transform = editor->transform.cached;
		} break;
	}

	// reset editor
	if (input_mouse_pressed(MOUSE_BUTTON_LEFT) ||
		input_key_pressed(KEY_CODE_ENTER) ||
		input_mouse_pressed(MOUSE_BUTTON_RIGHT) ||
		input_key_pressed(KEY_CODE_ESCAPE)

	) {
		if (editor->active_entity)
			ecs_find(pstate->world, editor->active_entity, TransformComponent)->dirty = true;

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

	float4 light_position = { 0.0f, 20.0f, -30.0f, 100.0f };
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

		EcsIterator iterator = ecs_query(pstate->world, ecs_type_id(TransformComponent), ecs_type_id(MeshComponent));
		Entity entity;
		while ((entity = ecs_next(&iterator))) {
			TransformComponent *transform = ecs_find(pstate->world, entity, TransformComponent);
			MeshComponent *mesh_component = ecs_find(pstate->world, entity, MeshComponent);

			if (mesh_component->mesh_group_index == 0 || mesh_component->mesh_group_index > arena_array_count(pstate->assets.mesh_groups))
				continue;
			uint32x2 mesh_group = pstate->assets.mesh_groups[mesh_component->mesh_group_index];

			vulkan_push_constants(pstate->context, sizeof(float4x4), sizeof(uint32_t), &entity);

			for (uint32_t mesh_index = mesh_group.x; mesh_index < mesh_group.x + mesh_group.y; ++mesh_index) {
				Mesh *mesh = &pstate->assets.meshes[mesh_index];
				float4x4 model_matrix = transform->global_matrix;
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

		for (uint32_t index = 0; index < editor->selected_entity_count; ++index) {
			Entity entity = editor->selected_entities[index];
			ASSERT(ecs_has(pstate->world, entity, TransformComponent));
			TransformComponent *transform = ecs_find(pstate->world, entity, TransformComponent);
			float3 min = { 0 };
			float3 max = { 0 };

			if (ecs_has(pstate->world, entity, MeshComponent)) {
				MeshComponent *mesh_component = ecs_find(pstate->world, entity, MeshComponent);
				ASSERT(mesh_component->mesh_group_index && mesh_component->mesh_group_index < arena_array_count(pstate->assets.mesh_groups));

				Interval3 *bounding_box = &pstate->assets.mesh_group_bounds[mesh_component->mesh_group_index];
				min = bounding_box->min;
				max = bounding_box->max;
			} else if (ecs_has(pstate->world, entity, ColliderComponent)) {
				continue;
			} else {
				min = float3_negate(float3_scale(transform->scale, 0.5f));
				max = float3_scale(transform->scale, 0.5f);
			}

			uint32_t line_segment_count = 24;
			size_t line_segment_size = 2 * sizeof(float4);
			size_t size = line_segment_count * line_segment_size;

			RhiBuffer buffer = pstate->frame_storage_buffer;
			float3 bounding_box_size = float3_subtract(max, min);

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

			float4x4 model_matrix = transform->global_matrix;
			float4 color = entity == editor->active_entity ? (float4){ 1.0f, 0.2f, 0.2f, 1.0f } : (float4){ 1.0f, 0.5f, 0.3f, 1.0f };

			vulkan_push_constants(pstate->context, 0, sizeof(float4x4), model_matrix.elements);
			size_t uniform_offset = vulkan_buffer_push(pstate->context, pstate->frame_uniform_buffer, sizeof(float4), &color);
			size_t point_offset = vulkan_buffer_push(pstate->context, pstate->frame_storage_buffer, sizeof(selection), selection);

			RhiUniformSet group = vulkan_uniformset_push(pstate->context, pstate->screenline_shader, 1);
			vulkan_uniformset_bind_buffer_range(pstate->context, group, 0, point_offset, sizeof(selection), pstate->frame_storage_buffer);
			vulkan_uniformset_bind_buffer_range(pstate->context, group, 1, uniform_offset, sizeof(float4), pstate->frame_uniform_buffer);
			vulkan_uniformset_bind(pstate->context, group);

			vulkan_renderer_draw(pstate->context, line_segment_count * 2 * 6);
		}

		vulkan_drawlist_end(pstate->context);
	}

	pstate->previous_target = pstate->current_target;
	pstate->current_target = (pstate->current_target + 1) % countof(pstate->main_color_targets);
}
