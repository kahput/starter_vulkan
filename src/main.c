#include "core/r_types.h"
#include "input/input_types.h"
#include "platform.h"

#include "renderer/r_internal.h"
#include "scene.h"
#include "renderer/backend/vulkan_api.h"

#include "event.h"
#include "events/platform_events.h"

#include "input.h"

#include "assets.h"
#include "assets/asset_types.h"

#include "common.h"
#include "core/logger.h"
#include "core/astring.h"
#include "core/arena.h"
#include "core/identifiers.h"

#include "platform/filesystem.h"

#include <cglm/cam.h>
#include <cglm/cglm.h>
#include <cglm/mat4.h>
#include <cglm/vec3.h>

#define GATE_FILE_PATH "assets/models/modular_dungeon/gate.glb"
#define GATE_DOOR_FILE_PATH "assets/models/modular_dungeon/gate-door.glb"
#define BOT_FILE_PATH "assets/models/characters/gdbot.glb"
#define MAGE_FILE_PATH "assets/models/characters/mage.glb"

typedef enum {
	RENDERER_DEFAULT_TEXTURE_WHITE,
	RENDERER_DEFAULT_TEXTURE_BLACK,
	RENDERER_DEFAULT_TEXTURE_NORMAL,
	RENDERER_DEFAULT_TARGET_SHADOW_DEPTH_MAP,
	RENDERER_DEFAULT_TARGET_MAIN_DEPTH_MAP,
	RENDERER_DEFAULT_TARGET_MAIN_COLOR_MAP,
	RENDERER_DEFAULT_SURFACE_COUNT,
} TextureIndex;

typedef enum {
	RENDERER_DEFAULT_SAMPLER_LINEAR,
	RENDERER_DEFAULT_SAMPLER_NEAREST,
} SamplerIndex;

typedef enum {
	RENDERER_DEFAULT_PASS_SHADOW,
	RENDERER_DEFAULT_PASS_MAIN,
	RENDERER_DEFAULT_PASS_POSTFX
} RenderPassIndex;

typedef enum {
	RENDERER_GLOBAL_RESOURCE_SHADOW,
	RENDERER_GLOBAL_RESOURCE_MAIN,
	RENDERER_GLOBAL_RESOURCE_POSTFX,
} GlobalResourceIndex;

typedef enum {
	ORIENTATION_Y,
	ORIENTATION_X,
	ORIENTATION_Z,
} Orientation;

static const float CAMERA_MOVE_SPEED = 5.f;
static const float CAMERA_SENSITIVITY = .001f;

typedef struct layer {
	void (*update)(float dt);
} Layer;

void editor_update(float dt);
bool resize_event(Event *event);
UUID create_plane_mesh(Arena *arena, uint32_t subdivide_width, uint32_t subdivide_depth, Orientation orientation, MeshSource **out_mesh);

static struct State {
	Arena permanent_arena, frame_arena;
	Platform display;
	VulkanContext *context;

	Camera editor_camera;

	Layer layers[1];
	Layer *current_layer;

	uint64_t start_time;
	uint32_t final_texture;

	uint32_t width, height;

	uint32_t next_buffer_index;
	uint32_t next_shader_index;
	uint32_t next_image_index;
	uint32_t next_group_index;
} state;

