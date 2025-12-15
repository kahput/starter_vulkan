#include "assets.h"
#include "core/astring.h"
#include "platform.h"

#include "event.h"
#include "events/platform_events.h"

#include "input.h"

#include "assets/importer.h"

#include "renderer/renderer_types.h"
#include "renderer/vk_renderer.h"

#include <cglm/cglm.h>

#include "common.h"
#include "core/logger.h"

#include "allocators/arena.h"

#define MAX_MODEL_MESHES 32
#define MAX_MODEL_MATERIALS 8
#define MAX_MODEL_TEXTURES 8
#define MAX_MODELS 8

#define GATE_FILE_PATH "assets/models/modular_dungeon/gate.glb"
#define GATE_DOOR_FILE_PATH "assets/models/modular_dungeon/gate-door.glb"
#define BOT_FILE_PATH "assets/models/characters/gdbot.glb"
#define MAGE_FILE_PATH "assets/models/characters/mage.glb"

bool resize_event(Event *event);

typedef enum {
	ORIENTATION_Y,
	ORIENTATION_X,
	ORIENTATION_Z,
} Orientation;

static const float CAMERA_MOVE_SPEED = 5.f;
static const float CAMERA_SENSITIVITY = .001f;

static const float PLAYER_MOVE_SPEED = 5.f;

typedef enum {
	CAMERA_PROJECTION_PERSPECTIVE = 0,
	CAMERA_PROJECTION_ORTHOGRAPHIC
} CameraProjection;

typedef struct camera {
	vec3 position, target, up;
	float fov;

	CameraProjection projection;
} Camera;

typedef struct model {
	Mesh meshes[MAX_MODEL_MESHES];
	uint32_t mesh_count;

	MaterialInstance materials[MAX_MODEL_MATERIALS];
	uint32_t material_count;

	Texture textures[MAX_MODEL_TEXTURES];
	uint32_t texture_count;
} Model;

typedef struct Sprite {
	MaterialInstance material;

	Texture texture;
} Sprite;

typedef struct layer {
	void (*update)(float dt);
} Layer;

bool upload_scene(SceneAsset *asset);

void editor_update(float dt);
void game_update(float dt);

bool create_default_material_instance(void);
bool create_plane_mesh(Mesh *mesh, uint32_t subdivide_width, uint32_t subdivide_depth, Orientation orientation);

static struct State {
	Arena permanent_arena, frame_arena, load_arena;

	VulkanContext *ctx;
	Platform *display;

	uint32_t scene_uniform_buffer;
	Mesh quad_mesh;

	uint32_t default_sampler, sprite_sampler;

	uint32_t default_texture_white;
	uint32_t default_texture_black;
	uint32_t default_texture_normal;
	uint32_t default_texture_offset;

	Model models[MAX_MODELS];
	uint32_t model_count;

	Material model_material, sprite_material;

	Camera editor_camera, world_camera;
	Camera *current_camera;

	enum {
		EDITOR_LAYER,
		GAME_LAYER,
		MAX_LAYER
	} L;
	Layer layers[MAX_LAYER];

	Layer *current_layer;

	vec3 sprite_position;

	MaterialParameters default_parameters;
	MaterialInstance default_material_instance;

	uint32_t buffer_count, texture_count, set_count, sampler_count;

	uint64_t start_time;
} state;

