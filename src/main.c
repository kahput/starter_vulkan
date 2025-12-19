#include "core/astring.h"
#include "core/debug.h"
#include "core/identifiers.h"
#include "platform.h"

#include "assets.h"
#include "assets/asset_types.h"

#include "event.h"
#include "events/platform_events.h"

#include "input.h"

#include "renderer.h"
#include "renderer/renderer_types.h"

#include <cglm/cglm.h>
#include <cglm/mat4.h>
#include <stdalign.h>

#include "common.h"
#include "core/logger.h"

#include "allocators/arena.h"

#define GATE_FILE_PATH "assets/models/modular_dungeon/gate.glb"
#define GATE_DOOR_FILE_PATH "assets/models/modular_dungeon/gate-door.glb"
#define BOT_FILE_PATH "assets/models/characters/gdbot.glb"
#define MAGE_FILE_PATH "assets/models/characters/mage.glb"

typedef enum {
	ORIENTATION_Y,
	ORIENTATION_X,
	ORIENTATION_Z,
} Orientation;

static const float CAMERA_MOVE_SPEED = 5.f;
static const float CAMERA_SENSITIVITY = .001f;

static const float PLAYER_MOVE_SPEED = 5.f;

typedef struct layer {
	void (*update)(float dt);
} Layer;

void editor_update(float dt);
bool resize_event(Event *event);
UUID create_plane_mesh(Arena *arena, uint32_t subdivide_width, uint32_t subdivide_depth, Orientation orientation, MeshSource **out_mesh);

static struct State {
	Arena permanent_arena, frame_arena;
	Platform *display;

	Camera editor_camera;

	UUID small_room_id;

	Layer layers[1];
	Layer *current_layer;

	uint64_t start_time;
} state;

int main(void) {
	state.permanent_arena = arena_create(MiB(64));
	state.frame_arena = arena_create(MiB(4));

	logger_set_level(LOG_LEVEL_DEBUG);

	event_system_startup();
	input_system_startup();

	asset_library_startup(state.permanent_arena.memory, 0, MiB(16));
	state.permanent_arena.offset += MiB(16);

	state.display = platform_startup(&state.permanent_arena, 1280, 720, "Starter Vulkan");
	if (state.display == NULL) {
		LOG_ERROR("Platform startup failed");
		return -1;
	}

	if (renderer_create(state.permanent_arena.memory, state.permanent_arena.offset, MiB(16), state.display, state.display->physical_width, state.display->physical_height) == false) {
		LOG_ERROR("Renderer startup failed");
		return -1;
	}
	state.permanent_arena.offset += MiB(16);

	state.layers[0] = (Layer){ .update = editor_update };
	state.current_layer = &state.layers[0];

	platform_pointer_mode(state.display, PLATFORM_POINTER_DISABLED);
	state.start_time = platform_time_ms(state.display);
	event_subscribe(SV_EVENT_WINDOW_RESIZED, resize_event);

	asset_library_track_directory(SLITERAL("assets"));
	ArenaTemp scratch = arena_scratch(NULL);
	{
		ShaderSource *terrain_shader = NULL;
		UUID terrain_shader_id = asset_library_load_shader(scratch.arena, SLITERAL("pbr.glsl"), &terrain_shader);

		ShaderSource *sprite_shader = NULL;
		UUID sprite_shader_id = asset_library_load_shader(scratch.arena, SLITERAL("sprite.glsl"), &terrain_shader);

		ModelSource *model_source = NULL;
		state.small_room_id = asset_library_load_model(scratch.arena, SLITERAL("room-small.glb"), &model_source, true);
		UUID gate = asset_library_load_model(scratch.arena, SLITERAL("gate.glb"), &model_source, true);
		state.small_room_id = asset_library_load_model(scratch.arena, SLITERAL("room-small.glb"), &model_source, true);

		if (model_source) {
			renderer_upload_model(state.small_room_id, model_source);
			renderer_upload_model(state.small_room_id, model_source);
			LOG_INFO("Uploaded model: %lu", state.small_room_id);
		}

		Image *sprite_image = NULL;
		MeshSource *sprite_mesh = NULL;

		UUID sprite_image_id = asset_library_load_image(scratch.arena, SLITERAL("tile_0085.png"), &sprite_image);
		UUID sprite_mesh_id = create_plane_mesh(scratch.arena, 1, 1, ORIENTATION_Z, &sprite_mesh);

		if (sprite_image) {
			renderer_upload_image(sprite_image_id, sprite_image);
			renderer_upload_mesh(sprite_mesh_id, sprite_mesh);
		}
	}
	arena_release_scratch(scratch);

	state.editor_camera = (Camera){
		.position = { 0.0f, 1.0f, 10.0f },
		.target = { 0.0f, 1.0f, 5.0f },
		.up = { 0.0f, 1.0f, 0.0f },
		.fov = 45.f,
		.projection = CAMERA_PROJECTION_PERSPECTIVE
	};

	float delta_time = 0.0f;
	float last_frame = 0.0f;

	while (platform_should_close(state.display) == false) {
		float current_frame = (double)(platform_time_ms(state.display) - state.start_time) / 1000.0f;
		delta_time = current_frame - last_frame;
		last_frame = current_frame;

		platform_poll_events(state.display);

		state.current_layer->update(delta_time);

		if (renderer_begin_frame(&state.editor_camera)) {
			mat4 transform;
			glm_mat4_identity(transform);
			renderer_draw_model(state.small_room_id, transform);

			renderer_end_frame();
		}

		if (input_key_down(SV_KEY_LEFTCTRL))
			platform_pointer_mode(state.display, PLATFORM_POINTER_NORMAL);
		else
			platform_pointer_mode(state.display, PLATFORM_POINTER_DISABLED);

		input_system_update();
	}

	input_system_shutdown();
	event_system_shutdown();

	return 0;
}

