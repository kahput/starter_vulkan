#include "assets/importer.h"
#include "input.h"
#include "input/input_types.h"
#include "platform.h"
#include "platform/filesystem.h"
#include "scene.h"

#include <game_interface.h>

#include <common.h>
#include <core/cmath.h>
#include <core/arena.h>
#include <core/debug.h>
#include <core/logger.h>
#include <core/astring.h>

#include <assets/json_parser.h>

#include <renderer.h>
#include <renderer/r_internal.h>
#include <renderer/backend/vulkan_api.h>

#include <assets.h>
#include <assets/mesh_source.h>
#include <assets/asset_types.h>

#include <math.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

typedef struct {
	RhiTexture white;

	RhiSampler linear;
	RhiSampler nearest;

	RhiShader shader;

	RhiBuffer global_buffer;

	RhiBuffer vbo;
} Renderer2D;

typedef struct {
	RhiBuffer buffer;
	RhiTexture texture;
} Sprite;

typedef struct {
	Arena arena, frame;
	bool is_initialized;

	VulkanContext *context;
	Window *display;

	Renderer2D renderer;
	Sprite sprite0, sprite1;

	RhiBuffer map_buffer;
	RhiTexture map_atlas;

	uint32_t tile_width, tile_height;

	uint32_t map_width, map_height;
	uint32_t map_columns;
	uint32_t map_id_offset;
	uint32_t *world_map_data;

	RhiBuffer world_map;

} GameState;

static GameState *state = NULL;

Renderer2D renderer_make(void);
bool frame_begin(Renderer2D *renderer);
void frame_end(Renderer2D *renderer);
Sprite sprite_make(Renderer2D *renderer, String path);
void texture_draw(Renderer2D *renderer, Sprite sprite, Vector3f position, Vector3f scale, Vector3f tint);