int main(void) {
	state.permanent_arena = arena_create(MiB(64));
	state.frame_arena = arena_create(MiB(4));
	state.load_arena = arena_create(MiB(128));

	logger_set_level(LOG_LEVEL_DEBUG);

	event_system_startup();
	input_system_startup();

	state.display = platform_startup(&state.permanent_arena, 1280, 720, "Starter Vulkan");
	if (state.display == NULL) {
		LOG_ERROR("Platform startup failed");
		return -1;
	}

	state.layers[EDITOR_LAYER] = (Layer){
		.update = editor_update,
	};

	state.layers[GAME_LAYER] = (Layer){
		.update = game_update,
	};

	platform_pointer_mode(state.display, PLATFORM_POINTER_DISABLED);

	state.start_time = platform_time_ms(state.display);
	event_subscribe(SV_EVENT_WINDOW_RESIZED, resize_event);

	vulkan_renderer_create(&state.permanent_arena, &state.ctx, state.display);

	// ========================= DEFAULT ======================================
	state.scene_uniform_buffer = state.buffer_count++;
	vulkan_renderer_create_buffer(state.ctx, state.scene_uniform_buffer, BUFFER_TYPE_UNIFORM, sizeof(SceneData), NULL);

	state.model_material = (Material){ .shader = 0, .pipeline = 0 };
	vulkan_renderer_create_shader(state.ctx, state.model_material.shader, SLITERAL("./assets/shaders/vs_terrain.spv"), SLITERAL("./assets/shaders/fs_terrain.spv"));
	PipelineDesc model_pipeline = DEFAULT_PIPELINE(state.model_material.shader);
	// model_pipeline.polygon_mode = POLYGON_MODE_LINE;
	vulkan_renderer_create_pipeline(state.ctx, state.model_material.pipeline, model_pipeline);

	state.sprite_material = (Material){ .shader = 1, .pipeline = 1 };
	vulkan_renderer_create_shader(state.ctx, state.sprite_material.shader, SLITERAL("./assets/shaders/vs_sprite.spv"), SLITERAL("./assets/shaders/fs_sprite.spv"));
	PipelineDesc sprite_pipeline = DEFAULT_PIPELINE(state.sprite_material.shader);
	sprite_pipeline.cull_mode = CULL_MODE_NONE;
	vulkan_renderer_create_pipeline(state.ctx, state.sprite_material.pipeline, sprite_pipeline);

	state.default_texture_white = state.texture_count++;
	uint8_t WHITE[4] = { 255, 255, 255, 255 };
	vulkan_renderer_create_texture(state.ctx, state.default_texture_white, 1, 1, 4, WHITE);

	state.default_texture_black = state.texture_count++;
	uint8_t BLACK[4] = { 0, 0, 0, 255 };
	vulkan_renderer_create_texture(state.ctx, state.default_texture_black, 1, 1, 4, BLACK);

	state.default_texture_normal = state.texture_count++;
	uint8_t FLAT_NORMAL[4] = { 128, 128, 255, 255 };
	vulkan_renderer_create_texture(state.ctx, state.default_texture_normal, 1, 1, 4, FLAT_NORMAL);

	state.default_texture_offset = state.texture_count;
	state.default_sampler = state.sampler_count++;
	vulkan_renderer_create_sampler(state.ctx, state.default_sampler, DEFAULT_SAMPLER);

	state.sprite_sampler = state.sampler_count++;
	vulkan_renderer_create_sampler(state.ctx, state.sprite_sampler, SPRITE_SAMPLER);

	uint32_t terrain_set = state.set_count++;
	vulkan_renderer_create_resource_set(state.ctx, terrain_set, state.model_material.shader, SHADER_UNIFORM_FREQUENCY_PER_FRAME);
	vulkan_renderer_update_resource_set_buffer(state.ctx, terrain_set, "u_scene", state.scene_uniform_buffer);

	uint32_t sprite_set = state.set_count++;
	vulkan_renderer_create_resource_set(state.ctx, sprite_set, state.sprite_material.shader, SHADER_UNIFORM_FREQUENCY_PER_FRAME);
	vulkan_renderer_update_resource_set_buffer(state.ctx, sprite_set, "u_scene", state.scene_uniform_buffer);

	create_default_material_instance();

	create_plane_mesh(&state.quad_mesh, 0, 0, ORIENTATION_Z);

	Mesh floor = { 0 };
	create_plane_mesh(&floor, 10, 10, ORIENTATION_Y);
	floor.material = &state.default_material_instance;

	AssetLibrary library = {
		.arena = arena_create(MiB(4))
	};
	asset_library_track_file(&library, SLITERAL(BOT_FILE_PATH));

	// asset_library_track_directory(&library, SLITERAL("assets/models/modular_dungeon"));

	/*
	 * ================================EXPLICT================================
	 * AssetLibrary assets = { 0 }
	 * asset_library_scan_directory(&assets, "assets/models/modular_dungeon");
	 *
	 * MeshSource *source = asset_library_fetch_mesh(&assets, "gate");
	 * TextureSource *source = asset_library_fetch_image(&assets, "colormap");
	 * Mesh *mesh = renderer_upload_mesh(source);
	 *
	 * ...
	 * renderer_submit(mesh);
	 *
	 * ================================IMPLICT================================
	 *
	 * AssetLibrary assets = { 0 };
	 * asset_library_process_directory(&assets, "assets/models/modular_dungeon");
	 *
	 * MeshID mesh = asset_library_fetch_mesh(&assets, "gate");
	 *
	 * ...
	 * // Uploads if not already uploaded here
	 * renderer_submit(mesh);
	 */

	ArenaTemp temp = arena_begin_temp(&state.load_arena);
	SceneAsset *scene = importer_load_gltf(&state.load_arena, GATE_DOOR_FILE_PATH);
	upload_scene(scene);
	arena_end_temp(temp);

	temp = arena_begin_temp(&state.load_arena);
	TextureSource *sprite_texture = importer_load_image(temp.arena, "assets/sprites/kenney/tile_0085.png");
	Sprite sprite = {
		.material = { .material = state.sprite_material, .parameter_uniform_buffer = state.scene_uniform_buffer, .resource_set = state.set_count++ },
		.texture = { .texture = state.texture_count++, .sampler = state.sprite_sampler }
	};
	vulkan_renderer_create_texture(state.ctx, sprite.texture.texture, sprite_texture->width, sprite_texture->height, sprite_texture->channels, sprite_texture->pixels);
	arena_end_temp(temp);

	vulkan_renderer_create_resource_set(state.ctx, sprite.material.resource_set, state.sprite_material.shader, SHADER_UNIFORM_FREQUENCY_PER_MATERIAL);
	vulkan_renderer_update_resource_set_texture_sampler(state.ctx, sprite.material.resource_set, "u_texture", sprite.texture.texture, sprite.texture.sampler);

	state.editor_camera = (Camera){
		.position = { 0.0f, 1.0f, 10.0f },
		.target = { 0.0f, 1.0f, 5.0f },
		.up = { 0.0f, 1.0f, 0.0f },
		.fov = 45.f,
		.projection = CAMERA_PROJECTION_PERSPECTIVE
	};
	state.current_layer = &state.layers[EDITOR_LAYER];
	state.current_camera = &state.editor_camera;

	state.sprite_position[0] = 0.0f;
	state.sprite_position[1] = 0.5f;
	state.sprite_position[2] = 5.0f;

	state.world_camera = (Camera){
		.position = { state.sprite_position[0], state.sprite_position[1] + 2.f, state.sprite_position[2] + 5.f },
		.target = { state.sprite_position[0], state.sprite_position[1], state.sprite_position[2] },
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

		SceneData data = { 0 };
		glm_mat4_identity(data.view);
		glm_lookat(state.current_camera->position, state.current_camera->target, state.current_camera->up, data.view);

		glm_mat4_identity(data.projection);
		glm_perspective(glm_rad(state.current_camera->fov), (float)state.display->physical_width / (float)state.display->physical_height, 0.1f, 1000.f, data.projection);
		data.projection[1][1] *= -1;

		vulkan_renderer_update_buffer(state.ctx, state.scene_uniform_buffer, 0, sizeof(SceneData), &data);

		vulkan_renderer_begin_frame(state.ctx, state.display);

		vulkan_renderer_bind_pipeline(state.ctx, state.sprite_material.pipeline);
		vulkan_renderer_bind_resource_set(state.ctx, sprite_set);

		if (input_key_pressed(SV_KEY_TAB)) {
			state.current_layer = state.current_layer == &state.layers[EDITOR_LAYER] ? &state.layers[GAME_LAYER] : &state.layers[EDITOR_LAYER];
			state.current_camera = state.current_camera == &state.editor_camera ? &state.world_camera : &state.editor_camera;
		}

		// Draw Sprite
		{
			mat4 transform;
			glm_mat4_identity(transform);
			glm_translate(transform, state.sprite_position);
			vulkan_renderer_push_constants(state.ctx, state.sprite_material.shader, "push_constants", transform);

			Mesh *mesh = &state.quad_mesh;
			MaterialInstance *material = &sprite.material;

			vulkan_renderer_bind_resource_set(state.ctx, material->resource_set);

			vulkan_renderer_bind_buffer(state.ctx, mesh->vertex_buffer);
			if (mesh->index_count) {
				vulkan_renderer_bind_buffer(state.ctx, mesh->index_buffer);
				vulkan_renderer_draw_indexed(state.ctx, mesh->index_count);
			} else
				vulkan_renderer_draw(state.ctx, mesh->vertex_count);
		}

		vulkan_renderer_bind_pipeline(state.ctx, state.model_material.pipeline);
		vulkan_renderer_bind_resource_set(state.ctx, terrain_set);

		// Draw Terrain
		{
			mat4 transform;
			glm_mat4_identity(transform);
			glm_scale(transform, (vec3){ 100.0f, 0.0f, 100.0f });
			vulkan_renderer_push_constants(state.ctx, state.model_material.shader, "push_constants", transform);

			Mesh *mesh = &floor;
			MaterialInstance *material = mesh->material;

			vulkan_renderer_bind_resource_set(state.ctx, material->resource_set);

			vulkan_renderer_bind_buffer(state.ctx, mesh->vertex_buffer);
			if (mesh->index_count) {
				vulkan_renderer_bind_buffer(state.ctx, mesh->index_buffer);
				vulkan_renderer_draw_indexed(state.ctx, mesh->index_count);
			} else
				vulkan_renderer_draw(state.ctx, mesh->vertex_count);
		}

		for (uint32_t model_index = 0; model_index < state.model_count; ++model_index) {
			Model *model = &state.models[model_index];

			mat4 transform;
			glm_mat4_identity(transform);
			glm_translate(transform, (vec3){ 0.0f, 0.0f, 0.0f });
			vulkan_renderer_push_constants(state.ctx, 0, "push_constants", transform);

			for (uint32_t mesh_index = 0; mesh_index < model->mesh_count; ++mesh_index) {
				Mesh *mesh = &model->meshes[mesh_index];
				MaterialInstance *material = mesh->material;

				vulkan_renderer_bind_resource_set(state.ctx, material->resource_set);

				vulkan_renderer_bind_buffer(state.ctx, mesh->vertex_buffer);
				vulkan_renderer_bind_buffer(state.ctx, mesh->index_buffer);

				vulkan_renderer_draw_indexed(state.ctx, mesh->index_count);
			}
		}

		Vulkan_renderer_end_frame(state.ctx);

		if (input_key_down(SV_KEY_LEFTCTRL))
			platform_pointer_mode(state.display, PLATFORM_POINTER_NORMAL);
		else
			platform_pointer_mode(state.display, PLATFORM_POINTER_DISABLED);

		input_system_update();
	}

	input_system_shutdown();
	event_system_shutdown();
	vulkan_renderer_destroy(state.ctx);

	return 0;
}