void editor_update(float dt) {
	float yaw_delta = -input_mouse_dx() * CAMERA_SENSITIVITY;
	float pitch_delta = -input_mouse_dy() * CAMERA_SENSITIVITY;

	vec3 target_position, camera_right;

	glm_vec3_sub(state.editor_camera.target, state.editor_camera.position, target_position);
	glm_vec3_normalize(target_position);

	glm_vec3_rotate(target_position, yaw_delta, state.editor_camera.up);

	glm_vec3_cross(target_position, state.editor_camera.up, camera_right);
	glm_vec3_normalize(camera_right);

	vec3 camera_down;
	glm_vec3_negate_to(state.editor_camera.up, camera_down);

	float max_angle = glm_vec3_angle(state.editor_camera.up, target_position) - 0.001f;
	float min_angle = -glm_vec3_angle(camera_down, target_position) + 0.001f;

	pitch_delta = clamp(pitch_delta, min_angle, max_angle);

	glm_vec3_rotate(target_position, pitch_delta, camera_right);

	vec3 move = GLM_VEC3_ZERO_INIT;

	glm_vec3_cross(state.editor_camera.up, target_position, camera_right);
	glm_vec3_normalize(camera_right);

	glm_vec3_muladds(camera_right, (input_key_down(SV_KEY_D) - input_key_down(SV_KEY_A)) * CAMERA_MOVE_SPEED * dt, move);
	glm_vec3_muladds(camera_down, (input_key_down(SV_KEY_SPACE) - input_key_down(SV_KEY_C)) * CAMERA_MOVE_SPEED * dt, move);
	glm_vec3_muladds(target_position, (input_key_down(SV_KEY_S) - input_key_down(SV_KEY_W)) * CAMERA_MOVE_SPEED * dt, move);

	glm_vec3_negate(move);
	glm_vec3_add(move, state.editor_camera.position, state.editor_camera.position);
	glm_vec3_add(state.editor_camera.position, target_position, state.editor_camera.target);
}