FrameInfo game_on_update_and_render(GameContext *context, float dt) {
	state = (GameState *)context->permanent_memory;

	ArenaTemp scratch = arena_scratch(NULL);
	if (state->is_initialized == false) {
		state->context = context->vk_context;
		state->display = context->display;
		state->arena = (Arena){
			.memory = state + 1,
			.offset = 0,
			.capacity = context->permanent_memory_size - sizeof(GameState)
		};
		state->frame = (Arena){
			.memory = context->transient_memory,
			.offset = 0,
			.capacity = context->transient_memory_size
		};

		state->renderer = renderer_make();
		state->sprite0 = sprite_make(&state->renderer, S("assets/sprites/kenney/tile_0085.png"));
		state->sprite1 = sprite_make(&state->renderer, S("assets/sprites/kenney/tile_0086.png"));

		ArenaTemp scratch = arena_scratch(NULL);
		String map_path = S("assets/pokemon/data/maps/world.tmj");
		String source = filesystem_read(scratch.arena, map_path);
		JsonNode *root = json_parse(scratch.arena, source);

		bool error = json_as(root, bool);
		bool infinite = json_find(root, "infinite", bool);
		LOG_INFO("Random value that doesn't exist is %b", infinite);

		state->map_columns = 0;
		uint32_t columns, rows;
		ImageSource world_atlas = { 0 };
		(void)world_atlas;

		for (JsonNode *tileset = json_list(root, "tilesets"); tileset; tileset = tileset->next) {
			String source = json_find(tileset, "source", String);
			uint32_t firstgid = json_find(tileset, "firstgid", uint32_t);

			if (firstgid == 1) {
				state->map_id_offset = firstgid;
				source = string_path_join(scratch.arena, string_path_directory(map_path), source);
				source = string_path_clean(scratch.arena, source);

				source = filesystem_read(scratch.arena, source);
				JsonNode *tileset_root = json_parse(scratch.arena, source);

				state->tile_width = json_find(tileset_root, "tilewidth", uint32_t);
				state->tile_height = json_find(tileset_root, "tileheight", uint32_t);

				String image_path = json_find(tileset_root, "image", String);
				image_path = string_path_clean(scratch.arena, image_path);
				image_path = string_path_join(scratch.arena, string_path_directory(map_path), image_path);
				LOG_INFO("ImagePath: " SFMT, SARG(image_path));

				world_atlas = importer_load_image(scratch.arena, image_path);

				columns = json_find(tileset_root, "columns", uint32_t);
				rows = world_atlas.height / state->tile_height;
			} else
				LOG_INFO("Skipping firstgid = %d", firstgid);
		}

		for (JsonNode *layer = json_list(root, "layers"); layer; layer = layer->next) {
			String name = json_find(layer, "name", String);

			if (string_equals(name, S("Terrain"))) {
				state->map_width = json_find(layer, "width", uint32_t);
				state->map_height = json_find(layer, "height", uint32_t);

				LOG_INFO("Layer " SFMT ": %d, %d", SARG(name), state->map_width, state->map_height);
				state->world_map_data = arena_push_count(scratch.arena, state->map_width * state->map_height, uint32_t);

				uint32_t index = 0;
				for (JsonNode *data_node = json_list(layer, "data"); data_node; data_node = data_node->next)
					state->world_map_data[index++] = json_as(data_node, uint32_t);
			}
		}

		float32 *map_batch = arena_push_count(scratch.arena, state->map_width * state->map_height * 24, float32);
		for (uint32_t y = 0; y < state->map_height; ++y) {
			for (uint32_t x = 0; x < state->map_width; ++x) {
				uint32_t index = y * state->map_width + x;

				uint32_t tile_id = state->world_map_data[index];

				uint32_t atlas_grid_x = (tile_id - state->map_id_offset) % columns;
				uint32_t atlas_grid_y = (tile_id - state->map_id_offset) / columns;

				float32 atlas_x = (float32)atlas_grid_x / (float32)columns;
				float32 atlas_y = (float32)atlas_grid_y / (float32)rows;

				float32 inc_x = atlas_x + (1.f / columns);
				float32 inc_y = atlas_y + (1.f / rows);

				// clang-format off
				float32 tile[] = {
                    // pos      // tex
                    x * 64,       (y + 1) * 64, atlas_x, inc_y,
                    (x + 1) * 64,  y * 64,      inc_x,   atlas_y,
                    x * 64,        y * 64,      atlas_x, atlas_y, 

                    x * 64,       (y + 1) * 64, atlas_x, inc_y,
                    (x + 1) * 64, (y + 1) * 64, inc_x,   inc_y,
                    (x + 1) * 64,  y * 64,      inc_x,   atlas_y
				};
				// clang-format on

				memcpy(map_batch + index * 24, tile, sizeof(tile));
			}
		}

		state->world_map = vulkan_buffer_make(context->vk_context, BUFFER_TYPE_VERTEX, sizeof(float) * 24 * state->map_width * state->map_height, map_batch);
		state->map_buffer = vulkan_buffer_make(state->context, BUFFER_TYPE_UNIFORM, sizeof(Vector4f), NULL);
		state->map_atlas = vulkan_texture_make(
			state->context,
			world_atlas.width, world_atlas.height,
			TEXTURE_TYPE_2D,
			TEXTURE_FORMAT_RGBA8_SRGB, TEXTURE_USAGE_SAMPLED,
			world_atlas.pixels);

		arena_scratch_release(scratch);

		state->is_initialized = true;
	}

	uint32_2 size = window_size_pixel(context->display);
	if (frame_begin(&state->renderer)) {
		RhiUniformSet set1 = vulkan_uniformset_push(state->context, state->renderer.shader, 1);
		vulkan_uniformset_bind_buffer(state->context, set1, 0, state->map_buffer);
		vulkan_uniformset_bind_texture(state->context, set1, 1, state->map_atlas, state->renderer.nearest);
		vulkan_buffer_write(state->context, state->map_buffer, 0, sizeof(Vector3f), &(Vector4f){ 1.0f, 1.0f, 1.0f, 1.0f });
		vulkan_uniformset_bind(state->context, set1);

		Matrix4f mat = matrix4f_identity();
		vulkan_push_constants(state->context, 0, sizeof(Matrix4f), &mat);
		vulkan_buffer_bind(state->context, state->world_map, 0);
		vulkan_renderer_draw(state->context, state->map_width * state->map_height * 6);

		texture_draw(&state->renderer, state->sprite1, (Vector3f){ .x = (size.x + 64) * 0.5f, (size.y + 64) * 0.5f, 0.0f }, (Vector3f){ 64.f, 64.f, 64.f }, (Vector3f){ 0.0f, 1.0f, 1.0f });
		texture_draw(&state->renderer, state->sprite0, (Vector3f){ .x = (size.x + 64) * 0.5f, 0.0f, 0.0f }, (Vector3f){ 64.f, 64.f, 64.f }, (Vector3f){ 1.0f, 1.0f, 1.0f });

		frame_end(&state->renderer);
	}

	arena_scratch_release(scratch);
	return (FrameInfo){ 0 };
}

static GameInterface interface;
GameInterface *game_hookup(void) {
	interface = (GameInterface){
		.on_update = game_on_update_and_render,
	};
	return &interface;
}