bool upload_scene(SceneAsset *scene) {
	if (state.buffer_count >= MAX_BUFFERS) {
		LOG_WARN("Main: mesh slots filled, aborting upload");
		return false;
	}

	uint32_t texture_offset = state.texture_count;
	Model *dst = &state.models[state.model_count++];

	for (uint32_t texture_index = 0; texture_index < scene->texture_count; ++texture_index) {
		if (texture_index > MAX_MODEL_TEXTURES)
			assert("Model has more than max textures");
		TextureSource *src_texture = &scene->textures[texture_index];
		Texture *dst_texture = &dst->textures[dst->texture_count++];

		dst_texture->texture = state.texture_count++;
		dst_texture->sampler = state.default_sampler;

		vulkan_renderer_create_texture(state.ctx, dst_texture->texture, src_texture->width, src_texture->height, src_texture->channels, src_texture->pixels);
	}

	for (uint32_t material_index = 0; material_index < scene->material_count; ++material_index) {
		if (material_index >= MAX_MODEL_MATERIALS)
			assert("Model has more than max materials");

		MaterialSource *src_material = &scene->materials[material_index];
		MaterialInstance *dst_material = &dst->materials[dst->material_count++];

		dst_material->material = state.model_material;
		dst_material->parameter_uniform_buffer = state.buffer_count++;
		dst_material->resource_set = state.set_count++;

		vulkan_renderer_create_resource_set(state.ctx, dst_material->resource_set, state.model_material.shader, SHADER_UNIFORM_FREQUENCY_PER_MATERIAL);

		MaterialParameters parameters = {
			.base_color_factor = {
			  src_material->base_color_factor[0],
			  src_material->base_color_factor[1],
			  src_material->base_color_factor[2],
			  src_material->base_color_factor[3],
			},
			.metallic_factor = src_material->metallic_factor,
			.roughness_factor = src_material->roughness_factor,
			.emissive_factor = {
			  src_material->emissive_factor[0],
			  src_material->emissive_factor[1],
			  src_material->emissive_factor[2],
			}
		};

		vulkan_renderer_create_buffer(state.ctx, dst_material->parameter_uniform_buffer, BUFFER_TYPE_UNIFORM, sizeof(MaterialParameters), &parameters);
		vulkan_renderer_update_resource_set_buffer(state.ctx, dst_material->resource_set, "u_material", dst_material->parameter_uniform_buffer);

		if (src_material->base_color_texture) {
			uint32_t texture_index = src_material->base_color_texture - scene->textures;
			vulkan_renderer_update_resource_set_texture_sampler(state.ctx, dst_material->resource_set, "u_base_color_texture", texture_index + texture_offset, state.default_sampler);
		} else {
			vulkan_renderer_update_resource_set_texture_sampler(state.ctx, dst_material->resource_set, "u_base_color_texture", state.default_texture_white, state.default_sampler);
		}

		if (src_material->metallic_roughness_texture) {
			uint32_t texture_index = src_material->metallic_roughness_texture - scene->textures;
			vulkan_renderer_update_resource_set_texture_sampler(state.ctx, dst_material->resource_set, "u_metallic_roughness_texture", texture_index + texture_offset, state.default_sampler);
		} else {
			vulkan_renderer_update_resource_set_texture_sampler(state.ctx, dst_material->resource_set, "u_metallic_roughness_texture", state.default_texture_white, state.default_sampler);
		}

		if (src_material->normal_texture) {
			uint32_t texture_index = src_material->normal_texture - scene->textures;
			vulkan_renderer_update_resource_set_texture_sampler(state.ctx, dst_material->resource_set, "u_normal_texture", texture_index + texture_offset, state.default_sampler);
		} else {
			vulkan_renderer_update_resource_set_texture_sampler(state.ctx, dst_material->resource_set, "u_normal_texture", state.default_texture_normal, state.default_sampler);
		}

		if (src_material->occlusion_texture) {
			uint32_t texture_index = src_material->occlusion_texture - scene->textures;
			vulkan_renderer_update_resource_set_texture_sampler(state.ctx, dst_material->resource_set, "u_occlusion_texture", texture_index + texture_offset, state.default_sampler);
		} else {
			vulkan_renderer_update_resource_set_texture_sampler(state.ctx, dst_material->resource_set, "u_occlusion_texture", state.default_texture_white, state.default_sampler);
		}

		if (src_material->emissive_texture) {
			uint32_t texture_index = src_material->emissive_texture - scene->textures;
			vulkan_renderer_update_resource_set_texture_sampler(state.ctx, dst_material->resource_set, "u_emissive_texture", texture_index + texture_offset, state.default_sampler);
		} else {
			vulkan_renderer_update_resource_set_texture_sampler(state.ctx, dst_material->resource_set, "u_emissive_texture", state.default_texture_black, state.default_sampler);
		}
	}

	for (uint32_t mesh_index = 0; mesh_index < scene->mesh_count; ++mesh_index) {
		if (mesh_index > MAX_MODEL_MESHES)
			assert("Model has more than max meshes");

		MeshSource *src_mesh = &scene->meshes[mesh_index];
		Mesh *dst_mesh = &dst->meshes[dst->mesh_count++];

		dst_mesh->vertex_buffer = state.buffer_count++;
		dst_mesh->index_buffer = state.buffer_count++;

		dst_mesh->vertex_count = src_mesh->vertex_count;
		dst_mesh->index_count = src_mesh->index_count;

		dst_mesh->material = &dst->materials[src_mesh->material - scene->materials];

		vulkan_renderer_create_buffer(state.ctx, dst_mesh->vertex_buffer, BUFFER_TYPE_VERTEX, sizeof(Vertex) * src_mesh->vertex_count, src_mesh->vertices);
		vulkan_renderer_create_buffer(state.ctx, dst_mesh->index_buffer, BUFFER_TYPE_INDEX, sizeof(uint32_t) * src_mesh->index_count, src_mesh->indices);
	}

	return true;
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

void game_update(float dt) {
	// TODO: Gameplay
}

bool resize_event(Event *event) {
	WindowResizeEvent *wr_event = (WindowResizeEvent *)event;

	vulkan_renderer_resize(state.ctx, wr_event->width, wr_event->height);
	return true;
}

bool create_default_material_instance(void) {
	state.default_parameters = (MaterialParameters){
		.base_color_factor = { 0.8f, 0.8f, 0.8f, 1.0f },
		.emissive_factor = { 0.0f, 0.0f, 0.0f, 0.0f },
		.roughness_factor = 0.5f,
		.metallic_factor = 0.0f,
	};

	state.default_material_instance = (MaterialInstance){
		.material = state.model_material,
		.parameter_uniform_buffer = state.buffer_count++,
		.resource_set = state.set_count++
	};
	vulkan_renderer_create_buffer(state.ctx, state.default_material_instance.parameter_uniform_buffer, BUFFER_TYPE_UNIFORM, sizeof(MaterialParameters), &state.default_parameters);

	vulkan_renderer_create_resource_set(state.ctx, state.default_material_instance.resource_set, state.default_material_instance.material.shader, SHADER_UNIFORM_FREQUENCY_PER_MATERIAL);
	vulkan_renderer_update_resource_set_buffer(state.ctx, state.default_material_instance.resource_set, "u_scene", state.scene_uniform_buffer);
	vulkan_renderer_update_resource_set_buffer(state.ctx, state.default_material_instance.resource_set, "u_material", state.default_material_instance.parameter_uniform_buffer);

	vulkan_renderer_update_resource_set_texture_sampler(state.ctx, state.default_material_instance.resource_set, "u_base_color_texture", state.default_texture_white, state.default_sampler);
	vulkan_renderer_update_resource_set_texture_sampler(state.ctx, state.default_material_instance.resource_set, "u_metallic_roughness_texture", state.default_texture_white, state.default_sampler);
	vulkan_renderer_update_resource_set_texture_sampler(state.ctx, state.default_material_instance.resource_set, "u_normal_texture", state.default_texture_normal, state.default_sampler);
	vulkan_renderer_update_resource_set_texture_sampler(state.ctx, state.default_material_instance.resource_set, "u_occlusion_texture", state.default_texture_white, state.default_sampler);
	vulkan_renderer_update_resource_set_texture_sampler(state.ctx, state.default_material_instance.resource_set, "u_emissive_texture", state.default_texture_black, state.default_sampler);

	return true;
}

bool create_plane_mesh(Mesh *mesh, uint32_t subdivide_width, uint32_t subdivide_depth, Orientation orientation) {
	ArenaTemp temp = arena_get_scratch(NULL);

	uint32_t rows = subdivide_width + 1, columns = subdivide_depth + 1;

	Vertex *vertices = arena_push_array_zero(temp.arena, Vertex, rows * columns * 6);
	mesh->vertex_count = 0;

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
					vertices[mesh->vertex_count++] = orientation_y_vertex00;
					vertices[mesh->vertex_count++] = orientation_y_vertex01;
					vertices[mesh->vertex_count++] = orientation_y_vertex10;

					vertices[mesh->vertex_count++] = orientation_y_vertex10;
					vertices[mesh->vertex_count++] = orientation_y_vertex01;
					vertices[mesh->vertex_count++] = orientation_y_vertex11;
				} break;
				case ORIENTATION_X: {
					vertices[mesh->vertex_count++] = orientation_x_vertex00;
					vertices[mesh->vertex_count++] = orientation_x_vertex01;
					vertices[mesh->vertex_count++] = orientation_x_vertex10;

					vertices[mesh->vertex_count++] = orientation_x_vertex10;
					vertices[mesh->vertex_count++] = orientation_x_vertex01;
					vertices[mesh->vertex_count++] = orientation_x_vertex11;
				} break;
				case ORIENTATION_Z: {
					vertices[mesh->vertex_count++] = orientation_z_vertex00;
					vertices[mesh->vertex_count++] = orientation_z_vertex01;
					vertices[mesh->vertex_count++] = orientation_z_vertex10;

					vertices[mesh->vertex_count++] = orientation_z_vertex10;
					vertices[mesh->vertex_count++] = orientation_z_vertex01;
					vertices[mesh->vertex_count++] = orientation_z_vertex11;
				} break;
					break;
			}
		}
	}

	mesh->vertex_buffer = state.buffer_count++;
	vulkan_renderer_create_buffer(state.ctx, mesh->vertex_buffer, BUFFER_TYPE_VERTEX, sizeof(Vertex) * mesh->vertex_count, vertices);

	mesh->index_buffer = mesh->index_count = 0;

	arena_reset_scratch(temp);

	return true;
}