bool resize_event(Event *event) {
	WindowResizeEvent *wr_event = (WindowResizeEvent *)event;

	renderer_resize(wr_event->width, wr_event->height);
	return true;
}

UUID create_plane_mesh(Arena *arena, uint32_t subdivide_width, uint32_t subdivide_depth, Orientation orientation, MeshSource **out_mesh) {
	uint32_t rows = subdivide_width + 1, columns = subdivide_depth + 1;

	String name = string_format(arena, SLITERAL("plane_%ux%u_d"), subdivide_width, subdivide_depth, orientation);
	UUID id = identifier_create_from_u64(string_hash64(name));

	(*out_mesh) = arena_push_struct(arena, MeshSource);

	(*out_mesh)->id = id;
	(*out_mesh)->vertices = arena_push_array_zero(arena, Vertex, rows * columns * 6);
	(*out_mesh)->vertex_count = 0;

	float row_unit = ((float)1 / rows);
	float column_unit = ((float)1 / columns);

	for (uint32_t column = 0; column < columns; ++column) {
		for (uint32_t row = 0; row < rows; ++row) {
			uint32_t index = (column * subdivide_width) + row;

			float rowf = -0.5f + (float)row * row_unit;
			float columnf = -0.5f + (float)column * column_unit;

			// TODO: Make a single set instead of three
			// Vertex v00 = { .position = { 0.0f }, .normal = { 0.0f, 1.0f, 0.0f }, .uv = { 0.0f, 0.0f }, .tangent = { 0.0f, 0.0f, 1.0f, 1.0f } };
			// Vertex v10 = { .position = { 0.0f }, .normal = { 0.0f, 1.0f, 0.0f }, .uv = { 1.0f, 0.0f }, .tangent = { 0.0f, 0.0f, 1.0f, 1.0f } };
			// Vertex v01 = { .position = { 0.0f }, .normal = { 0.0f, 1.0f, 0.0f }, .uv = { 0.0f, 1.0f }, .tangent = { 0.0f, 0.0f, 1.0f, 1.0f } };
			// Vertex v11 = { .position = { 0.0f }, .normal = { 0.0f, 1.0f, 0.0f }, .uv = { 1.0f, 1.0f }, .tangent = { 0.0f, 0.0f, 1.0f, 1.0f } };

			Vertex orientation_y_vertex00 = { .position = { rowf, 0, columnf }, .normal = { 0.0f, 1.0f, 0.0f }, .uv = { 0.0f, 0.0f }, .tangent = { 0.0f, 0.0f, 1.0f, 1.0f } };
			Vertex orientation_y_vertex10 = { .position = { rowf + row_unit, 0, columnf }, { 0.0f, 1.0f, 0.0f }, .uv = { 1.0f, 0.0f }, .tangent = { 0.0f, 0.0f, 1.0f, 1.0f } };
			Vertex orientation_y_vertex01 = { .position = { rowf, 0, columnf + column_unit }, .normal = { 0.0f, 1.0f, 0.0f }, .uv = { 0.0f, 1.0f }, .tangent = { 0.0f, 0.0f, 1.0f, 1.0f } };
			Vertex orientation_y_vertex11 = { .position = { rowf + row_unit, 0, columnf + column_unit }, .normal = { 0.0f, 1.0f, 0.0f }, .uv = { 1.0f, 1.0f }, .tangent = { 0.0f, 0.0f, 1.0f, 1.0f } };

			Vertex orientation_x_vertex00 = { .position = { 0, columnf + column_unit, rowf }, .normal = { 1.0f, 0.0f, 0.0f }, .uv = { 0.0f, 0.0f }, .tangent = { 0.0f, 0.0f, 1.0f, 1.0f } };
			Vertex orientation_x_vertex10 = { .position = { 0, columnf + column_unit, rowf + row_unit }, .normal = { 1.0f, 0.0f, 0.0f }, .uv = { 0.0f, 1.0f }, .tangent = { 0.0f, 0.0f, 1.0f, 1.0f } };
			Vertex orientation_x_vertex01 = { .position = { 0, columnf, rowf }, .normal = { 1.0f, 0.0f, 0.0f }, .uv = { 1.0f, 0.0f }, .tangent = { 0.0f, 0.0f, 1.0f, 1.0f } };
			Vertex orientation_x_vertex11 = { .position = { 0, columnf, rowf + row_unit }, .normal = { 1.0f, 0.0f, 0.0f }, .uv = { 1.0f, 1.0f }, .tangent = { 0.0f, 0.0f, 1.0f, 1.0f } };

			Vertex orientation_z_vertex00 = { .position = { rowf, columnf + column_unit, 0 }, .normal = { 0.0f, 0.0f, 1.0f }, .uv = { 0.0f, 0.0f }, .tangent = { 0.0f, 0.0f, 1.0f, 1.0f } };
			Vertex orientation_z_vertex10 = { .position = { rowf + row_unit, columnf + column_unit, 0 }, .normal = { 0.0f, 0.0f, 1.0f }, .uv = { 1.0f, 0.0f }, .tangent = { 0.0f, 0.0f, 1.0f, 1.0f } };
			Vertex orientation_z_vertex01 = { .position = { rowf, columnf, 0 }, .normal = { 0.0f, 0.0f, 1.0f }, .uv = { 0.0f, 1.0f }, .tangent = { 0.0f, 0.0f, 1.0f, 1.0f } };
			Vertex orientation_z_vertex11 = { .position = { rowf + row_unit, columnf, 0 }, .normal = { 0.0f, 0.0f, 1.0f }, .uv = { 1.0f, 1.0f }, .tangent = { 0.0f, 0.0f, 1.0f, 1.0f } };

			switch (orientation) {
				case ORIENTATION_Y: {
					(*out_mesh)->vertices[(*out_mesh)->vertex_count++] = orientation_y_vertex00;
					(*out_mesh)->vertices[(*out_mesh)->vertex_count++] = orientation_y_vertex01;
					(*out_mesh)->vertices[(*out_mesh)->vertex_count++] = orientation_y_vertex10;

					(*out_mesh)->vertices[(*out_mesh)->vertex_count++] = orientation_y_vertex10;
					(*out_mesh)->vertices[(*out_mesh)->vertex_count++] = orientation_y_vertex01;
					(*out_mesh)->vertices[(*out_mesh)->vertex_count++] = orientation_y_vertex11;
				} break;
				case ORIENTATION_X: {
					(*out_mesh)->vertices[(*out_mesh)->vertex_count++] = orientation_x_vertex00;
					(*out_mesh)->vertices[(*out_mesh)->vertex_count++] = orientation_x_vertex01;
					(*out_mesh)->vertices[(*out_mesh)->vertex_count++] = orientation_x_vertex10;

					(*out_mesh)->vertices[(*out_mesh)->vertex_count++] = orientation_x_vertex10;
					(*out_mesh)->vertices[(*out_mesh)->vertex_count++] = orientation_x_vertex01;
					(*out_mesh)->vertices[(*out_mesh)->vertex_count++] = orientation_x_vertex11;
				} break;
				case ORIENTATION_Z: {
					(*out_mesh)->vertices[(*out_mesh)->vertex_count++] = orientation_z_vertex00;
					(*out_mesh)->vertices[(*out_mesh)->vertex_count++] = orientation_z_vertex01;
					(*out_mesh)->vertices[(*out_mesh)->vertex_count++] = orientation_z_vertex10;

					(*out_mesh)->vertices[(*out_mesh)->vertex_count++] = orientation_z_vertex10;
					(*out_mesh)->vertices[(*out_mesh)->vertex_count++] = orientation_z_vertex01;
					(*out_mesh)->vertices[(*out_mesh)->vertex_count++] = orientation_z_vertex11;
				} break;
					break;
			}
		}
	}

	(*out_mesh)->indices = NULL, (*out_mesh)->index_count = 0;
	(*out_mesh)->material = NULL;

	return id;
}
