#include "assets/asset_types.h"
#include "assets/importer.h"
#include "common.h"
#include "core/arena.h"
#include "core/cmath.h"
#include "core/debug.h"
#include "core/logger.h"
#include "core/r_types.h"
#include "game_interface.h"
#include "input.h"
#include "input/input_types.h"
#include "platform.h"
#include "renderer/backend/vulkan_api.h"
#include "renderer/r_internal.h"
#include "scene.h"

#include "cgltf.h"
#include <math.h>

typedef struct Editor {
	float sensitivity, pan_speed;
	Camera camera;
} Editor;

Editor editor_make(void);
void editor_update(Editor *editor, float dt);

typedef enum {
	GAME_STATE_PLAY,
	GAME_STATE_EDITOR,
} GameState;

typedef struct {
	bool initialized;

	RhiBuffer global_uniform_buffer;
	RhiShader pbr_shader;

	RhiBuffer quad_geometry;

	RhiBuffer model;

	GameState state;
	Editor editor;
	Camera game_camera;

	Camera *camera;
} PermanentState;

FrameInfo update_and_render(GameContext *context, float dt) {
	PermanentState *pstate = context->permanent_memory;

	if (pstate->initialized == false) {
		pstate->global_uniform_buffer = vulkan_buffer_make(context->render, BUFFER_TYPE_UNIFORM, BUFFER_MEMORY_SHARED, sizeof(Matrix4f) * 2, NULL);

		ArenaTemp scratch = arena_scratch_begin(NULL);

		pstate->pbr_shader =
			vulkan_shader_make(
				NULL,
				context->render,
				importer_load_shader(scratch.arena, S("assets/shaders/pbr.vert.spv"), S("assets/shaders/pbr.frag.spv")),
				NULL);

		float3 vertices[] = {
			{ -1.0f, 1.0f, 0.0f },
			{ -1.0f, -1.0f, 0.0f },
			{ 1.0f, 1.0f, 0.0f },

			{ 1.0f, 1.0f, 0.0f },
			{ -1.0f, -1.0f, 0.0f },
			{ 1.0f, -1.0f, 0.0f }
		};

		pstate->quad_geometry = vulkan_buffer_make(context->render, BUFFER_TYPE_VERTEX, BUFFER_MEMORY_DEVICE, sizeof(vertices), vertices);

		ModelSource model_src = importer_load_gltf(scratch.arena, S("assets/models/custom/room.glb"));

		pstate->game_camera = (Camera){
			.position = { 0.0f, 0.0f, -5.0f },
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
	if (vulkan_frame_begin(context->render, window_size.x, window_size.y)) {
		Camera *camera = pstate->camera;

		Matrix4f projection = float44_perspective(DEG2RAD(camera->fov), (float)window_size.x / (float)window_size.y, 0.01f, 1000.f);
		Matrix4f view = float44_lookat(camera->position, camera->target, camera->up);

		vulkan_buffer_write(context->render, pstate->global_uniform_buffer, 0, sizeof(Matrix4f), &projection);
		vulkan_buffer_write(context->render, pstate->global_uniform_buffer, sizeof(Matrix4f), sizeof(Matrix4f), &view);

		RhiUniformSet global_set = vulkan_uniformset_push(context->render, pstate->pbr_shader, 0);
		vulkan_uniformset_bind_buffer(context->render, global_set, 0, pstate->global_uniform_buffer);

		DrawListDesc desc = {
			.color_attachments[0] = {
			  .clear = { .color = { 1.0f, 1.0f, 1.0f, 1.0f } },
			  .load = CLEAR,
			  .store = STORE,
			},
			.color_attachment_count = 1,
		};

		if (vulkan_drawlist_begin(context->render, desc)) {
			vulkan_shader_bind(context->render, pstate->pbr_shader, DEFAULT_PIPELINE);
			vulkan_uniformset_bind(context->render, global_set);

			Matrix4f transform = float44_identity();
			vulkan_push_constants(context->render, 0, sizeof(Matrix4f), &transform);
			vulkan_buffer_bind(context->render, pstate->quad_geometry, 0);
			vulkan_renderer_draw(context->render, 6);

			vulkan_drawlist_end(context->render);
		}

		vulkan_frame_end(context->render);
	}

	return (FrameInfo){ 0 };
}

GameInterface game_hookup(void) {
	GameInterface interface = (GameInterface){
		.on_update = update_and_render,
	};
	return interface;
}

ModelSource importer_load_gltf(Arena *arena, String path) {
	cgltf_options options = { 0 };
	cgltf_data *data = NULL;
	cgltf_result cgltf_result = cgltf_parse_file(&options, path.chars, &data);

	if (cgltf_result == cgltf_result_success)
		cgltf_result = cgltf_load_buffers(&options, data, path.chars);

	if (cgltf_result == cgltf_result_success)
		cgltf_result = cgltf_validate(data);

	if (cgltf_result != cgltf_result_success) {
		LOG_ERROR("Importer: Failed to load model");
		return (ModelSource){ 0 };
	}

	LOG_INFO("Loading %.*s", SARG(path));

	ModelSource result = { 0 };

	ArenaTemp scratch = arena_scratch_begin(NULL);

	float *vertices = NULL;
	uint32_t *indices = NULL;

	for (uint32_t mesh_index = 0; mesh_index < data->meshes_count; ++mesh_index) {
		cgltf_mesh *mesh = &data->meshes[mesh_index];
		LOG_INFO("Mesh '%s' (primitive_count = %d)", mesh->name, mesh->primitives_count);

		for (uint32_t primitive_index = 0; primitive_index < mesh->primitives_count; ++primitive_index) {
			cgltf_primitive *primitive = &mesh->primitives[primitive_index];

			size_t primitive_size = 0;
			for (uint32_t attribute_index = 0; attribute_index < primitive->attributes_count; ++attribute_index) {
				cgltf_attribute *attribute = &primitive->attributes[attribute_index];
				cgltf_accessor *accessor = attribute->data;

				size_t count = accessor->count * cgltf_num_components(accessor->type) * cgltf_component_size(accessor->component_type);
				vertices = arena_array_ensure(scratch.arena, vertices, 1, count);
				cgltf_accessor_unpack_floats(accessor, vertices + arena_array_count(vertices), accessor->count);
				HEADER(vertices, ArenaArrayHeader)->count += count;
			}

			cgltf_accessor *accessor = primitive->indices;

			indices = arena_array_ensure(scratch.arena, indices, sizeof(uint32_t), accessor->count);
			cgltf_accessor_unpack_indices(primitive->indices, indices + arena_array_count(indices), 4, accessor->count);
			HEADER(indices, ArenaArrayHeader)->count += accessor->count;
		}
	}

	arena_scratch_end(scratch);
	cgltf_free(data);

	return result;
}

Editor editor_make(void) {
	Editor result = {
		.pan_speed = 0.005f,
		.sensitivity = 0.005f,
		.camera = {
		  .position = { 0.0f, 0.0f, 5.0f },
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

		float3 camera_target_offset = float3_subtract(camera->target, camera->position);
		float3 camera_forward = float3_normalize(camera_target_offset);

		float3 camera_right = float3_cross(camera->up, camera_forward);
		float3 camera_up = float3_cross(camera_right, camera_forward);

		camera->position = float3_add(camera->position, float3_scale(camera_right, shift.x));
		camera->position = float3_add(camera->position, float3_scale(camera_up, shift.y));

		camera->target = float3_add(camera->position, camera_target_offset);

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

		current_theta = CLAMP(current_theta + pitch_delta, EPSILON, C_PIf - EPSILON);
		current_azimuth += yaw_delta;

		// Apply move
		camera->position = float3_add(
			(float3){
			  .x = r * sinf(current_theta) * cosf(current_azimuth),
			  .y = r * cosf(current_theta),
			  .z = r * sinf(current_theta) * sinf(current_azimuth),
			},
			camera->target);

	} else {
	}
}