int main(void) {
	state.permanent_arena = arena_create(MiB(512));
	state.frame_arena = arena_create(MiB(4));
	state.final_texture = RENDERER_DEFAULT_TARGET_MAIN_COLOR_MAP;

	logger_set_level(LOG_LEVEL_DEBUG);

	event_system_startup();
	input_system_startup();
	asset_library_startup(arena_push(&state.permanent_arena, MiB(128), 1, true), MiB(128));

	platform_startup(&state.permanent_arena, 1280, 720, "Starter Vulkan", &state.display);

	state.width = state.display.physical_width;
	state.height = state.display.physical_height;

	if (vulkan_renderer_create(&state.permanent_arena, &state.display, &state.context) == false) {
		LOG_ERROR("Failed to create vulkan context");
		return 1;
	}

	state.next_buffer_index = 0;
	state.next_shader_index = 0;
	state.next_image_index = RENDERER_DEFAULT_SURFACE_COUNT;
	state.next_group_index = 0;

	// Create default textures
	uint8_t WHITE[4] = { 255, 255, 255, 255 };
	vulkan_renderer_texture_create(state.context, RENDERER_DEFAULT_TEXTURE_WHITE, 1, 1, TEXTURE_FORMAT_RGBA8_SRGB, TEXTURE_USAGE_SAMPLED, WHITE);

	uint8_t BLACK[4] = { 0, 0, 0, 255 };
	vulkan_renderer_texture_create(state.context, RENDERER_DEFAULT_TEXTURE_BLACK, 1, 1, TEXTURE_FORMAT_RGBA8_SRGB, TEXTURE_USAGE_SAMPLED, BLACK);

	uint8_t FLAT_NORMAL[4] = { 128, 128, 255, 255 };
	vulkan_renderer_texture_create(state.context, RENDERER_DEFAULT_TEXTURE_NORMAL, 1, 1, TEXTURE_FORMAT_RGBA8, TEXTURE_USAGE_SAMPLED, FLAT_NORMAL);

	// Create default samplers
	vulkan_renderer_sampler_create(state.context, RENDERER_DEFAULT_SAMPLER_LINEAR, LINEAR_SAMPLER);
	vulkan_renderer_sampler_create(state.context, RENDERER_DEFAULT_SAMPLER_NEAREST, NEAREST_SAMPLER);

	// Create render target textures
	vulkan_renderer_texture_create(state.context, RENDERER_DEFAULT_TARGET_SHADOW_DEPTH_MAP,
		state.width, state.height, TEXTURE_FORMAT_DEPTH,
		TEXTURE_USAGE_RENDER_TARGET | TEXTURE_USAGE_SAMPLED, NULL);

	vulkan_renderer_texture_create(state.context, RENDERER_DEFAULT_TARGET_MAIN_DEPTH_MAP,
		state.width, state.height, TEXTURE_FORMAT_DEPTH,
		TEXTURE_USAGE_RENDER_TARGET | TEXTURE_USAGE_SAMPLED, NULL);

	vulkan_renderer_texture_create(state.context, RENDERER_DEFAULT_TARGET_MAIN_COLOR_MAP,
		state.width, state.height, TEXTURE_FORMAT_RGBA16F,
		TEXTURE_USAGE_RENDER_TARGET | TEXTURE_USAGE_SAMPLED, NULL);

	// Create global resources
	vulkan_renderer_resource_global_create(
		state.context, RENDERER_GLOBAL_RESOURCE_SHADOW,
		(ResourceBinding[]){
		  { .binding = 0, .type = SHADER_BINDING_UNIFORM_BUFFER, .size = sizeof(mat4), .count = 1 },
		},
		1);
	vulkan_renderer_resource_global_create(
		state.context, RENDERER_GLOBAL_RESOURCE_MAIN,
		(ResourceBinding[]){
		  { .binding = 0, .type = SHADER_BINDING_UNIFORM_BUFFER, .size = sizeof(FrameData), .count = 1 },
		  { .binding = 1, .type = SHADER_BINDING_TEXTURE_2D, .size = 0, .count = 1 },
		  { .binding = 1, .type = SHADER_BINDING_SAMPLER, .size = 0, .count = 1 },
		},
		3);
	vulkan_renderer_resource_global_set_texture_sampler(state.context, RENDERER_GLOBAL_RESOURCE_MAIN, 1,
		RENDERER_DEFAULT_TARGET_SHADOW_DEPTH_MAP, RENDERER_DEFAULT_SAMPLER_LINEAR);

	vulkan_renderer_resource_global_create(
		state.context, RENDERER_GLOBAL_RESOURCE_POSTFX,
		(ResourceBinding[]){
		  { .binding = 0, .type = SHADER_BINDING_TEXTURE_2D, .size = 0, .count = 1 },
		  { .binding = 0, .type = SHADER_BINDING_SAMPLER, .size = 0, .count = 1 },
		},
		2);
	vulkan_renderer_resource_global_set_texture_sampler(state.context, RENDERER_GLOBAL_RESOURCE_POSTFX, 0,
		state.final_texture, RENDERER_DEFAULT_SAMPLER_LINEAR);

	// Create render passes
	RenderPassDesc shadow_pass = {
		.name = S("Shadow"),
		.depth_attachment = {
		  .texture = RENDERER_DEFAULT_TARGET_SHADOW_DEPTH_MAP,
		  .clear = { .depth = 1.0f },
		  .load = CLEAR,
		  .store = STORE,
		},
		.use_depth = true,
		.enable_msaa = false
	};

	RenderPassDesc main_pass = {
		.name = S("Main"),
		.color_attachments = {
		  { .texture = RENDERER_DEFAULT_TARGET_MAIN_COLOR_MAP, .clear = { .color = GLM_VEC4_BLACK_INIT }, .load = CLEAR, .store = STORE } },
		.color_attachment_count = 1,
		.depth_attachment = { .texture = RENDERER_DEFAULT_TARGET_MAIN_DEPTH_MAP, .clear = { .depth = 1.0f }, .load = CLEAR, .store = DONT_CARE },
		.use_depth = true,
		.enable_msaa = true
	};

	RenderPassDesc postfx_pass = {
		.name = S("Post"),
		.color_attachments = {
		  { .present = true, .clear = { .color = GLM_VEC4_BLACK_INIT }, .load = CLEAR, .store = STORE } },
		.color_attachment_count = 1,
		.enable_msaa = false
	};

	vulkan_renderer_pass_create(state.context, RENDERER_DEFAULT_PASS_SHADOW, shadow_pass);
	vulkan_renderer_pass_create(state.context, RENDERER_DEFAULT_PASS_MAIN, main_pass);
	vulkan_renderer_pass_create(state.context, RENDERER_DEFAULT_PASS_POSTFX, postfx_pass);

	MaterialParameters default_material_parameters = (MaterialParameters){
		.base_color_factor = { 0.8f, 0.8f, 0.8f, 1.0f },
		.emissive_factor = { 0.0f, 0.0f, 0.0f, 0.0f },
		.roughness_factor = 0.5f,
		.metallic_factor = 0.0f,
	};

	// Create default PBR shader
	uint32_t default_shader_index = state.next_shader_index++;
	ArenaTemp scratch = arena_scratch(NULL);
	{
		FileContent vsc = filesystem_read(scratch.arena, S("./assets/shaders/pbr.vert.spv"));
		FileContent fsc = filesystem_read(scratch.arena, S("./assets/shaders/pbr.frag.spv"));

		ShaderConfig shader_config = {
			.vertex_code = vsc.content,
			.vertex_code_size = vsc.size,
			.fragment_code = fsc.content,
			.fragment_code_size = fsc.size,
		};

		PipelineDesc desc = DEFAULT_PIPELINE();
		desc.cull_mode = CULL_MODE_BACK;

		ShaderReflection reflection;
		vulkan_renderer_shader_create(&state.permanent_arena, state.context,
			default_shader_index, RENDERER_GLOBAL_RESOURCE_MAIN, RENDERER_DEFAULT_PASS_MAIN, &shader_config, desc, &reflection);
	}
	arena_release_scratch(scratch);

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

	// Create sprite shader
	ShaderSource *sprite_shader_src = NULL;
	UUID sprite_shader_id = asset_library_request_shader(S("sprite.glsl"), &sprite_shader_src);
	vec4 sprite_color = { 1.0f, 1.0f, 1.0f, 1.0f };
	uint32_t sprite_shader_index = state.next_shader_index++;

	{
		ShaderConfig sprite_config = {
			.vertex_code = sprite_shader_src->vertex_shader.content,
			.vertex_code_size = sprite_shader_src->vertex_shader.size,
			.fragment_code = sprite_shader_src->fragment_shader.content,
			.fragment_code_size = sprite_shader_src->fragment_shader.size,
		};

		PipelineDesc desc = DEFAULT_PIPELINE();
		desc.cull_mode = CULL_MODE_NONE;

		ShaderReflection reflection;
		vulkan_renderer_shader_create(&state.permanent_arena, state.context,
			sprite_shader_index, RENDERER_GLOBAL_RESOURCE_MAIN, RENDERER_DEFAULT_PASS_MAIN, &sprite_config, desc, &reflection);
	}

	// Create plane mesh
	scratch = arena_scratch(NULL);
	MeshSource *plane_src = NULL;
	UUID plane_id = create_plane_mesh(scratch.arena, 0, 0, ORIENTATION_Z, &plane_src);

	uint32_t plane_vb = state.next_buffer_index++;
	uint32_t plane_vertex_count = plane_src->vertex_count;
	vulkan_renderer_buffer_create(state.context, plane_vb, BUFFER_TYPE_VERTEX,
		sizeof(Vertex) * plane_src->vertex_count, plane_src->vertices);
	arena_release_scratch(scratch);

	// Load sprite textures
	ImageSource *sprite_src = NULL;
	UUID sprite_id_0 = asset_library_request_image(S("tile_0085.png"), &sprite_src);
	uint32_t sprite_texture_0 = state.next_image_index++;
	vulkan_renderer_texture_create(state.context, sprite_texture_0,
		sprite_src->width, sprite_src->height, TEXTURE_FORMAT_RGBA8_SRGB,
		TEXTURE_USAGE_SAMPLED, sprite_src->pixels);

	UUID sprite_id_1 = asset_library_request_image(S("tile_0086.png"), &sprite_src);
	uint32_t sprite_texture_1 = state.next_image_index++;
	vulkan_renderer_texture_create(state.context, sprite_texture_1,
		sprite_src->width, sprite_src->height, TEXTURE_FORMAT_RGBA8_SRGB,
		TEXTURE_USAGE_SAMPLED, sprite_src->pixels);

	// Create material instances for sprites
	uint32_t sprite_mat_0 = state.next_group_index++;
	vulkan_renderer_resource_group_create(state.context, sprite_mat_0, sprite_shader_index, 256);
	vulkan_renderer_resource_group_write(state.context, sprite_mat_0, 0, 0, sizeof(vec4), &sprite_color, true);
	vulkan_renderer_resource_group_set_texture_sampler(state.context, sprite_mat_0, 0,
		sprite_texture_0, RENDERER_DEFAULT_SAMPLER_NEAREST);

	uint32_t sprite_mat_1 = state.next_group_index++;
	vulkan_renderer_resource_group_create(state.context, sprite_mat_1, sprite_shader_index, 256);
	vulkan_renderer_resource_group_write(state.context, sprite_mat_1, 0, 0, sizeof(vec4), &sprite_color, true);
	vulkan_renderer_resource_group_set_texture_sampler(state.context, sprite_mat_1, 0,
		sprite_texture_1, RENDERER_DEFAULT_SAMPLER_NEAREST);

	// Load glTF model
	ModelSource *model_src = NULL;
	Arena model_arena = arena_create(MiB(512));
	UUID model_id = asset_library_load_model(&model_arena, S("room-large.glb"), &model_src, true);

	uint32_t room_vb = INVALID_INDEX;
	uint32_t room_ib = INVALID_INDEX;
	uint32_t room_vertex_count = 0;
	uint32_t room_index_count = 0;
	uint32_t room_index_size = 0;
	uint32_t room_texture = INVALID_INDEX;
	uint32_t room_material = INVALID_INDEX;

	if (model_src) {
		LOG_INFO("Model loaded: %d meshes, %d materials, %d images",
			model_src->mesh_count, model_src->material_count, model_src->image_count);

		room_vb = state.next_buffer_index++;
		room_vertex_count = model_src->meshes[0].vertex_count;
		vulkan_renderer_buffer_create(state.context, room_vb, BUFFER_TYPE_VERTEX,
			sizeof(Vertex) * model_src->meshes[0].vertex_count, model_src->meshes[0].vertices);

		if (model_src->meshes[0].index_count > 0) {
			room_ib = state.next_buffer_index++;
			room_index_count = model_src->meshes[0].index_count;
			room_index_size = model_src->meshes[0].index_size;
			vulkan_renderer_buffer_create(state.context, room_ib, BUFFER_TYPE_INDEX,
				model_src->meshes[0].index_size * model_src->meshes[0].index_count,
				model_src->meshes[0].indices);
		}

		room_texture = state.next_image_index++;
		vulkan_renderer_texture_create(state.context, room_texture,
			model_src->images->width, model_src->images->height, TEXTURE_FORMAT_RGBA8_SRGB,
			TEXTURE_USAGE_SAMPLED, model_src->images->pixels);

		room_material = state.next_group_index++;
		vulkan_renderer_resource_group_create(state.context, room_material, default_shader_index, 256);
		vulkan_renderer_resource_group_write(state.context, room_material, 0, 0, sizeof(MaterialParameters), &default_material_parameters, true);
		vulkan_renderer_resource_group_set_texture_sampler(state.context, room_material, 0,
			room_texture, RENDERER_DEFAULT_SAMPLER_NEAREST);
		vulkan_renderer_resource_group_set_texture_sampler(state.context, room_material, 1,
			RENDERER_DEFAULT_TEXTURE_WHITE, RENDERER_DEFAULT_SAMPLER_LINEAR);
		vulkan_renderer_resource_group_set_texture_sampler(state.context, room_material, 2,
			RENDERER_DEFAULT_TEXTURE_NORMAL, RENDERER_DEFAULT_SAMPLER_LINEAR);
		vulkan_renderer_resource_group_set_texture_sampler(state.context, room_material, 3,
			RENDERER_DEFAULT_TEXTURE_WHITE, RENDERER_DEFAULT_SAMPLER_LINEAR);
		vulkan_renderer_resource_group_set_texture_sampler(state.context, room_material, 4,
			RENDERER_DEFAULT_TEXTURE_BLACK, RENDERER_DEFAULT_SAMPLER_LINEAR);
	}

	// Create light debug shader
	ShaderSource *light_shader_src = NULL;
	UUID light_shader_id = asset_library_request_shader(S("light_debug.glsl"), &light_shader_src);
	uint32_t light_shader_index = state.next_shader_index++;

	{
		ShaderConfig light_config = {
			.vertex_code = light_shader_src->vertex_shader.content,
			.vertex_code_size = light_shader_src->vertex_shader.size,
			.fragment_code = light_shader_src->fragment_shader.content,
			.fragment_code_size = light_shader_src->fragment_shader.size,
		};

		PipelineDesc desc = DEFAULT_PIPELINE();
		desc.cull_mode = CULL_MODE_NONE;

		ShaderReflection reflection;
		vulkan_renderer_shader_create(
			&state.permanent_arena, state.context,
			light_shader_index, RENDERER_GLOBAL_RESOURCE_MAIN, RENDERER_DEFAULT_PASS_MAIN, &light_config, desc, &reflection);
	}

	ShaderSource *shadow_shader_src = NULL;
	UUID shadow_shader_id = asset_library_request_shader(S("shadow_caster.glsl"), &shadow_shader_src);
	uint32_t shadow_shader_index = state.next_shader_index++;
	{
		ShaderConfig shadow_config = {
			.vertex_code = shadow_shader_src->vertex_shader.content,
			.vertex_code_size = shadow_shader_src->vertex_shader.size,
			.fragment_code = shadow_shader_src->fragment_shader.content,
			.fragment_code_size = shadow_shader_src->fragment_shader.size,
		};

		PipelineDesc desc = DEFAULT_PIPELINE();
		desc.override_attributes = (ShaderAttribute[]){
			(ShaderAttribute){ .name = S("in_position"), .binding = 0, .format = { .type = SHADER_ATTRIBUTE_TYPE_FLOAT32, .count = 3 } },
			(ShaderAttribute){ .name = S("in_normal"), .binding = 0, .format = { .type = SHADER_ATTRIBUTE_TYPE_FLOAT32, .count = 3 } },
			(ShaderAttribute){ .name = S("in_uv"), .binding = 0, .format = { .type = SHADER_ATTRIBUTE_TYPE_FLOAT32, .count = 3 } },
			(ShaderAttribute){ .name = S("in_tangent"), .binding = 0, .format = { .type = SHADER_ATTRIBUTE_TYPE_FLOAT32, .count = 3 } },
		};
		desc.override_count = 4;
		desc.cull_mode = CULL_MODE_NONE;

		ShaderReflection reflection;
		vulkan_renderer_shader_create(
			&state.permanent_arena, state.context,
			shadow_shader_index, RENDERER_GLOBAL_RESOURCE_SHADOW, RENDERER_DEFAULT_PASS_SHADOW, &shadow_config, desc, &reflection);
	}

	ShaderSource *postfx_shader_src = NULL;
	UUID postfx_shader_id = asset_library_request_shader(S("postfx.glsl"), &postfx_shader_src);
	uint32_t postfx_shader_index = state.next_shader_index++;
	{
		ShaderConfig postfx_config = {
			.vertex_code = postfx_shader_src->vertex_shader.content,
			.vertex_code_size = postfx_shader_src->vertex_shader.size,
			.fragment_code = postfx_shader_src->fragment_shader.content,
			.fragment_code_size = postfx_shader_src->fragment_shader.size,
		};

		PipelineDesc desc = DEFAULT_PIPELINE();
		desc.cull_mode = CULL_MODE_NONE;
		ShaderReflection reflection;
		vulkan_renderer_shader_create(
			&state.permanent_arena, state.context,
			postfx_shader_index, RENDERER_GLOBAL_RESOURCE_POSTFX, RENDERER_DEFAULT_PASS_POSTFX, &postfx_config, desc, &reflection);
	}
	float quadVertices[] = {
		-1.0f, 1.0f, 0.0f, 1.0f,
		-1.0f, -1.0f, 0.0f, 0.0f,
		1.0f, -1.0f, 1.0f, 0.0f,

		-1.0f, 1.0f, 0.0f, 1.0f,
		1.0f, -1.0f, 1.0f, 0.0f,
		1.0f, 1.0f, 1.0f, 1.0f
	};
	uint32_t quadBuffer = state.next_buffer_index++;
	vulkan_renderer_buffer_create(state.context, quadBuffer, BUFFER_TYPE_VERTEX, sizeof(quadVertices), quadVertices);

	uint32_t light_materials[16];
	for (uint32_t light_material = 0; light_material < 16; ++light_material) {
		light_materials[light_material] = state.next_group_index++;
		vulkan_renderer_resource_group_create(state.context, light_materials[light_material], light_shader_index, 256);
	}

	float timer_accumulator = 0.0f;
	uint32_t frames = 0;

	float sun_theta = 2 * GLM_PI / 3.f;
	float sun_azimuth = 0;
	Light lights[] = {
		[0] = { .type = LIGHT_TYPE_DIRECTIONAL, .color = { 0.2f, 0.2f, 1.0f, 0.1f }, .as.direction = { 0.0f, 0.0f, 0.0f } },
		[1] = { .type = LIGHT_TYPE_POINT, .color = { 1.0f, 0.5f, 0.2f, 0.8f }, .as.position = { 0.0f, 3.0f, 1.0f } }
	};

	lights[0].as.direction[0] = sin(sun_theta) * cos(sun_azimuth);
	lights[0].as.direction[1] = cos(sun_theta);
	lights[0].as.direction[2] = sin(sun_theta) * sin(sun_azimuth);

	while (platform_should_close(&state.display) == false) {
		float time = platform_time_seconds(&state.display);
		delta_time = time - last_frame;
		last_frame = time;
		delta_time = max(delta_time, 0.0016f);

		platform_poll_events(&state.display);

		timer_accumulator += delta_time;
		frames++;

		float tint = (cos(timer_accumulator) + 1.0f) * 0.5f;

		if (timer_accumulator >= 1.0f) {
			LOG_INFO("FPS: %d", frames);
			frames = 0;
			timer_accumulator = 0;
		}

		state.current_layer->update(delta_time);

		LOG_INFO("Sun direction = { %.2f, %.2f, %.2f }", lights[0].as.direction[0], lights[0].as.direction[1], lights[0].as.direction[2]);

		lights[1].as.position[0] = cos(time) * 5;
		lights[1].as.position[2] = sin(time) * 5;

		if (vulkan_renderer_frame_begin(state.context, state.display.physical_width,
				state.display.physical_height)) {
			// Shadow pass
			vulkan_renderer_pass_begin(state.context, RENDERER_DEFAULT_PASS_SHADOW);
			{
				mat4 light_projection;
				float scene_radius = 30.0f;
				glm_ortho(-scene_radius, scene_radius, scene_radius, -scene_radius,
					1.0f, scene_radius * 2.0f, light_projection);

				mat4 light_view;
				vec3 light_position;
				vec3 light_target = { 0.0f, 0.0f, 0.0f }; // Looking at scene center

				glm_vec3_scale(lights[0].as.direction, -scene_radius - 15.0f, light_position);
				glm_lookat(light_position, light_target, (vec3){ 0.0f, 1.0f, 0.0f }, light_view);

				LOG_INFO("Light position: { %.2f, %.2f, %.2f }", light_position[0], light_position[1], light_position[2]);

				mat4 light_matrix;
				glm_mat4_mul(light_projection, light_view, light_matrix);

				vulkan_renderer_resource_global_write(state.context, RENDERER_GLOBAL_RESOURCE_SHADOW, 0,
					sizeof(mat4), &light_matrix);
				vulkan_renderer_resource_global_bind(state.context, RENDERER_GLOBAL_RESOURCE_SHADOW);
				vulkan_renderer_shader_bind(state.context, shadow_shader_index);

				// Draw sprites
				mat4 transform = GLM_MAT4_IDENTITY_INIT;
				glm_translate(transform, (vec3){ 1.0f, 0.75f, 1.0f });
				glm_scale(transform, (vec3){ 1.5f, 1.5f, 1.5f });

				vulkan_renderer_resource_local_write(state.context, 0, sizeof(mat4), transform);
				vulkan_renderer_buffer_bind(state.context, plane_vb, 0);
				vulkan_renderer_draw(state.context, plane_vertex_count);

				glm_mat4_identity(transform);
				glm_translate(transform, (vec3){ 0.0f, 0.75f, 0.0f });
				glm_scale(transform, (vec3){ 1.5f, 1.5f, 1.5f });

				vulkan_renderer_resource_local_write(state.context, 0, sizeof(mat4), transform);
				vulkan_renderer_buffer_bind(state.context, plane_vb, 0);
				vulkan_renderer_draw(state.context, plane_vertex_count);

				if (room_vb != INVALID_INDEX) {
					glm_mat4_identity(transform);
					glm_translate(transform, (vec3){ 0.0f, 0.0f, 0.0f });

					vulkan_renderer_resource_local_write(state.context, 0, sizeof(mat4), transform);
					vulkan_renderer_buffer_bind(state.context, room_vb, 0);

					if (room_index_count > 0) {
						vulkan_renderer_buffer_bind(state.context, room_ib, room_index_size);
						vulkan_renderer_draw_indexed(state.context, room_index_count);
					} else {
						vulkan_renderer_draw(state.context, room_vertex_count);
					}
				}
			}
			vulkan_renderer_pass_end(state.context);
			vulkan_renderer_texture_prepare_sample(state.context, RENDERER_DEFAULT_TARGET_SHADOW_DEPTH_MAP);

			// Main pass
			vulkan_renderer_pass_begin(state.context, RENDERER_DEFAULT_PASS_MAIN);
			{
				FrameData frame_data = { 0 };
				glm_mat4_identity(frame_data.view);
				glm_lookat(state.editor_camera.position, state.editor_camera.target,
					state.editor_camera.up, frame_data.view);

				glm_mat4_identity(frame_data.projection);
				glm_perspective(glm_rad(state.editor_camera.fov),
					(float)state.width / (float)state.height, 0.1f, 1000.f, frame_data.projection);
				frame_data.projection[1][1] *= -1;

				uint32_t point_light_count = 0;
				for (uint32_t light_index = 0; light_index < countof(lights); ++light_index) {
					if (lights[light_index].type == LIGHT_TYPE_DIRECTIONAL)
						memcpy(&frame_data.directional_light, lights + light_index, sizeof(Light));
					else
						memcpy(frame_data.lights + point_light_count++, lights + light_index, sizeof(Light));
				}
				glm_vec3_dup(state.editor_camera.position, frame_data.camera_position);
				frame_data.light_count = point_light_count;

				vulkan_renderer_resource_global_write(state.context, RENDERER_GLOBAL_RESOURCE_MAIN, 0,
					sizeof(FrameData), &frame_data);
				vulkan_renderer_resource_global_bind(state.context, RENDERER_GLOBAL_RESOURCE_MAIN);

				// Draw sprites
				mat4 transform = GLM_MAT4_IDENTITY_INIT;
				glm_translate(transform, (vec3){ 1.0f, 0.75f, 1.0f });
				glm_scale(transform, (vec3){ 1.5f, 1.5f, 1.5f });

				vulkan_renderer_shader_bind(state.context, sprite_shader_index);
				vulkan_renderer_resource_group_bind(state.context, sprite_mat_0, 0);
				vulkan_renderer_resource_local_write(state.context, 0, sizeof(mat4), transform);
				vulkan_renderer_buffer_bind(state.context, plane_vb, 0);
				vulkan_renderer_draw(state.context, plane_vertex_count);

				glm_mat4_identity(transform);
				glm_translate(transform, (vec3){ 0.0f, 0.75f, 0.0f });
				glm_scale(transform, (vec3){ 1.5f, 1.5f, 1.5f });

				vulkan_renderer_resource_group_bind(state.context, sprite_mat_1, 0);
				vulkan_renderer_resource_local_write(state.context, 0, sizeof(mat4), transform);
				vulkan_renderer_buffer_bind(state.context, plane_vb, 0);
				vulkan_renderer_draw(state.context, plane_vertex_count);

				if (room_vb != INVALID_INDEX) {
					glm_mat4_identity(transform);
					glm_translate(transform, (vec3){ 0.0f, 0.0f, 0.0f });

					vulkan_renderer_shader_bind(state.context, default_shader_index);
					vulkan_renderer_resource_group_bind(state.context, room_material, 0);
					vulkan_renderer_resource_local_write(state.context, 0, sizeof(mat4), transform);
					vulkan_renderer_buffer_bind(state.context, room_vb, 0);

					if (room_index_count > 0) {
						vulkan_renderer_buffer_bind(state.context, room_ib, room_index_size);
						vulkan_renderer_draw_indexed(state.context, room_index_count);
					} else {
						vulkan_renderer_draw(state.context, room_vertex_count);
					}
				}

				// Draw light debug
				vulkan_renderer_shader_bind(state.context, light_shader_index);
				for (uint32_t index = 0; index < countof(lights); ++index) {
					if (lights[index].type == LIGHT_TYPE_DIRECTIONAL)
						continue;

					glm_mat4_identity(transform);
					glm_translate(transform, lights[index].as.position);

					vulkan_renderer_resource_group_write(state.context, light_materials[index],
						0, 0, sizeof(vec4), lights[index].color, true);
					vulkan_renderer_resource_group_bind(state.context, light_materials[index], 0);
					vulkan_renderer_resource_local_write(state.context, 0, sizeof(mat4), transform);
					vulkan_renderer_buffer_bind(state.context, plane_vb, 0);
					vulkan_renderer_draw(state.context, plane_vertex_count);
				}
			}
			vulkan_renderer_pass_end(state.context);
			vulkan_renderer_texture_prepare_sample(state.context, RENDERER_DEFAULT_TARGET_MAIN_COLOR_MAP);

			// Postfx pass
			vulkan_renderer_pass_begin(state.context, RENDERER_DEFAULT_PASS_POSTFX);
			{
				vulkan_renderer_shader_bind(state.context, postfx_shader_index);
				vulkan_renderer_resource_global_bind(state.context, RENDERER_GLOBAL_RESOURCE_POSTFX);
				vulkan_renderer_buffer_bind(state.context, quadBuffer, 0);
				vulkan_renderer_draw(state.context, 6);
			}
			vulkan_renderer_pass_end(state.context);

			Vulkan_renderer_frame_end(state.context);
		}

		static bool wireframe = false;
		if (input_key_pressed(SV_KEY_ENTER)) {
			wireframe = !wireframe;
			vulkan_renderer_shader_global_state_wireframe_set(state.context, wireframe);
		}

		if (input_key_down(SV_KEY_LEFTCTRL))
			platform_pointer_mode(&state.display, PLATFORM_POINTER_NORMAL);
		else
			platform_pointer_mode(&state.display, PLATFORM_POINTER_DISABLED);

		input_system_update();
	}

	vulkan_renderer_destroy(state.context);
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

	if (vulkan_renderer_on_resize(state.context, wr_event->width, wr_event->height)) {
		state.width = wr_event->width;
		state.height = wr_event->height;
	}

	vulkan_renderer_texture_resize(state.context, RENDERER_DEFAULT_TARGET_SHADOW_DEPTH_MAP, state.width, state.height);
	vulkan_renderer_texture_resize(state.context, RENDERER_DEFAULT_TARGET_MAIN_COLOR_MAP, state.width, state.height);
	vulkan_renderer_texture_resize(state.context, RENDERER_DEFAULT_TARGET_MAIN_DEPTH_MAP, state.width, state.height);

	vulkan_renderer_resource_global_set_texture_sampler(state.context, RENDERER_GLOBAL_RESOURCE_POSTFX, 0, state.final_texture, RENDERER_DEFAULT_SAMPLER_NEAREST);

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
