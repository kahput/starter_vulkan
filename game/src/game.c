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
	uint32_t parent_index;

	float3 position, scale, rotation;
	float4x4 local, global;
} TransformComponent;

typedef struct {
	uint32_t mesh_group_index;
	uint32_t material_index;
} MeshComponent;

typedef enum {
	COMPONENT_FLAG_TRANSFORM = 1 << 0,
	COMPONENT_FLAG_MESH = 1 << 1,
	COMPONENT_FLAG_DRAWABLE = COMPONENT_FLAG_TRANSFORM | COMPONENT_FLAG_MESH,
} ComponentFlags;

// EDITOR
typedef enum {
	AXIS_MODE_NONE,
	AXIS_MODE_X,
	AXIS_MODE_Y,
	AXIS_MODE_Z,
	AXIS_MODE_XY,
	AXIS_MODE_YZ,
	AXIS_MODE_ZX,
	AXIS_MODE_XYZ,
} AxisMode;
typedef struct Editor {
	float sensitivity, pan_speed, zoom_speed;
	Camera camera;

	bool grab;
	uint32_t number_input;

	AxisMode axis_mode;
	bool axis_reverse;

	float2 grab_mouse_position;
	TransformComponent cached_selection_transform;
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
	RhiShader line_shader, picker_shader;
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
	uint32_t components[MAX_ENTITIES];

	Entity selected_entity;
	Entity player;

	Camera *camera;
} PermanentState;

