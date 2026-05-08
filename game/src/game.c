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
#include <stdint.h>

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
	UUID id;
	uint32_t start_index, count;
} MeshGroup;

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

typedef struct {
	uint32_t number_input;

	AxisMode axis;
	bool axis_reverse;

	float2 mouse_start_position;
	TransformComponent cached;
} EditorTransformInfo;

typedef struct Editor {
	float sensitivity, pan_speed, zoom_speed;
	Camera camera;

	Entity active_entity;
	Entity selected_entities[64];
	uint32_t selected_entity_count;

	TRSMode mode;
	EditorTransformInfo transform;

	bool adding;

	struct {
		TransformComponent cached_transform;
		// TODO: add redo, extend undo/redo for all changes
		/* MeshComponent cached_mesh; */
		Entity entity;
	} undo_steps[32];
	uint32_t undo_count, undo_write_cursor;
} Editor;

typedef struct {
	float2 mouse_position;
	bool mouse_down;

	int32_t hot_item;
	int32_t active_item;
} GUI;

GUI g_ui = { 0 };

typedef struct {
	Arena arena;
	bool initialized;

	VulkanContext *context;
	Window *display;

	RhiSampler linear_sampler, nearest_sampler, shadow_sampler;
	RhiTexture white;

	// These are assets too
	RhiShader shadow_shader;
	RhiShader unlit_shader, phong_shader;
	RhiShader screenline_shader, picker_shader;
	RhiShader batch_shader;

	RhiShader postfx_shader, blit_shader, composite_shader;
	// :shader

	RhiBuffer frame_uniform_buffer;
	RhiBuffer frame_storage_buffer;

	RhiBuffer scene_uniform_buffer;
	RhiBuffer scene_geometry_buffer;

	RhiTexture shadow_depth_target;
	RhiTexture main_color_targets[2];
	RhiTexture ui_color_target;

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

		MeshGroup *mesh_groups;
		Interval3 *mesh_group_bounds; // per model
	} assets;

	AssetStore store;

	Arena *scene_arena;
	size_t editor_offset;
	ECS *world;
	ECS *editor_scene;

	struct {
		bool is_initialized;
		Entity player, selection, pickaxe_pivot;

		float animation_duration, animation_elapsed;
		bool playing;
	} game;
	bool debug_draw_collisions;

	Camera *camera;
} PermanentState;

Editor editor_make(Arena *arena);
void editor_update(PermanentState *state, Editor *editor, float dt);
void editor_draw(PermanentState *state, Editor *editor);

void load_assets(PermanentState *state);

void transform_system_update(ECS *world);
void mesh_system_update(ECS *world, PermanentState *pstate);

bool window_resize(EventCode code, void *event, void *receiver) {
	WindowResizeEvent *resize_event = event;
	PermanentState *pstate = receiver;

	vulkan_renderer_on_resize(pstate->context, resize_event->width, resize_event->height);
	vulkan_texture_resize(pstate->context, pstate->picker_target, resize_event->width, resize_event->height);
	vulkan_texture_resize(pstate->context, pstate->ui_color_target, resize_event->width, resize_event->height);
	for (uint32_t index = 0; index < countof(pstate->main_color_targets); ++index)
		vulkan_texture_resize(pstate->context, pstate->main_color_targets[index], resize_event->width, resize_event->height);

	return false;
}