Renderer2D renderer_make(void) {
	Renderer2D result = { 0 };

	uint8_t WHITE[4] = { 255, 255, 255, 255 };
	result.white = vulkan_texture_make(state->context, 1, 1, TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8, TEXTURE_USAGE_SAMPLED, WHITE);
	result.linear = vulkan_sampler_make(state->context, LINEAR_SAMPLER);
	result.nearest = vulkan_sampler_make(state->context, NEAREST_SAMPLER);

	ArenaTemp scratch = arena_scratch(NULL);

	result.shader = vulkan_shader_make(
		scratch.arena,
		state->context,
		importer_load_shader(scratch.arena, S("assets/shaders/unlit.vert.spv"), S("assets/shaders/unlit.frag.spv")),
		NULL);
	arena_scratch_release(scratch);

	result.global_buffer = vulkan_buffer_make(state->context, BUFFER_TYPE_UNIFORM, sizeof(Matrix4f) * 2, NULL);

	// clang-format off
    float vertices[] = { 
        // pos      // tex
        0.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 
    
        0.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 0.0f, 1.0f, 0.0f
    };
	// clang-format on
	result.vbo = vulkan_buffer_make(state->context, BUFFER_TYPE_VERTEX, sizeof(vertices), vertices);

	return result;
}

bool frame_begin(Renderer2D *renderer) {
	Camera camera = {
		.projection = CAMERA_PROJECTION_ORTHOGRAPHIC,
		.position = { 0.0f, 0.0f, -10.f },
		.target = { 0.0f, 0.0f, 0.0f },
		.up = { 0.0f, 1.0f, 0.0f },
		.fov = 45.f,
	};

	uint32_2 size = window_size(state->display);
	if (vulkan_frame_begin(state->context, size.x, size.y)) {
		DrawListDesc desc = {
			.color_attachments[0] = {
			  .clear = { .color = { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f } },
			},
			.color_attachment_count = 1
		};
		if (vulkan_drawlist_begin(state->context, desc)) {
			Matrix4f projection = matrix4f_orthographic(0, size.x, 0, size.y, -1, 1);
			Matrix4f view = matrix4f_identity();
			vulkan_buffer_write(state->context, renderer->global_buffer, 0, sizeof(Matrix4f), &view);
			vulkan_buffer_write(state->context, renderer->global_buffer, sizeof(Matrix4f), sizeof(Matrix4f), &projection);

			PipelineDesc pipeline = DEFAULT_PIPELINE;
			pipeline.cull_mode = CULL_MODE_NONE;
			vulkan_shader_bind(state->context, renderer->shader, pipeline);

			RhiUniformSet set0 = vulkan_uniformset_push(state->context, renderer->shader, 0);
			vulkan_uniformset_bind_buffer(state->context, set0, 0, renderer->global_buffer);
			vulkan_uniformset_bind(state->context, set0);
			return true;
		} else
			return false;
	} else
		return false;
}
void frame_end(Renderer2D *renderer) {
	vulkan_drawlist_end(state->context);
	vulkan_frame_end(state->context);
}

Sprite sprite_make(Renderer2D *renderer, String path) {
	Sprite result = { 0 };

	ArenaTemp scratch = arena_scratch(NULL);
	ImageSource image = importer_load_image(scratch.arena, path);
	Vector4f tint = { 1.0f, 1.0f, 1.0f, 1.0f };

	result.texture = vulkan_texture_make(state->context, image.width, image.height, TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8_SRGB, TEXTURE_USAGE_SAMPLED, image.pixels);
	result.buffer = vulkan_buffer_make(state->context, BUFFER_TYPE_UNIFORM, sizeof(Vector4f), &tint);

	arena_scratch_release(scratch);

	return result;
}

void texture_draw(Renderer2D *renderer, Sprite sprite, Vector3f position, Vector3f scale, Vector3f tint) {
	Matrix4f transform = matrix4f_translated(position);
	transform = matrix4f_scale(transform, scale);

	vulkan_push_constants(state->context, 0, sizeof(Matrix4f), &transform);

	RhiUniformSet set1 = vulkan_uniformset_push(state->context, renderer->shader, 1);
	vulkan_uniformset_bind_buffer(state->context, set1, 0, sprite.buffer);
	vulkan_uniformset_bind_texture(state->context, set1, 1, sprite.texture, renderer->nearest);
	vulkan_buffer_write(state->context, sprite.buffer, 0, sizeof(Vector3f), &tint);
	vulkan_uniformset_bind(state->context, set1);

	vulkan_buffer_bind(state->context, renderer->vbo, 0);
	vulkan_renderer_draw(state->context, 6);
}