Editor editor_make(void);
void editor_update(PermanentState *state, Editor *editor, float dt);
void editor_draw(PermanentState *state, Editor *editor);

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
		pstate->line_shader =
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
		pstate->entity_count = 0;
		pstate->transforms[pstate->entity_count] = (TransformComponent){ 0 };
		pstate->meshes[pstate->entity_count] = (MeshComponent){ 0 };
		pstate->entity_count++;

		// TODO: Import the node transforms & cache shared textures
		SceneSource models[] = {
			importer_load_gltf_scene(scratch.arena, S("assets/models/modular_dungeon/room-large.glb")),
			importer_load_gltf_scene(scratch.arena, S("assets/models/modular_dungeon/room-small.glb")),
			importer_load_gltf_scene(scratch.arena, S("assets/models/modular_dungeon/corridor.glb")),
			importer_load_gltf_scene(scratch.arena, S("assets/models/modular_dungeon/gate-door.glb")),
		};

		/* uint32_t player_mesh_index = arena_array_count(mesh_sources); */
		/* MeshSource player_src = mesh_source_cube(scratch.arena, 0, 0.5f, 0); */
		/* Interval3 player_bounds = { */
		/* 	.min = { -0.5f, 0, -0.5f }, */
		/* 	.max = { 0.5f, 1, 0.5f }, */
		/* }; */

		/* arena_darray_put(scratch.arena, mesh_sources, MeshSource, player_src); */
		/* arena_darray_put(scratch.arena, mesh_to_material, uint32_t, 0); */
		/* arena_darray_put(scratch.arena, bounding_boxes, Interval3, player_bounds); */

		/*
		 * The final runtime format for the GPU is
		 * float *vertices + uint32_t *indices of all geometry + metadata for offsets
		 */

		// Prep Upload
		RhiTexture *textures = NULL;
		Material *materials = NULL;
		Mesh *meshes = NULL;
		uint32_t *mesh_to_material = NULL;
		Interval3 *bounds = NULL;
		uint32x2 *mesh_groups = NULL;

		uint32_t material_offset = 0;
		uint32_t texture_offset = 0;
		uint32_t mesh_offset = 0;
		size_t vertex_offset = 0, index_offset = 0;

		// Defaults
		arena_darray_push(scratch.arena, mesh_groups, uint32x2); // 0 == invalid

		Arena *geometry_upload_arena = arena_partition(scratch.arena, MiB(32));
		for (uint32_t model_index = 0; model_index < countof(models); ++model_index) {
			SceneSource *model = &models[model_index];
			for (uint32_t image_index = 0; image_index < model->image_count; ++image_index) {
				ImageSource *src = &model->images[image_index];
				RhiTexture *dst = arena_darray_push(scratch.arena, textures, RhiTexture);

				if (src->pixels)
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
					if (textures)
						dst->textures[texture_index] = textures[src->properties[texture_index].as.uint32x1 + texture_offset];
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

				arena_darray_put(scratch.arena, mesh_to_material, uint32_t, model->mesh_to_material[mesh_index] + material_offset);
				arena_darray_put(scratch.arena, bounds, Interval3, model->bounding_boxes[mesh_index]);
			}

			arena_darray_put(scratch.arena, mesh_groups, uint32x2, { mesh_offset, model->mesh_count });

			material_offset += model->material_count;
			mesh_offset += model->mesh_count;

			arena_push_copy(geometry_upload_arena, model->vertices, model->vertices_size, 1);
			arena_push_copy(geometry_upload_arena, model->indices, model->indices_size, 1);
		}

		pstate->assets.textures = arena_array_copy(&pstate->arena, textures, RhiTexture);
		pstate->assets.meshes = arena_array_copy(&pstate->arena, meshes, Mesh);
		uint32_t mesh_count = arena_array_count(pstate->assets.meshes);
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

		pstate->editor = editor_make();

		for (uint32_t index = 0; index < 32; ++index) {
			Entity e = pstate->entity_count++;
			pstate->transforms[e] = (TransformComponent){
				.position = { 0.0f, 0.0f, 0.0f },
				.scale = FLOAT3_ONE,
			};
			pstate->transforms[e].global = float4x4_translation(pstate->transforms[1].position);

			pstate->meshes[e] = (MeshComponent){
				.mesh_group_index = 1 + 2 * (index % 2),
				.material_index = 0,
			};
			pstate->components[e] = COMPONENT_FLAG_DRAWABLE;
		}

		pstate->camera = &pstate->editor.camera;
		pstate->state = GAME_STATE_EDITOR;

		arena_scratch_end(scratch);

		pstate->initialized = true;
	}

	ArenaTemp scratch = arena_scratch_begin(NULL);

	switch (pstate->state) {
		case GAME_STATE_PLAY: {
			Camera *camera = &pstate->game_camera;
			TransformComponent *transform = &pstate->transforms[pstate->player];

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
			vulkan_shader_bind(pstate->context, pstate->pbr_shader, pipeline);
			vulkan_uniformset_bind(pstate->context, global_set);

			// Entities
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

					vulkan_push_constants(pstate->context, 0, sizeof(float4x4), &transform->global);

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

void editor_update(PermanentState *pstate, Editor *editor, float dt) {
	Camera *camera = &editor->camera;
	float2 mouse_delta = (float2){ input_mouse_dx(), input_mouse_dy() };

	if (input_mouse_down(MOUSE_BUTTON_MIDDLE))
		window_set_cursor_locked(pstate->display, true);
	else
		window_set_cursor_locked(pstate->display, false);

	// State transition
	if (input_mouse_pressed(MOUSE_BUTTON_LEFT)) {
		double2 mouse_position = input_mouse_position();
		uint32_t node_index = 0;
		vulkan_texture_read_pixel(pstate->context, pstate->picker_target, (uint32_t)mouse_position.x, (uint32_t)mouse_position.y, &node_index);
		LOG_INFO("Node index = %d", node_index);

		pstate->selected_entity = node_index;
	}

	if (pstate->selected_entity) {
		if (input_key_pressed(KEY_CODE_G)) {
			editor->grab = !editor->grab;
			if (editor->grab) {
				editor->cached_selection_transform = pstate->transforms[pstate->selected_entity];
				editor->grab_mouse_position = float2_from_double2(input_mouse_position());
			}
		}

		if (editor->grab) {
			bool exclude = input_key_down(KEY_CODE_LEFTCTRL);

			bool x_axis = input_key_pressed(KEY_CODE_X);
			bool y_axis = input_key_pressed(KEY_CODE_Y);
			bool z_axis = input_key_pressed(KEY_CODE_Z);

			if (exclude == false) {
				if (x_axis)
					editor->axis_mode = AXIS_MODE_X;
				if (y_axis)
					editor->axis_mode = AXIS_MODE_Y;
				if (z_axis)
					editor->axis_mode = AXIS_MODE_Z;
			}
		}
	}

	float2 screen_size = float2_from_uint2(window_size_pixel(pstate->display));
	float2 mouse_position = float2_from_double2(input_mouse_position());
	float2 offset = float2_negate(float2_subtract(mouse_position, editor->grab_mouse_position));

	float3 camera_target_offset = float3_subtract(camera->target, camera->position);
	float3 camera_forward = float3_normalize(camera_target_offset);

	float3 camera_right = float3_cross(camera->up, camera_forward);
	float3 camera_up = float3_cross(camera_right, camera_forward);

	if (editor->grab) {
		TransformComponent *transform = &pstate->transforms[pstate->selected_entity];
		float distance = float3_dot(float3_subtract(editor->cached_selection_transform.position, camera->position), camera_forward);
		float fov_radians = deg2radf(camera->fov);
		float frustum_height = 2 * tanf(fov_radians * 0.5f) * distance;
		float frustum_width = frustum_height * (screen_size.x / screen_size.y);

		float2 units_per_pixel = {
			.x = frustum_width / screen_size.x,
			.y = frustum_height / screen_size.y,
		};

		for (uint32_t key = KEY_CODE_0; key < KEY_CODE_9 + 1; ++key) {
			if (input_key_pressed(key)) {
				if (editor->number_input == 0)
					*transform = editor->cached_selection_transform;
				editor->number_input *= 10;
				editor->number_input += key - KEY_CODE_0;
			}
		}
		if (input_key_pressed(KEY_CODE_MINUS))
			editor->axis_reverse = true;

		if (editor->axis_mode == AXIS_MODE_NONE && editor->number_input == 0) {
			// TODO: The postion/rotation/scale are in local space, extract/calculate the world position
			float3 move_x = float3_scale(camera_right, offset.x * units_per_pixel.x);
			float3 move_y = float3_scale(camera_up, -offset.y * units_per_pixel.y);

			transform->position = float3_add(
				float3_add(editor->cached_selection_transform.position, move_x),
				move_y);
		} else if (editor->axis_mode != AXIS_MODE_NONE && editor->number_input == 0) {
			// TODO: Move on camera view's relation to the global axis

			float3 axis = { 0 };
			if (editor->axis_mode == AXIS_MODE_X)
				axis = float3_dot(FLOAT3_X, camera_right) > 0 ? FLOAT3_X : float3_negate(FLOAT3_X);
			else if (editor->axis_mode == AXIS_MODE_Z)
				axis = float3_dot(FLOAT3_Z, camera_up) > 0 ? float3_negate(FLOAT3_Z) : FLOAT3_Z;
			else if (editor->axis_mode == AXIS_MODE_Y)
				axis = FLOAT3_Y;
			float3 move_x = float3_scale(axis, offset.x * units_per_pixel.x);
			float3 move_y = float3_scale(axis, offset.y * units_per_pixel.y);

			transform->position = float3_add(
				float3_add(editor->cached_selection_transform.position, move_x),
				move_y);
		} else if (editor->number_input || editor->axis_reverse) {
			if (input_key_pressed(KEY_CODE_BACKSPACE))
				editor->number_input /= 10;

			float3 move = { 0 };
			float scale = editor->number_input * (editor->axis_reverse ? -1.f : 1.f);
			if (editor->axis_mode == AXIS_MODE_X)
				move = float3_scale(FLOAT3_X, scale);
			else if (editor->axis_mode == AXIS_MODE_Z)
				move = float3_scale(FLOAT3_Z, scale);
			else if (editor->axis_mode == AXIS_MODE_Y)
				move = float3_scale(FLOAT3_Y, scale);
			transform->position = float3_add(editor->cached_selection_transform.position, move);
		}

		float4x4 translation = float4x4_translation(transform->position);
		float4x4 scale = float4x4_scaling(transform->scale);
		float4x4 rotation = float4x4_identity();

		transform->global = float4x4_multiply(translation, float4x4_multiply(rotation, scale));

		if (input_mouse_pressed(MOUSE_BUTTON_LEFT) || input_key_pressed(KEY_CODE_ENTER)) {
			editor->axis_mode = AXIS_MODE_NONE;
			editor->axis_reverse = false;
			editor->grab = false;
			editor->number_input = 0;
		}
		if (input_mouse_pressed(MOUSE_BUTTON_RIGHT) || input_key_pressed(KEY_CODE_ESCAPE)) {
			editor->axis_mode = AXIS_MODE_NONE;
			editor->axis_reverse = false;
			editor->grab = false;
			editor->number_input = 0;

			*transform = editor->cached_selection_transform;
		}
	} else {
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

	RhiUniformSet global_set = vulkan_uniformset_push(pstate->context, pstate->line_shader, 0);
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

			TransformComponent transform = pstate->transforms[entity];
			MeshComponent *mesh_component = &pstate->meshes[entity];

			if (mesh_component->mesh_group_index == 0 || mesh_component->mesh_group_index > arena_array_count(pstate->assets.mesh_groups))
				continue;
			uint32x2 mesh_group = pstate->assets.mesh_groups[mesh_component->mesh_group_index];

			vulkan_push_constants(pstate->context, sizeof(float4x4), sizeof(uint32_t), &entity);

			for (uint32_t mesh_index = mesh_group.x; mesh_index < mesh_group.x + mesh_group.y; ++mesh_index) {
				Mesh *mesh = &pstate->assets.meshes[mesh_index];
				vulkan_push_constants(pstate->context, 0, sizeof(float4x4), &transform.global);

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
		vulkan_shader_bind(pstate->context, pstate->line_shader, pipeline);

		if (pstate->selected_entity) {
			TransformComponent *transform = &pstate->transforms[pstate->selected_entity];
			MeshComponent *mesh_component = &pstate->meshes[pstate->selected_entity];

			ASSERT(mesh_component->mesh_group_index || mesh_component->mesh_group_index < arena_array_count(pstate->assets.mesh_groups));
			uint32x2 mesh_group = pstate->assets.mesh_groups[mesh_component->mesh_group_index];

			uint32_t line_segment_count = 12 * mesh_group.y;
			size_t line_segment_size = 2 * sizeof(float4);
			size_t size = line_segment_count * line_segment_size;

			RhiBuffer buffer = pstate->frame_storage_buffer;
			size_t offset = vulkan_buffer_push(pstate->context, buffer, size, NULL);

			for (uint32_t mesh_index = mesh_group.x, index = 0; mesh_index < mesh_group.x + mesh_group.y; ++mesh_index, ++index) {
				Mesh *mesh = &pstate->assets.meshes[mesh_index];
				Interval3 *bounds = &pstate->assets.bounds[mesh_index];

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

			vulkan_push_constants(pstate->context, 0, sizeof(float4x4), transform->global.elements);

			RhiUniformSet group = vulkan_uniformset_push(pstate->context, pstate->line_shader, 1);
			vulkan_uniformset_bind_buffer_range(pstate->context, group, 0, offset, size, buffer);
			vulkan_uniformset_bind(pstate->context, group);

			vulkan_renderer_draw(pstate->context, line_segment_count * 2 * 6);
		}

		vulkan_drawlist_end(pstate->context);
	}

	pstate->previous_target = pstate->current_target;
	pstate->current_target = (pstate->current_target + 1) % countof(pstate->main_color_targets);
}