static float4x4 light_matrix = { 0 };
void draw_shadow_pass(PermanentState *pstate) {
	// :shadow
	Camera light_camera = {
		.position = { 0.0f, 20.0f, -30.0f },
		.up = FLOAT3_Y,
		.ortho_size = 10.f,
		.projection = CAMERA_PROJECTION_ORTHOGRAPHIC,
	};
	Camera *camera = &light_camera;

	float4x4 projection = float4x4_orthographic(
		-camera->ortho_size, camera->ortho_size,
		-camera->ortho_size, camera->ortho_size,
		0.01f, 100.f);
	projection.elements[5] *= -1;
	float4x4 view = float4x4_lookat(camera->position, camera->target, camera->up);
	light_matrix = float4x4_multiply(projection, view);

	DrawListDesc shadow_pass = {
		.name = S("shadow_pass"),
		.depth_attachment = {
		  .target = pstate->shadow_depth_target,
		  .clear.depth = 1.0f,
		  .load = CLEAR,
		  .store = STORE,
		},
		.use_depth = true,
	};

	if (vulkan_drawlist_begin(pstate->context, shadow_pass)) {
		PipelineDesc pipeline = DEFAULT_PIPELINE;
		pipeline.cull_mode = CULL_MODE_BACK;
		vulkan_shader_bind(pstate->context, pstate->shadow_shader, pipeline);

		EcsIterator iterator = ecs_query(pstate->world, ecs_type_id(TransformComponent), ecs_type_id(MeshComponent));
		Entity entity = 0;
		while ((entity = ecs_next(&iterator))) {
			TransformComponent *transform = ecs_find(pstate->world, entity, TransformComponent);
			MeshComponent *mesh_component = ecs_find(pstate->world, entity, MeshComponent);

			if (mesh_component->mesh_group_index == 0 || mesh_component->mesh_group_index > arena_array_count(pstate->assets.mesh_groups))
				continue;
			MeshGroup group = pstate->assets.mesh_groups[mesh_component->mesh_group_index];

			for (uint32_t mesh_index = group.start_index; mesh_index < group.start_index + group.count; ++mesh_index) {
				Mesh *mesh = &pstate->assets.meshes[mesh_index];

				vulkan_push_constants(pstate->context, 0, sizeof(float4x4), float4x4_multiply(projection, view).elements);
				vulkan_push_constants(pstate->context, sizeof(float4x4), sizeof(float4x4), transform->world_matrix.elements);

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
}
void draw_main_pass(PermanentState *pstate) {
	Camera *camera = pstate->camera;

	float2 window_size = float2_from_uint2(window_size_pixel(pstate->display));
	float4x4 projection = float4x4_identity();
	if (camera->projection == CAMERA_PROJECTION_PERSPECTIVE) {
		projection = float4x4_perspective(deg2radf(camera->fov), window_size.x / window_size.y, 0.01f, 1000.f);
	} else if (camera->projection == CAMERA_PROJECTION_ORTHOGRAPHIC) {
		float aspect = window_size.x / window_size.y;
		projection = float4x4_orthographic(
			-window_size.x / camera->ortho_size, window_size.x / camera->ortho_size,
			-window_size.y / camera->ortho_size, window_size.y / camera->ortho_size,
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

	LightData light = {
		.color = { 1.0f, 1.0f, 1.0f, 1.0f },
		.position = { 0.0f, 20.0f, -30.0f },
		.constant_attenuation = 1.0f,
		.light_matrix = light_matrix,

	};
	size_t light_offset = vulkan_buffer_push(pstate->context, pstate->frame_storage_buffer, sizeof(LightData), &light);

	RhiUniformSet global_set = vulkan_uniformset_push(pstate->context, pstate->phong_shader, 0);
	vulkan_uniformset_bind_buffer_range(pstate->context, global_set, 0, global_offset, sizeof(GlobalData), pstate->frame_uniform_buffer);
	vulkan_uniformset_bind_buffer_range(pstate->context, global_set, 1, light_offset, sizeof(LightData), pstate->frame_storage_buffer);

	vulkan_texture_prepare_sample(pstate->context, pstate->shadow_depth_target);
	vulkan_uniformset_bind_texture(pstate->context, global_set, 2, pstate->shadow_depth_target, pstate->shadow_sampler);

	// :main_pass
	DrawListDesc main_pass = {
		.name = S("main_pass"),
		.color_attachments[0] = {
		  .target = pstate->main_color_targets[pstate->current_target],
		  .clear.color = { 1.0f, 1.0f, 1.0f, 1.0f },
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

		Entity entity = 0;
		while ((entity = ecs_next(&iterator))) {
			TransformComponent *transform = ecs_find(pstate->world, entity, TransformComponent);
			MeshComponent *mesh_component = ecs_find(pstate->world, entity, MeshComponent);

			if (mesh_component->mesh_group_index == 0 || mesh_component->mesh_group_index > arena_array_count(pstate->assets.mesh_groups))
				continue;
			MeshGroup group = pstate->assets.mesh_groups[mesh_component->mesh_group_index];

			for (uint32_t mesh_index = group.start_index; mesh_index < group.start_index + group.count; ++mesh_index) {
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
						pstate->nearest_sampler);
				}

				vulkan_uniformset_bind(pstate->context, group);

				float4x4 model_matrix = transform->world_matrix;
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

				float4x4 model_matrix = transform->world_matrix;
				vulkan_push_constants(pstate->context, 0, sizeof(float4x4), model_matrix.elements);

				RhiUniformSet group = vulkan_uniformset_push(pstate->context, pstate->screenline_shader, 1);
				vulkan_uniformset_bind_buffer_range(pstate->context, group, 0, uniform_offset, sizeof(float4), pstate->frame_uniform_buffer);
				vulkan_uniformset_bind_buffer_range(pstate->context, group, 1, storage_offset, size, buffer);
				vulkan_uniformset_bind(pstate->context, group);

				vulkan_renderer_draw(pstate->context, (sizeof(outline) / sizeof(float4)) * 6);
			}
		}

		vulkan_drawlist_end(pstate->context);
	}
}

typedef struct {
	uint8_t r, g, b, a;
} Color;

typedef struct {
	float2 position, uv;
	uint32_t color;
	uint32_t _pad0;
} Vertex2;

static inline uint32_t color_pack(Color c) {
	return ((uint32_t)c.r) | ((uint32_t)c.g << 8) | ((uint32_t)c.b << 16) | ((uint32_t)c.a << 24);
}

void push_rectangle(Arena *arena, float x, float y, float w, float h, Color color) {
	*((uint32_t *)arena->base) += 1;

	float x0 = x;
	float y0 = y;
	float x1 = x + w;
	float y1 = y + h;

	float u0 = 0.0f;
	float v0 = 0.0f;
	float u1 = 1.0f;
	float v1 = 1.0f;

	uint32_t packed = color_pack(color);

	// clang-format off
    Vertex2 quad[] = {
        // pos      // tex
		(Vertex2){.position = {x0, y1}, .uv = {u0, v1}, .color = packed},
        (Vertex2){.position = {x1, y0}, .uv = {u1, v0}, .color = packed},
        (Vertex2){.position = {x0, y0}, .uv = {u0, v0}, .color = packed}, 

        (Vertex2){.position = {x0, y1}, .uv = {u0, v1}, .color = packed},
        (Vertex2){.position = {x1, y1}, .uv = {u1, v1}, .color = packed},
        (Vertex2){.position = {x1, y0}, .uv = {u1, v0}, .color = packed}
    };

	// clang-format on
	arena_push_copy(arena, quad, sizeof(quad), alignof(Vertex2));
}

void push_rectangle2(Arena *arena, float2 position, float2 size, Color color) {
	push_rectangle(arena, position.x, position.y, size.x, size.y, color);
}

bool point_rectangle_intersection(float2 point, float rect_x, float rect_y, float rect_w, float rect_h) {
	if (point.x < rect_x || point.x >= rect_x + rect_w ||
		point.y < rect_y || point.y >= rect_y + rect_h)
		return false;
	return true;
}

#define UI_ID(index) __LINE__ + index

void ui_begin(void) {
	g_ui.mouse_position = float2_from_double2(input_mouse_position());
	g_ui.mouse_down = input_mouse_down(MOUSE_BUTTON_LEFT);
	g_ui.hot_item = 0;
}
void ui_end(void) {
	if (g_ui.mouse_down == 0)
		g_ui.active_item = 0;
	else if (g_ui.active_item == 0)
		g_ui.active_item = -1;
}

static Arena *ui_arena = NULL;
bool ui_button(int32_t id, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
	float2 mouse = g_ui.mouse_position;

	bool hovered =
		mouse.x >= x && mouse.x < x + w &&
		mouse.y >= y && mouse.y < y + h;

	if (hovered) {
		g_ui.hot_item = id;
		if (g_ui.active_item == 0 && g_ui.mouse_down)
			g_ui.active_item = id;
	}

	push_rectangle(ui_arena, x + 8, y + 8, w, h, (Color){ 0, 0, 0, 255 });

	if (g_ui.hot_item == id) {
		if (g_ui.active_item == id) {
			push_rectangle(ui_arena, x + 2, y + 2, w, h, (Color){ 255, 255, 255, 255 });
		} else {
			push_rectangle(ui_arena, x, y, w, h, (Color){ 255, 255, 255, 255 });
		}
	} else {
		if (g_ui.active_item == id) {
			push_rectangle(ui_arena, x + 2, y + 2, w, h, (Color){ 255, 255, 255, 255 });
		} else {
			push_rectangle(ui_arena, x, y, w, h, (Color){ 170, 170, 170, 170 });
		}
	}

	if (g_ui.mouse_down == 0 &&
		g_ui.hot_item == id &&
		g_ui.active_item == id)
		return true;

	return false;
}

FrameInfo update_and_draw(GameContext *context, float dt) {
	PermanentState *pstate = context->permanent_memory;
	pstate->context = context->render;
	pstate->display = context->display;

	if (pstate->initialized == false) { // :init
		pstate->arena = arena_wrap((uint8_t *)context->permanent_memory + sizeof(PermanentState), context->permanent_memory_size - sizeof(PermanentState));

		event_subscribe(EVENT_PLATFORM_WINDOW_RESIZED, window_resize, pstate);

		pstate->frame_uniform_buffer = vulkan_buffer_make(pstate->context, BUFFER_USAGE_UNIFORM, BUFFER_MEMORY_SHARED, MiB(8), NULL);
		pstate->frame_storage_buffer = vulkan_buffer_make(pstate->context, BUFFER_USAGE_STORAGE, BUFFER_MEMORY_SHARED, MiB(32), NULL);

		pstate->scene_uniform_buffer = vulkan_buffer_make(pstate->context, BUFFER_USAGE_UNIFORM, BUFFER_MEMORY_SHARED, MiB(32), NULL);
		pstate->scene_geometry_buffer = vulkan_buffer_make(pstate->context, BUFFER_USAGE_INDEX | BUFFER_USAGE_VERTEX, BUFFER_MEMORY_DEVICE, MiB(128), NULL);

		pstate->linear_sampler = vulkan_sampler_make(pstate->context, LINEAR_SAMPLER);
		pstate->nearest_sampler = vulkan_sampler_make(pstate->context, NEAREST_SAMPLER);
		pstate->shadow_sampler = vulkan_sampler_make(pstate->context, SHADOW_SAMPLER);

		pstate->white = vulkan_texture_make(pstate->context, 1, 1, TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8, TEXTURE_USAGE_SAMPLED, &(uint32_t){ 0xffffffff });

		uint32x2 window_size = window_size_pixel(pstate->display);
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
		pstate->shadow_depth_target = vulkan_texture_make(
			pstate->context,
			1024, 1024,
			TEXTURE_TYPE_2D, TEXTURE_FORMAT_DEPTH,
			TEXTURE_USAGE_SAMPLED | TEXTURE_USAGE_RENDER_TARGET,
			NULL);
		pstate->ui_color_target = vulkan_texture_make(
			pstate->context,
			window_size.x, window_size.y,
			TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8_SRGB,
			TEXTURE_USAGE_SAMPLED | TEXTURE_USAGE_RENDER_TARGET,
			NULL);

		pstate->scene_arena = arena_partition(&pstate->arena, MiB(32));
		pstate->world = pstate->editor_scene = ecs_make(pstate->scene_arena);
		load_assets(pstate);

		// Initialize entity transforms
		transform_system_update(pstate->world);

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

	if (input_key_pressed(KEY_CODE_TAB)) {
		pstate->state = !pstate->state;
		if (pstate->state == GAME_STATE_EDITOR) {
			window_set_cursor_locked(context->display, false);
			arena_rewind(pstate->scene_arena, pstate->editor_offset);
			pstate->world = pstate->editor_scene;
			pstate->camera = &pstate->editor.camera;
		} else {
			/* window_set_cursor_locked(context->display, true); */
			pstate->camera = &pstate->game_camera;
			pstate->editor_offset = arena_mark(pstate->scene_arena);
			memory_zero(&pstate->game, sizeof(pstate->game));
			pstate->world = ecs_make_copy(pstate->scene_arena, pstate->editor_scene);
		}
	}

	ui_arena = arena_partition(scratch.arena, MiB(4));
	arena_put(ui_arena, uint32_t, 0);

	switch (pstate->state) {
		case GAME_STATE_PLAY: {
			// :game
			if (pstate->game.is_initialized == false) {
				float yaw = deg2radf(90.0f); // deg2radf(54.736f);
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
					.ortho_size = 64.0f,
					.fov = 45.f,

					.projection = CAMERA_PROJECTION_ORTHOGRAPHIC
				};
				pstate->game_camera_start_offset = pstate->game_camera.position;

				pstate->game.player = ecs_spawn(pstate->world, FLOAT3_ZERO);

				TransformComponent *player_transform = ecs_find(pstate->world, pstate->game.player, TransformComponent);
				MeshComponent cube_mesh = {
					.group_id = asset_store_find(&pstate->store, ASSET_TYPE_geometry, S("in_memory:unit_cube")),
				};
				ecs_put(pstate->world, pstate->game.player, MeshComponent, cube_mesh);

				Entity eyes[] = {
					ecs_spawn(pstate->world, (float3){ 0.25f, 0.5f, 0.5f }),
					ecs_spawn(pstate->world, (float3){ -0.25f, 0.5f, 0.5f }),
				};

				for (uint32_t index = 0; index < countof(eyes); ++index) {
					Entity e = eyes[index];

					TransformComponent *transform = ecs_find(pstate->world, e, TransformComponent);
					transform->scale = (float3){ 0.25f, 0.25f, 0.25f };
					ecs_put(pstate->world, e, MeshComponent, cube_mesh);

					ecs_hierarchy_parent(pstate->world, pstate->game.player, e);
				}

				Entity pickaxe_pivot = pstate->game.pickaxe_pivot = ecs_spawn(pstate->world, (float3){ 0.75f, 0.25f, 0.25f });
				Entity pickaxe = ecs_spawn(pstate->world, FLOAT3_ZERO);
				ecs_put(pstate->world, pickaxe, TransformComponent,
					{
					  .position = { 0.0f, -0.25f, 0.0f },
					  .scale = float3_fill(8.0f),
					  .rotation = { 0.0f, 90.0f, 0.0f },
					});

				MeshComponent pickaxe_mesh = {
					.group_id = asset_store_find(
						&pstate->store,
						ASSET_TYPE_geometry,
						S("assets/models/kenney/survival_kit/tool-pickaxe.glb")),
				};
				ecs_put(pstate->world, pickaxe, MeshComponent, pickaxe_mesh);
				ecs_hierarchy_parent(pstate->world, pickaxe_pivot, pickaxe);
				ecs_hierarchy_parent(pstate->world, pstate->game.player, pickaxe_pivot);

				/* pstate->game.selection = ecs_deserialize_entity(pstate->world, S("assets/scenes/selection_entity.prefab")); */
				Entity rock = ecs_deserialize_entity(pstate->world, S("assets/scenes/rock.prefab"));

				mesh_system_update(pstate->world, pstate);

				pstate->game.is_initialized = true;
			}

			Entity player = pstate->game.player;
			Entity selection = pstate->game.selection;

			Camera *camera = &pstate->game_camera;
			TransformComponent *transform = ecs_find(pstate->world, player, TransformComponent);

			float3 camera_target_offset = float3_subtract(camera->target, camera->position);
			float3 camera_forward = float3_normalize(camera_target_offset);

			float3 camera_right = float3_normalize(float3_cross(camera->up, camera_forward));
			float3 camera_up = float3_cross(camera_forward, camera_right);

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
			// :collision
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
						float4x4 trs = collision_transform->world_matrix;

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

					transform->position = float3_add(transform->position, float3_scale(move_delta, t_min - 0.001f));

					move_delta = float3_subtract(move_delta, float3_scale(nearest.normal, float3_dot(move_delta, nearest.normal)));
					t_remaining -= t_min;
				}
			}

			camera->target = transform->position;
			camera->position.x = pstate->game_camera_start_offset.x + transform->position.x;
			camera->position.z = pstate->game_camera_start_offset.z + transform->position.z;

			float2 mouse_position = float2_from_double2(input_mouse_position());
			float2 window_size = float2_from_uint2(window_size_pixel(context->display));

			// Calculate the positon where the camera view ray intersects world y 0

			/*
			float distance = float3_dot(float3_subtract(editor->transform.cached.position, camera->position), camera_forward);
			float fov_radians = deg2radf(camera->fov);
			float frustum_height = 2 * tanf(fov_radians * 0.5f) * distance;
			float frustum_width = frustum_height * (screen_size.x / screen_size.y);

			float2 units_per_pixel = {
				.x = frustum_width / screen_size.x,
				.y = frustum_height / screen_size.y,
			};
			*/

			float2 ndc = {
				.x = -1 * ((mouse_position.x / window_size.x) * 2.0f - 1.0f),
				.y = -1 * ((mouse_position.y / window_size.y) * 2.0f - 1.0f),
			};
			float2 extent = {
				ndc.x * (window_size.x / camera->ortho_size),
				ndc.y * (window_size.y / camera->ortho_size),
			};

			float3 forward_offset = float3_scale(camera_forward, 0.1f);
			float3 right_offset = float3_scale(camera_right, extent.x);
			float3 up_offset = float3_scale(camera_up, extent.y);

			float3 ro = float3_add(camera->position, float3_add(forward_offset, float3_add(right_offset, up_offset)));
			float3 rd = camera_forward;

			float t = -ro.y / rd.y;

			float3 mouse_floor = float3_add(ro, float3_scale(rd, t));
			float3 mouse_offset = float3_subtract(mouse_floor, transform->position);
			float3 mouse_direction = float3_normalize_safe(mouse_offset, EPSILON);

			if (float3_equal(mouse_direction, FLOAT3_ZERO) == false) {
				float rotation = -rad2degf(atan2(mouse_direction.z, mouse_direction.x));
				transform->rotation.y = rotation + 90.0f;
			}

			if (input_mouse_pressed(MOUSE_BUTTON_LEFT) && pstate->game.playing == false) {
				pstate->game.animation_duration = 0.5f;
				pstate->game.animation_elapsed = 0.0f;
				pstate->game.playing = true;
			}

			if (pstate->game.playing == true) {
				TransformComponent *pivot_transform = ecs_find(pstate->world, pstate->game.pickaxe_pivot, TransformComponent);

				float half_duration = pstate->game.animation_duration * 0.5f;

				pstate->game.animation_elapsed += dt;
				float elapsed = pstate->game.animation_elapsed;
				if (elapsed < half_duration) {
					float t = elapsed / half_duration;
					pivot_transform->rotation.x = lerpf(0.0f, 75.0f, t);
				} else if (elapsed < half_duration * 2.0f) {
					float t = (elapsed - half_duration) / half_duration;
					pivot_transform->rotation.x = lerpf(75.0f, 0.0f, t);
				} else {
					pstate->game.playing = false;
				}
			}

			// :ui game
			ui_begin();

			if (ui_button(UI_ID(0), 200, 200, 80, 80)) {
				LOG_INFO("Button clicked");
			};
			push_rectangle(ui_arena, 0.0f, 0.0f, 100.f, 100.f, (Color){ 255, 0, 0, 255 });

			ui_end();
		} break;

		case GAME_STATE_EDITOR: {
			editor_update(pstate, &pstate->editor, dt);
		} break;
	}

	// Update transforms
	transform_system_update(pstate->world);

	float2 window_size = float2_from_uint2(window_size_pixel(context->display));
	if (vulkan_frame_begin(pstate->context, window_size.x, window_size.y)) {
		// :pass
		draw_shadow_pass(pstate);
		draw_main_pass(pstate);

		// :pass
		// :ui draw
		DrawListDesc ui_pass = {
			.color_attachments[0] = {
			  .target = pstate->ui_color_target,
			  .load = CLEAR,
			  .store = STORE,
			  .clear.color = { 0.0f, 0.0f, 0.0f, 0.0f },
			},
			.color_attachment_count = 1,
		};

		if (vulkan_drawlist_begin(pstate->context, ui_pass)) {
			uint32_t draw_count = *(uint32_t *)ui_arena->base;
			if (draw_count) {
				float4x4 projection = float4x4_orthographic(0.0f, window_size.x, 0.0f, window_size.y, -50, 50.f);
				float4x4 view = float4x4_identity();
				float4x4 view_projection = float4x4_multiply(projection, view);

				PipelineDesc pipeline = DEFAULT_PIPELINE;
				pipeline.cull_mode = CULL_MODE_NONE;

				vulkan_shader_bind(pstate->context, pstate->batch_shader, pipeline);
				RhiUniformSet set0 = vulkan_uniformset_push(pstate->context, pstate->batch_shader, 0);

				size_t global_data_offset = vulkan_buffer_push(pstate->context, pstate->frame_uniform_buffer, sizeof(float4x4), view_projection.elements);
				vulkan_uniformset_bind_buffer_range(pstate->context, set0, 0, global_data_offset, sizeof(float4x4), pstate->frame_uniform_buffer);

				void *data = (uint8_t *)ui_arena->base + sizeof(uint32_t);
				size_t offset = ui_arena->offset - sizeof(uint32_t);
				size_t vertex_data_offset = vulkan_buffer_push(pstate->context, pstate->frame_storage_buffer, offset, data);
				vulkan_uniformset_bind_buffer_range(pstate->context, set0, 1, vertex_data_offset, offset, pstate->frame_storage_buffer);
				vulkan_uniformset_bind(pstate->context, set0);

				/* RhiUniformSet set1 = vulkan_uniformset_push(pstate->context, pstate->batch_shader, 1); */
				/* float4 color = { 0.0f, 0.0f, 0.0f, 1.0f }; */
				/* size_t color_offset = vulkan_buffer_push(pstate->context, pstate->frame_uniform_buffer, sizeof(float4), &color); */
				/* vulkan_uniformset_bind_buffer_range(pstate->context, set1, 0, color_offset, sizeof(float4), pstate->frame_uniform_buffer); */
				/* vulkan_uniformset_bind(pstate->context, set1); */

				vulkan_renderer_draw(pstate->context, draw_count * 6);
			}
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
		vulkan_texture_prepare_sample(pstate->context, pstate->ui_color_target);
		if (vulkan_drawlist_begin(pstate->context, present_pass)) {
			PipelineDesc pipeline = DEFAULT_PIPELINE;
			vulkan_shader_bind(pstate->context, pstate->composite_shader, pipeline);

			RhiUniformSet set0 = vulkan_uniformset_push(pstate->context, pstate->composite_shader, 0);

			/* RhiTexture output = pstate->ui_color_target; */
			/* RhiTexture output = pstate->shadow_depth_target; */

			RhiTexture layer0 = pstate->main_color_targets[pstate->previous_target];
			RhiTexture layer1 = pstate->ui_color_target;
			vulkan_uniformset_bind_texture(pstate->context, set0, 0, layer0, pstate->linear_sampler);
			vulkan_uniformset_bind_texture(pstate->context, set0, 1, layer1, pstate->linear_sampler);
			vulkan_uniformset_bind(pstate->context, set0);

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

static void editor_select_entity(Editor *editor, Entity entity, bool multi) {
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

	// :editor

	if (input_mouse_down(MOUSE_BUTTON_MIDDLE))
		window_set_cursor_locked(pstate->display, true);
	else
		window_set_cursor_locked(pstate->display, false);

	bool entity_updated = false;
	if (editor->selected_entity_count > 0) {
		// enter TRS mode
		TRSMode was = editor->mode;
		if (input_key_pressed(KEY_CODE_G))
			editor->mode = TRS_MODE_TRANSLATION;
		if (input_key_pressed(KEY_CODE_R))
			editor->mode = TRS_MODE_ROTATION;
		if (input_key_down(KEY_CODE_LEFTSHIFT) == false && input_key_pressed(KEY_CODE_S))
			editor->mode = TRS_MODE_SCALING;

		if (editor->mode && was != editor->mode) {
			editor->transform.axis = AXIS_MODE_XYZ;
			editor->transform.cached = *ecs_find(pstate->world, editor->active_entity, TransformComponent);
			editor->transform.mouse_start_position = float2_from_double2(input_mouse_position());
		}

		// enter axis constraint mode
		if (editor->mode != TRS_MODE_NONE) {
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

			if (input_key_pressed(KEY_CODE_Q)) {
				entity_mesh->mesh_group_index = entity_mesh->mesh_group_index > 1
					? entity_mesh->mesh_group_index - 1
					: group_count - 1;
				entity_mesh->group_id = pstate->assets.mesh_groups[entity_mesh->mesh_group_index].id;
			}

			if (input_key_pressed(KEY_CODE_E)) {
				entity_mesh->mesh_group_index = (entity_mesh->mesh_group_index % (group_count - 1)) + 1;
				entity_mesh->group_id = pstate->assets.mesh_groups[entity_mesh->mesh_group_index].id;
			}
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
			ecs_hierarchical_despawn(pstate->world, editor->active_entity);
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
	if (input_key_down(KEY_CODE_LEFTSHIFT) && input_key_pressed(KEY_CODE_A))
		editor_select_entity(editor, ecs_spawn(pstate->world, FLOAT3_ZERO), false);

	float2 screen_size = float2_from_uint2(window_size_pixel(pstate->display));
	float2 mouse_position = float2_from_double2(input_mouse_position());
	float2 mouse_offset = float2_negate(float2_subtract(mouse_position, editor->transform.mouse_start_position));

	float3 camera_target_offset = float3_subtract(camera->target, camera->position);
	float3 camera_forward = float3_normalize(camera_target_offset);

	float3 camera_right = float3_cross(camera->up, camera_forward);
	float3 camera_up = float3_cross(camera_right, camera_forward);

	// :trs
	switch (editor->mode) {
		case TRS_MODE_NONE: {
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

		case TRS_MODE_TRANSLATION:
		case TRS_MODE_ROTATION:
		case TRS_MODE_SCALING: { // Shared state
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
				.y = screen_size.y / frustum_height,
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

			switch (editor->mode) {
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
		editor->transform = (EditorTransformInfo){ 0 };
		editor->mode = TRS_MODE_NONE;
	}
}

void editor_draw(PermanentState *pstate, Editor *editor) {
	Camera *camera = pstate->camera;
	// :editor

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
			MeshGroup group = pstate->assets.mesh_groups[mesh_component->mesh_group_index];

			vulkan_push_constants(pstate->context, sizeof(float4x4), sizeof(uint32_t), &entity);

			for (uint32_t mesh_index = group.start_index; mesh_index < group.start_index + group.count; ++mesh_index) {
				Mesh *mesh = &pstate->assets.meshes[mesh_index];
				float4x4 model_matrix = transform->world_matrix;
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
				uint32_t count = arena_array_count(pstate->assets.mesh_groups);
				ASSERT(mesh_component->mesh_group_index && mesh_component->mesh_group_index < count);

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

			float4x4 model_matrix = transform->world_matrix;
			float4 color = entity == editor->active_entity ? (float4){ 1.0f, 0.2f, 0.2f, 1.0f } : (float4){ 1.0f, 0.5f, 0.3f, 1.0f };

			vulkan_push_constants(pstate->context, 0, sizeof(float4x4), model_matrix.elements);
			size_t uniform_offset = vulkan_buffer_push(pstate->context, pstate->frame_uniform_buffer, sizeof(float4), &color);
			size_t point_offset = vulkan_buffer_push(pstate->context, pstate->frame_storage_buffer, sizeof(selection), selection);

			RhiUniformSet group = vulkan_uniformset_push(pstate->context, pstate->screenline_shader, 1);
			vulkan_uniformset_bind_buffer_range(pstate->context, group, 0, uniform_offset, sizeof(float4), pstate->frame_uniform_buffer);
			vulkan_uniformset_bind_buffer_range(pstate->context, group, 1, point_offset, sizeof(selection), pstate->frame_storage_buffer);
			vulkan_uniformset_bind(pstate->context, group);

			vulkan_renderer_draw(pstate->context, line_segment_count * 2 * 6);
		}

		if (editor->adding) {
			uint32_t slices = 20;
			float spacing = 1.0f, thickness = 1.0f;

			ArenaTemp scratch = arena_scratch_begin(NULL);
			size_t initial_offset = arena_mark(scratch.arena);
			int32_t half_slices = slices / 2;
			for (int32_t index = -half_slices; index <= half_slices; ++index) {
				arena_put(scratch.arena, float4, { (float)index * spacing, 0.0f, (float)-half_slices * spacing, thickness });
				arena_put(scratch.arena, float4, { (float)index * spacing, 0.0f, (float)half_slices * spacing, thickness });

				arena_put(scratch.arena, float4, { (float)-half_slices * spacing, 0.0f, (float)index * spacing, thickness });
				arena_put(scratch.arena, float4, { (float)half_slices * spacing, 0.0f, (float)index * spacing, thickness });
			}
			size_t size = arena_mark(scratch.arena) - initial_offset;

			float4x4 model_matrix = float4x4_identity();
			float4 color = (float4){ 0.0f, 0.0f, 0.0f, 1.0f };

			vulkan_push_constants(pstate->context, 0, sizeof(float4x4), model_matrix.elements);
			size_t uniform_offset = vulkan_buffer_push(pstate->context, pstate->frame_uniform_buffer, sizeof(float4), &color);
			size_t point_offset = vulkan_buffer_push(pstate->context, pstate->frame_storage_buffer, size, scratch.arena->base);

			RhiUniformSet group = vulkan_uniformset_push(pstate->context, pstate->screenline_shader, 1);
			vulkan_uniformset_bind_buffer_range(pstate->context, group, 0, uniform_offset, sizeof(float4), pstate->frame_uniform_buffer);
			vulkan_uniformset_bind_buffer_range(pstate->context, group, 1, point_offset, size, pstate->frame_storage_buffer);
			vulkan_uniformset_bind(pstate->context, group);

			vulkan_renderer_draw(pstate->context, (slices + 1) * 2 * 6);

			arena_temp_end(scratch);
		}

		vulkan_drawlist_end(pstate->context);
	}

	pstate->previous_target = pstate->current_target;
	pstate->current_target = (pstate->current_target + 1) % countof(pstate->main_color_targets);
}

static inline void calculate_transforms(ECS *world, Entity entity, float4x4 parent_global) {
	TransformComponent *transform = ecs_find(world, entity, TransformComponent);
	transform->world_matrix = float4x4_multiply(parent_global, float4x4_compose(transform->position, transform->rotation, transform->scale));

	HierarchyComponent *node = ecs_find(world, entity, HierarchyComponent);
	if (node && node->first_child) {
		Entity child = node->first_child;
		while (child) {
			calculate_transforms(world, child, transform->world_matrix);
			child = ecs_find(world, child, HierarchyComponent)->next_sibling;
		}
	}
}

void transform_system_update(ECS *world) {
	EcsIterator it = ecs_query(world, COMPONENT_TYPE_TransformComponent);
	Entity entity;

	while ((entity = ecs_next(&it))) {
		HierarchyComponent *node = ecs_find(world, entity, HierarchyComponent);

		if (node == NULL || node->parent == 0)
			calculate_transforms(world, entity, float4x4_identity());
	}
}

void mesh_system_update(ECS *world, PermanentState *pstate) {
	EcsIterator it = ecs_query(world, ecs_type_id(MeshComponent));
	Entity entity;

	while ((entity = ecs_next(&it))) {
		MeshComponent *mesh = ecs_find(world, entity, MeshComponent);
		if (mesh->mesh_group_index)
			continue;

		for (uint32_t index = 0; index < arena_array_count(pstate->assets.mesh_groups); ++index) {
			MeshGroup *group = &pstate->assets.mesh_groups[index];
			if (mesh->group_id == group->id) {
				mesh->mesh_group_index = index;
				break;
			}
		}

		ASSERT(mesh->mesh_group_index != 0);
	}
}

static inline RhiShader load_shader(VulkanContext *context, String vertex, String fragment) {
	ArenaTemp scratch = arena_scratch_begin(NULL);
	String vertex_path = string_format(scratch.arena, "assets/shaders/vertex/bin/%.*s.vertex.spv", SARG(vertex));
	String fragment_path = string_format(scratch.arena, "assets/shaders/fragment/bin/%.*s.fragment.spv", SARG(fragment));
	ShaderSource source = importer_load_shader(scratch.arena, vertex_path, fragment_path);
	RhiShader result =
		vulkan_shader_make(
			NULL,
			context,
			source.vertex, source.fragment,
			NULL);

	arena_scratch_end(scratch);
	return result;
}

void load_assets(PermanentState *pstate) {
	// :assets
	ArenaTemp scratch = arena_scratch_begin(NULL);

	pstate->store = asset_store_make(&pstate->arena);
	AssetStore *store = &pstate->store;
	if (file_exists(S("assets/asset_manifest.json")))
		asset_store_deserialize(store, S("assets/asset_manifest.json"));
	else
		asset_store_track_directory(store, S("assets/"));

	UUID unlit_generated = uuid_generate();
	UUID unlit = asset_store_find(store, ASSET_TYPE_shader, S("shaders/bin/unlit.glsl"));

	UUID wall = asset_store_find(store, ASSET_TYPE_geometry, S("assets/models/kenney/modular_dungeon/room-large.glb"));

	ShaderSource source = { 0 };

	pstate->shadow_shader = load_shader(pstate->context, S("shadow"), S("blank"));

	pstate->unlit_shader = load_shader(pstate->context, S("basic"), S("unlit"));
	pstate->picker_shader = load_shader(pstate->context, S("basic"), S("picker"));

	pstate->phong_shader = load_shader(pstate->context, S("base"), S("phong"));
	pstate->screenline_shader = load_shader(pstate->context, S("line"), S("flat"));

	pstate->postfx_shader = load_shader(pstate->context, S("quad"), S("postfx"));
	pstate->blit_shader = load_shader(pstate->context, S("quad"), S("blit"));
	pstate->batch_shader = load_shader(pstate->context, S("batch"), S("vertex_color"));
	pstate->composite_shader = load_shader(pstate->context, S("quad"), S("composite"));
	// :shader

	// TODO: Import the node transforms & cache shared textures
	SceneSource models[] = {
		importer_load_gltf_scene(scratch.arena, S("assets/models/kenney/modular_dungeon/room-large.glb")),
		importer_load_gltf_scene(scratch.arena, S("assets/models/kenney/modular_dungeon/room-small.glb")),
		importer_load_gltf_scene(scratch.arena, S("assets/models/kenney/modular_dungeon/corridor.glb")),
		importer_load_gltf_scene(scratch.arena, S("assets/models/kenney/modular_dungeon/gate-door.glb")),
		importer_load_gltf_scene(scratch.arena, S("assets/models/characters/gdbot.glb")),
		importer_load_gltf_scene(scratch.arena, S("assets/models/kenney/survival_kit/rock-a.glb")),
		importer_load_gltf_scene(scratch.arena, S("assets/models/kenney/survival_kit/rock-b.glb")),
		importer_load_gltf_scene(scratch.arena, S("assets/models/kenney/survival_kit/tool-pickaxe.glb")),
		// :model
	};

	// Prep Upload
	RhiTexture *textures = NULL;
	Material *materials = NULL;

	Mesh *meshes = NULL;
	uint32_t *mesh_to_material = NULL;
	Interval3 *mesh_group_bounds = NULL;
	MeshGroup *mesh_groups = NULL;

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

		arena_darray_put(scratch.arena, mesh_groups, MeshGroup,
			{
			  .id = asset_store_find(store, ASSET_TYPE_geometry, model->path),
			  .start_index = mesh_offset,
			  .count = model->mesh_count,
			});
		arena_darray_put(scratch.arena, mesh_group_bounds, Interval3, largest);

		arena_push_copy(geometry_upload_arena, model->vertices, model->vertices_size, 1);
		arena_push_copy(geometry_upload_arena, model->indices, model->indices_size, 1);
	}

	// Add cube asset for player
	{ // This is the code necessary for pushing a single mesh resource
		MeshSource cube = mesh_source_cube(scratch.arena, 0, 0.5f, 0);
		Interval3 cube_bounds = {
			.min = { -0.5f, 0, -0.5f },
			.max = { 0.5f, 1, 0.5f },
		};

		MeshGroup group = {
			.id = asset_store_register(store, ASSET_TYPE_geometry, S("unit_cube")),
			.start_index = arena_array_count(meshes),
			.count = 1,
		};
		arena_darray_put(scratch.arena, mesh_groups, MeshGroup, group);

		Mesh *cube_mesh = arena_darray_push(scratch.arena, meshes, Mesh);
		cube_mesh->buffer = pstate->scene_geometry_buffer;
		cube_mesh->vertex_count = cube.vertex_count;
		cube_mesh->vertex_offset = geometry_upload_arena->offset;
		arena_push_copy(geometry_upload_arena, cube.vertices, cube.vertex_size * cube.vertex_count, 1);

		arena_darray_put(scratch.arena, mesh_to_material, uint32_t, 0);
		arena_darray_put(scratch.arena, mesh_group_bounds, Interval3, cube_bounds);
	}
	{
		MeshSource quad_src = mesh_source_quad3(scratch.arena, (float3){ 0.0f, 0.5f, 0.0f }, 1.0f);
		Interval3 quad_bounds = {
			.min = { -0.5f, 0.0f, -0.1f },
			.max = { 0.5f, 1.0f, 0.1f },
		};

		MeshGroup group = {
			.id = asset_store_register(store, ASSET_TYPE_geometry, S("quad")),
			.start_index = arena_array_count(meshes),
			.count = 1,
		};
		arena_darray_put(scratch.arena, mesh_groups, MeshGroup, group);

		Mesh *quad_mesh = arena_darray_push(scratch.arena, meshes, Mesh);
		quad_mesh->buffer = pstate->scene_geometry_buffer;
		quad_mesh->vertex_count = quad_src.vertex_count;
		quad_mesh->vertex_offset = geometry_upload_arena->offset;
		arena_push_copy(geometry_upload_arena, quad_src.vertices, quad_src.vertex_size * quad_src.vertex_count, 1);

		arena_darray_put(scratch.arena, mesh_to_material, uint32_t, 0);
		arena_darray_put(scratch.arena, mesh_group_bounds, Interval3, quad_bounds);
	}
	// :generated

	pstate->assets.textures = arena_array_copy(&pstate->arena, textures, RhiTexture);
	pstate->assets.meshes = arena_array_copy(&pstate->arena, meshes, Mesh);
	pstate->assets.materials = arena_array_copy(&pstate->arena, materials, Material);
	pstate->assets.mesh_to_material = arena_array_copy(&pstate->arena, mesh_to_material, uint32_t);
	pstate->assets.mesh_group_bounds = arena_array_copy(&pstate->arena, mesh_group_bounds, Interval3);
	pstate->assets.mesh_groups = arena_array_copy(&pstate->arena, mesh_groups, MeshGroup);

	// Upload all geometry once
	vulkan_buffer_push(pstate->context, pstate->scene_geometry_buffer, geometry_upload_arena->offset, geometry_upload_arena->base);

	asset_store_serialize(store, S("assets/asset_manifest.json"));
	arena_scratch_end(scratch);
}
