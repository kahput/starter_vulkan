#include "core/r_types.h"
#include "input/input_types.h"
#include "platform.h"

#include "event.h"
#include "events/platform_events.h"

#include "input.h"

#include "assets.h"
#include "assets/asset_types.h"

#include "renderer.h"

#include "common.h"
#include "core/logger.h"
#include "core/astring.h"
#include "allocators/arena.h"
#include "core/identifiers.h"

#include <cglm/cglm.h>

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
RShader create_shader(String name, size_t ubo_size, void *ubo_data);
UUID create_plane_mesh(Arena *arena, uint32_t subdivide_width, uint32_t subdivide_depth, Orientation orientation, MeshSource **out_mesh);

static struct State {
	Arena permanent_arena, frame_arena;
	Platform display;

	Camera editor_camera;

	Layer layers[1];
	Layer *current_layer;

	uint64_t start_time;
} state;

int main(void) {
	state.permanent_arena = arena_create(MiB(512));
	state.frame_arena = arena_create(MiB(4));

	logger_set_level(LOG_LEVEL_DEBUG);

	event_system_startup();
	input_system_startup();
	asset_library_startup(arena_push_zero(&state.permanent_arena, MiB(128), 1), MiB(128));

	// TODO: Rework platform layer when starting on Windows
	platform_startup(&state.permanent_arena, 1280, 720, "Starter Vulkan", &state.display);
	renderer_system_startup(arena_push_zero(&state.permanent_arena, MiB(16), 1), MiB(16), &state.display, state.display.physical_width, state.display.physical_height);

	state.layers[0] = (Layer){ .update = editor_update };
	state.current_layer = &state.layers[0];

	platform_pointer_mode(&state.display, PLATFORM_POINTER_DISABLED);
	state.start_time = platform_time_ms(&state.display);
	event_subscribe(SV_EVENT_WINDOW_RESIZED, resize_event);

	state.editor_camera = (Camera){
		.position = { 0.0f, 15.0f, -27.f },
		.target = { 0.0f, 0.0f, 0.0f },
		.up = { 0.0f, 1.0f, 0.0f },
		.fov = 45.f,
		.projection = CAMERA_PROJECTION_PERSPECTIVE
	};

	float delta_time = 0.0f;
	float last_frame = 0.0f;

	asset_library_track_directory(S("assets"));
	RShader sprite_shader = create_shader(S("sprite.glsl"), sizeof(vec4), (vec4){ 1.0f, 1.0f, 1.0f, 1.0f });

	ArenaTemp scratch = arena_scratch(NULL);
	MeshSource *plane_src = NULL;
	UUID plane_id = create_plane_mesh(scratch.arena, 0, 0, ORIENTATION_Z, &plane_src);
	MeshConfig mconfig = {
		.vertices = plane_src->vertices,
		.vertex_size = (sizeof *plane_src->vertices),
		.vertex_count = plane_src->vertex_count,
		.indices = plane_src->indices,
		.index_size = plane_src->index_size,
		.index_count = plane_src->index_count,
	};
	RMesh plane = renderer_mesh_create(plane_id, &mconfig);
	arena_release_scratch(scratch);

	ImageSource *sprite_src = NULL;

	// Sprite 0
	UUID sprite_id = asset_library_request_image(S("tile_0085.png"), &sprite_src);
	TextureConfig sprite_tex = { .pixels = sprite_src->pixels, .width = sprite_src->width, .height = sprite_src->height, .channels = sprite_src->channels, .is_srgb = true };
	renderer_texture_create(sprite_id, &sprite_tex);

	Handle mat_instance = renderer_material_create(sprite_shader, 0, NULL);
	renderer_material_set_texture(mat_instance, S("u_texture"), sprite_id);

	// Sprite 1
	UUID sprite_id_1 = asset_library_request_image(S("tile_0086.png"), &sprite_src);
	sprite_tex = (TextureConfig){ .pixels = sprite_src->pixels, .width = sprite_src->width, .height = sprite_src->height, .channels = sprite_src->channels, .is_srgb = true };
	renderer_texture_create(sprite_id_1, &sprite_tex);

	Handle mat_instance2 = renderer_material_create(sprite_shader, 0, NULL);
	renderer_material_set_texture(mat_instance2, S("u_texture"), sprite_id_1);

	// glTF Mesh
	ModelSource *model_src = NULL;
	Arena unique_completely_new_memory = arena_create(MiB(512));
	UUID model_id = asset_library_load_model(&unique_completely_new_memory, S("room-large.glb"), &model_src, true);

	RMesh large_room = INVALID_HANDLE;
	RMaterial large_room_mat = INVALID_HANDLE;
	if (model_src) {
		mconfig = (MeshConfig){
			.vertices = model_src->meshes[0].vertices,
			.vertex_size = sizeof(Vertex),
			.vertex_count = model_src->meshes[0].vertex_count,
			.indices = model_src->meshes[0].indices,
			.index_size = model_src->meshes[0].index_size,
			.index_count = model_src->meshes[0].index_count,
		};

		LOG_INFO("Model loaded: %d meshes, %d materials, %d images",
			model_src->mesh_count,
			model_src->material_count,
			model_src->image_count);
		large_room = renderer_mesh_create(model_id, &mconfig);
		TextureConfig config = { .pixels = model_src->images->pixels, .width = model_src->images->width, .height = model_src->images->height, .channels = model_src->images->channels, .is_srgb = true };

		renderer_texture_create(model_src->images->id, &config);

		large_room_mat = renderer_material_create(renderer_shader_default(), 0, NULL);
		renderer_material_set_texture(large_room_mat, model_src->materials->properties[0].name, model_src->images->id);
	}

	UUID current_texture = sprite_id_1;

	float timer_accumulator = 0.0f;
	uint32_t frames = 0;

	float sun_theta = 2 * GLM_PI / 3.f;
	float sun_azimuth = 0;

	Light lights[] = {
		[0] = { .type = LIGHT_TYPE_DIRECTIONAL, .color = { 0.2f, 0.2f, 1.0f, 0.1f }, .as.direction = { 0.0f, 0.0f, 0.0f } },
		[1] = { .type = LIGHT_TYPE_POINT, .color = { 1.0f, 0.5f, 0.2f, 0.8f }, .as.position = { 0.0f, 3.0f, 1.0f } }
	};
	RShader light_shader = create_shader(S("light_debug.glsl"), 0, NULL);
	ShaderParameter light_parameters[] = { [0] = { .name = S("material"), .type = SHADER_PARAMETER_TYPE_STRUCT, .as.vec4f = { 1.0f, 1.0f, 1.0f, 1.0f } } };
	RMaterial light_mat = renderer_material_create(light_shader, countof(light_parameters), light_parameters);

	while (platform_should_close(&state.display) == false) {
		float time = platform_time_seconds(&state.display);
		delta_time = time - last_frame;
		last_frame = time;
		delta_time = max(delta_time, 0.0016f);

		platform_poll_events(&state.display);

		timer_accumulator += delta_time;
		frames++;

		if (timer_accumulator >= 1.0f) {
			LOG_INFO("FPS: %d", frames);

			frames = 0;
			timer_accumulator = 0;
		}

		state.current_layer->update(delta_time);

		static float tint = 0.0f;
		tint += delta_time;
		float tint_normalized = (cos(tint) + 1.0f) * 0.5f;

		lights[0].as.direction[0] = sin(sun_theta) * cos(sun_azimuth);
		lights[0].as.direction[1] = cos(sun_theta);
		lights[0].as.direction[2] = sin(sun_theta) * sin(sun_azimuth);

		lights[1].as.position[0] = cos(time) * 5;
		lights[1].as.position[2] = sin(time) * 5;

		if (renderer_frame_begin(&state.editor_camera, countof(lights), lights)) {
			// renderer_material_set3fv(mat_instance, S("tint"), (vec3){ tint_normalized, 1.0f, 1.0f });

			mat4 transform = GLM_MAT4_IDENTITY_INIT;
			glm_translate(transform, (vec3){ 1.0f, 0.75f, 1.0f });
			glm_scale(transform, (vec3){ 1.5f, 1.5f, 1.5f });
			renderer_draw_mesh(plane, mat_instance, 0, transform);

			glm_mat4_identity(transform);
			glm_translate(transform, (vec3){ 0.0f, 0.75f, 0.0f });
			glm_scale(transform, (vec3){ 1.5f, 1.5f, 1.5f });
			renderer_draw_mesh(plane, mat_instance2, 0, transform);

			glm_mat4_identity(transform);
			glm_translate(transform, (vec3){ 0.0f, 0.0f, 0.0f });
			renderer_draw_mesh(large_room, large_room_mat, 0, transform);

			for (uint32_t index = 0; index < countof(lights); ++index) {
				if (lights[index].type == LIGHT_TYPE_DIRECTIONAL)
					continue;

				glm_mat4_identity(transform);
				glm_translate(transform, lights[index].as.position);
				renderer_material_instance_set4fv(light_mat, index + 1, S("color"), lights[index].color);
				renderer_draw_mesh(plane, light_mat, index + 1, transform);
			}
			renderer_frame_end();
		}

		static bool wireframe = false;
		if (input_key_pressed(SV_KEY_ENTER)) {
			wireframe = wireframe ? false : true;
			renderer_state_global_wireframe_set(wireframe);
		}

		if (input_key_down(SV_KEY_LEFTCTRL))
			platform_pointer_mode(&state.display, PLATFORM_POINTER_NORMAL);
		else
			platform_pointer_mode(&state.display, PLATFORM_POINTER_DISABLED);

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

	renderer_on_resize(wr_event->width, wr_event->height);
	return true;
}

UUID create_plane_mesh(Arena *arena, uint32_t subdivide_width, uint32_t subdivide_depth, Orientation orientation, MeshSource **out_mesh) {
	uint32_t rows = subdivide_width + 1, columns = subdivide_depth + 1;

	String name = string_format(arena, S("plane_%ux%u_d"), subdivide_width, subdivide_depth, orientation);
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
			Vertex orientation_y_vertex10 = { .position = { rowf + row_unit, 0, columnf }, .normal = { 0.0f, 1.0f, 0.0f }, .uv = { 1.0f, 0.0f }, .tangent = { 0.0f, 0.0f, 1.0f, 1.0f } };
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

	(*out_mesh)->indices = NULL, (*out_mesh)->index_size = 0, (*out_mesh)->index_count = 0;
	(*out_mesh)->material = NULL;

	return id;
}

RShader create_shader(String name, size_t ubo_size, void *ubo_data) {
	ShaderSource *shader_src = NULL;
	UUID shader_id = asset_library_request_shader(name, &shader_src);

	ShaderConfig shader_config = {
		.vertex_code = shader_src->vertex_shader.content,
		.vertex_code_size = shader_src->vertex_shader.size,
		.fragment_code = shader_src->fragment_shader.content,
		.fragment_code_size = shader_src->fragment_shader.size,
		.default_ubo_data = ubo_data,
		.ubo_size = ubo_size
	};

	return renderer_shader_create(shader_src->id, &shader_config);
}
