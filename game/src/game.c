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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

#define MAX_DRAW_COMMANDS 65536

typedef struct {
	float32 x, y;
	float32 width, height;
} Rectangle;

typedef struct {
	RhiTexture texture;
	uint32_2 texture_size;
	Rectangle source, dest;
} DrawCommand;

typedef struct {
	RhiTexture white;

	RhiSampler linear;
	RhiSampler nearest;

	RhiShader shader;

	RhiBuffer global_buffer;
	RhiBuffer vbo;

	DrawCommand *draw_list;
	RhiBuffer dynamic_ssbo;
} Renderer2D;

// NOTE: Entity
typedef struct {
	RhiTexture texture;
	Vector2f position;
	Vector2f size;
} Sprite;

typedef struct {
	Arena arena;
	bool is_initialized;

	VulkanContext *context;
	Window *display;

	Renderer2D renderer;
	RhiTexture sprite0, sprite1;

	ArenaTrie uniform_set_cache;
	// TERRRAIN
	ArenaTrie tile_lookup;
	RhiTexture terrain_atlas;

	uint32_t *terrain_layer_data;
	uint32_t map_width, map_height;

	// Objects
	Sprite *objects;

	Camera camera;
	Vector2f player_position;

} PermanentState;

typedef struct {
	Arena transient;
	bool is_initialized;

} TransientState;

static PermanentState *pstate = NULL;
static TransientState *tstate = NULL;

typedef struct {
	uint32_t firstgid;
	RhiTexture texture;
	float32 width, height;
	float32 imagewidth, imageheight;

	uint32_t columns, rows;
} TilesetTexture;

Renderer2D renderer_make(void);
bool frame_begin(Renderer2D *renderer, Camera camera);
void frame_end(Renderer2D *renderer);
RhiTexture texture_make(Renderer2D *renderer, String path);
void texture_draw(Renderer2D *renderer, RhiTexture texture, Vector2f position, Vector2f scale, Vector3f tint);
void texture_draw_ex(RhiTexture texture, Rectangle source, Rectangle dest, Vector3f tint);
static int sort_draw_commands(const void *a, const void *b) { return ((DrawCommand *)a)->texture.id - ((DrawCommand *)b)->texture.id; }

FrameInfo game_on_update_and_render(GameContext *context, float dt) {
	pstate = (PermanentState *)context->permanent_memory;
	tstate = (TransientState *)context->transient_memory;

	if (pstate->is_initialized == false) {
		pstate->context = context->vk_context;
		pstate->display = context->display;
		pstate->arena = arena_wrap(pstate + 1, context->permanent_memory_size - sizeof(PermanentState));
		pstate->renderer = renderer_make();

		ArenaTemp scratch = arena_scratch_begin(NULL);
		pstate->tile_lookup = arena_trie_make(scratch.arena);
		String map_path = S("assets/pokemon/data/maps/world.tmj");

		String source = filesystem_read(scratch.arena, map_path);
		JsonNode *root = json_parse(scratch.arena, source);

		for (JsonNode *map_tileset = json_list(root, S("tilesets")); map_tileset; map_tileset = map_tileset->next) {
			String source = json_find(map_tileset, S("source"), String);
			uint32_t firstgid = json_find(map_tileset, S("firstgid"), uint32_t);

			source = string_path_join(scratch.arena, string_path_directory(map_path), source);
			source = string_path_clean(scratch.arena, source);

			source = filesystem_read(scratch.arena, source);
			JsonNode *tileset = json_parse(scratch.arena, source);

			if (json_find(tileset, S("columns"), uint32_t)) {
				String image_path = json_find(tileset, S("image"), String);
				image_path = string_path_clean(scratch.arena, image_path);
				image_path = string_path_join(scratch.arena, string_path_directory(map_path), image_path);
				LOG_INFO("ImagePath: " SFMT, SARG(image_path));

				ImageSource image_src = importer_load_image(scratch.arena, image_path);
				RhiTexture texture = vulkan_texture_make(pstate->context,
					image_src.width, image_src.height,
					TEXTURE_TYPE_2D,
					TEXTURE_FORMAT_RGBA8_SRGB, TEXTURE_USAGE_SAMPLED,
					image_src.pixels);

				TilesetTexture tile = {
					.texture = texture,
					.firstgid = firstgid,
					.width = json_find(tileset, S("tilewidth"), float32),
					.height = json_find(tileset, S("tileheight"), float32),
					.imagewidth = image_src.width,
					.imageheight = image_src.height,
				};
				tile.columns = json_find(tileset, S("columns"), uint32_t);
				tile.rows = image_src.height / tile.height;

				if (pstate->terrain_atlas.id == 0)
					pstate->terrain_atlas = tile.texture;

				for (uint32_t index = 0; index < json_find(tileset, S("tilecount"), uint32_t); ++index)
					*arena_trie_push(&pstate->tile_lookup, index + firstgid, TilesetTexture) = tile;

			} else {
				for (JsonNode *image_node = json_list(tileset, S("tiles")); image_node; image_node = image_node->next) {
					uint32_t id = json_find(image_node, S("id"), uint32_t);
					String image_path = json_find(image_node, S("image"), String);

					image_path = string_path_clean(scratch.arena, image_path);
					image_path = string_path_join(scratch.arena, string_path_directory(map_path), image_path);
					ImageSource image_src = importer_load_image(scratch.arena, image_path);

					TilesetTexture tile = {
						.width = json_find(image_node, S("imagewidth"), uint32_t),
						.height = json_find(image_node, S("imageheight"), uint32_t),
					};
					tile.imagewidth = tile.width;
					tile.imageheight = tile.height;
					tile.firstgid = firstgid;
					tile.columns = 1;
					tile.rows = 1;
					tile.texture = vulkan_texture_make(pstate->context,
						image_src.width, image_src.height,
						TEXTURE_TYPE_2D,
						TEXTURE_FORMAT_RGBA8_SRGB, TEXTURE_USAGE_SAMPLED,
						image_src.pixels);

					*arena_trie_push(&pstate->tile_lookup, id + firstgid, TilesetTexture) = tile;

					// NOTE: id in tsj (tileset file) is local. Id repeats for each tileset.
					// Not guaranteed to be sequential either (can't use id == index)
				}
			}
		}

		JsonNode *layers = json_list(root, S("layers"));

		JsonNode *terrain = json_find_where(layers, S("name"), S("Terrain"));
		JsonNode *entities = json_find_where(layers, S("name"), S("Entities"));
		JsonNode *objects = json_find_where(layers, S("name"), S("Objects"));

		pstate->map_width = json_find(root, S("width"), uint32_t);
		pstate->map_height = json_find(root, S("height"), uint32_t);

		uint32_t index = 0;
		pstate->terrain_layer_data = arena_push_count(scratch.arena, pstate->map_width * pstate->map_height, uint32_t);
		for (JsonNode *data_node = json_list(terrain, S("data")); data_node; data_node = data_node->next)
			pstate->terrain_layer_data[index++] = json_as(data_node, uint32_t);

		for (JsonNode *entity = json_list(entities, S("objects")); entity; entity = entity->next) {
			String name = json_find(entity, S("name"), String);
			JsonNode *properties = json_list(entity, S("properties"));
			bool at_house = json_find_where(properties, S("value"), S("house")) != NULL;

			float32 entity_x = json_find(entity, S("x"), float32);
			float32 entity_y = json_find(entity, S("y"), float32);

			if (string_equals(S("Player"), name) && at_house) {
				LOG_INFO(SFMT ": %.2f, %.2f", SARG(name), entity_x, entity_y);
				pstate->player_position.x = entity_x;
				pstate->player_position.y = entity_y;
			}
		}

		pstate->objects = arena_array_make(&pstate->arena, json_list_count(objects, S("objects")), Sprite);
		index = 0;
		for (JsonNode *object_node = json_list(objects, S("objects")); object_node; object_node = object_node->next) {
			Sprite *object = arena_array_push(pstate->objects);
			TilesetTexture *tile = arena_trie_find(&pstate->tile_lookup, json_find(object_node, S("gid"), uint32_t), TilesetTexture);

			*object = (Sprite){
				.position = { json_find(object_node, S("x"), float32), json_find(object_node, S("y"), float32) },
				.size = { tile->width, tile->height },
			};

			object->position.y -= object->size.y;

			object->texture = tile->texture;
		}

		arena_scratch_end(scratch);

		pstate->camera = (Camera){
			.projection = CAMERA_PROJECTION_ORTHOGRAPHIC,
			.position = { 1300, 600, 0.f },
			.target = { 0.0f, 0.0f, 0.0f },
			.up = { 0.0f, 1.0f, 0.0f },
			.fov = 45.f,
		};
		pstate->sprite0 = texture_make(&pstate->renderer, S("assets/sprites/kenney/tile_0085.png"));
		pstate->sprite1 = texture_make(&pstate->renderer, S("assets/sprites/kenney/tile_0086.png"));

		pstate->is_initialized = true;
	}

	if (tstate->is_initialized == false) {
		tstate->transient = (Arena){
			.memory = tstate + 1,
			.capacity = context->transient_memory_size - sizeof(TransientState)
		};

		tstate->is_initialized = true;
	}

	Vector2f input = {
		.x = input_key_down(KEY_CODE_D) - input_key_down(KEY_CODE_A),
		.y = input_key_down(KEY_CODE_S) - input_key_down(KEY_CODE_W),
	};
	input = vector2f_normalize(input);

	float32 speed = 100;
	if (input_key_down(KEY_CODE_LEFTSHIFT))
		speed = 1000;

	pstate->player_position.x += input.x * speed * dt;
	pstate->player_position.y += input.y * speed * dt;

	uint32_2 size = window_size_pixel(context->display);

	pstate->camera.position.x = MAX(pstate->player_position.x - size.x * .5f + 32, 0);
	pstate->camera.position.y = MAX(pstate->player_position.y - size.y * .5f + 32, 0);

	ArenaTemp scratch = arena_scratch_begin(NULL);
	pstate->uniform_set_cache = arena_trie_make(scratch.arena);
	pstate->renderer.draw_list = arena_array_make(scratch.arena, MAX_DRAW_COMMANDS, DrawCommand);
	for (uint32_t y = 0; y < pstate->map_height; ++y) {
		for (uint32_t x = 0; x < pstate->map_width; ++x) {
			uint32_t index = y * pstate->map_width + x;

			uint32_t tile_id = pstate->terrain_layer_data[index];
			TilesetTexture *tile = arena_trie_find(&pstate->tile_lookup, tile_id, TilesetTexture);
			if (tile == NULL)
				continue;

			uint32_t atlas_grid_x = (tile_id - tile->firstgid) % tile->columns;
			uint32_t atlas_grid_y = (tile_id - tile->firstgid) / tile->columns;

			float32 atlas_x = (float32)atlas_grid_x / (float32)tile->columns;
			float32 atlas_y = (float32)atlas_grid_y / (float32)tile->rows;

			Rectangle source = {
				.x = atlas_grid_x * tile->width,
				.y = atlas_grid_y * tile->height,
				.width = tile->width,
				.height = tile->height,
			};

			Rectangle dest = {
				.x = x * tile->width,
				.y = y * tile->width,
				.width = tile->width,
				.height = tile->width
			};

			Vector2f image_size = { tile->imagewidth, tile->imageheight };
			texture_draw_ex(tile->texture, source, dest, (Vector3f){ 1.0f, 1.0f, 1.0f });
		}
	}

	texture_draw(&pstate->renderer, pstate->sprite1, (Vector2f){ .x = (size.x + 64) * 0.5f, (size.y + 64) * 0.5f }, (Vector2f){ 64.f, 64.f }, (Vector3f){ 0.0f, 1.0f, 1.0f });
	texture_draw(&pstate->renderer, pstate->sprite0, pstate->player_position, (Vector2f){ 64.f, 64.f }, (Vector3f){ 1.0f, 1.0f, 1.0f });

	for (uint32_t index = 0; index < arena_array_count(pstate->objects); ++index)
		texture_draw(&pstate->renderer, pstate->objects[index].texture, pstate->objects[index].position, pstate->objects[index].size, (Vector3f){ 1.0f, 1.0f, 1.0f });

	uint32_t count = arena_array_count(pstate->renderer.draw_list);
	if (count > 0) {
		qsort(pstate->renderer.draw_list, count, sizeof(DrawCommand), sort_draw_commands);

		size_t floats_per_quad = 24;
		float32 *vertices = arena_push_count(scratch.arena, count * floats_per_quad, float32);

		uint32_t quad_index = 0;
		for (uint32_t index = 0; index < count; ++index) {
			DrawCommand *cmd = &pstate->renderer.draw_list[index];

			float u0 = cmd->source.x / cmd->texture_size.x;
			float v0 = cmd->source.y / cmd->texture_size.y;
			float u1 = (cmd->source.x + cmd->source.width) / cmd->texture_size.x;
			float v1 = (cmd->source.y + cmd->source.height) / cmd->texture_size.y;

			float32 x0 = cmd->dest.x;
			float32 y0 = cmd->dest.y;
			float32 x1 = cmd->dest.x + cmd->dest.width;
			float32 y1 = cmd->dest.y + cmd->dest.height;

			// clang-format off
            float32 quad[] = {
                // pos      // tex
                x0,  y1, u0, v1,
                x1,  y0, u1, v0,
                x0,  y0, u0, v0, 

                x0, y1, u0, v1,
                x1, y1, u1, v1,
                x1, y0, u1, v0
            };
			// clang-format on
			//
			memcpy(&vertices[quad_index++ * floats_per_quad], quad, sizeof(quad));
		}

		vulkan_buffer_write(pstate->context, pstate->renderer.dynamic_ssbo, 0, count * 24 * sizeof(float32), vertices);
	}

	if (frame_begin(&pstate->renderer, pstate->camera)) {
		uint32_t quad_index = 0;
		uint32_t batch_start_quad = 0;

		Matrix4f identity = matrix4f_identity();
		vulkan_push_constants(pstate->context, 0, sizeof(Matrix4f), &identity);

		vulkan_buffer_bind(pstate->context, pstate->renderer.dynamic_ssbo, 0);

		for (uint32_t index = 0; index < count; ++index) {
			DrawCommand *cmd = &pstate->renderer.draw_list[index];

			bool is_last = (index == count - 1);
			quad_index++;

			if (is_last || pstate->renderer.draw_list[index + 1].texture.id != cmd->texture.id) {
				uint32_t batch_quad_count = quad_index - batch_start_quad;

				RhiUniformSet set1 = vulkan_uniformset_push(pstate->context, pstate->renderer.shader, 1);
				vulkan_uniformset_bind_texture(pstate->context, set1, 1, cmd->texture, pstate->renderer.nearest);
				vulkan_uniformset_bind(pstate->context, set1);
				vulkan_renderer_draw_offset(pstate->context, batch_quad_count * 6, batch_start_quad * 6);

				batch_start_quad = quad_index;
			}
		}

		frame_end(&pstate->renderer);
	}
	arena_scratch_end(scratch);

	LOG_INFO("FPS: %.2f", 1 / dt);
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

	result.white = vulkan_texture_make(pstate->context, 1, 1, TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8, TEXTURE_USAGE_SAMPLED, (uint8_t[]){ 255, 255, 255, 255 });
	result.linear = vulkan_sampler_make(pstate->context, LINEAR_SAMPLER);
	result.nearest = vulkan_sampler_make(pstate->context, NEAREST_SAMPLER);

	ArenaTemp scratch = arena_scratch_begin(NULL);

	result.shader = vulkan_shader_make(
		scratch.arena,
		pstate->context,
		importer_load_shader(scratch.arena, S("assets/shaders/unlit.vert.spv"), S("assets/shaders/unlit.frag.spv")),
		NULL);
	arena_scratch_end(scratch);

	result.global_buffer = vulkan_buffer_make(pstate->context, BUFFER_TYPE_UNIFORM, sizeof(Matrix4f) * 2, NULL);

	uint32_t buffer_size = MAX_DRAW_COMMANDS * 24 * sizeof(float);
	// vulkan_buffer_make(pstate->context, BUFFER_TYPE_STORAGE, MAX_DRAW_COMMANDS, STRIDE, NULL);
	result.dynamic_ssbo = vulkan_buffer_make(pstate->context, BUFFER_TYPE_VERTEX, buffer_size, NULL);

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
	result.vbo = vulkan_buffer_make(pstate->context, BUFFER_TYPE_VERTEX, sizeof(vertices), vertices);

	return result;
}

bool frame_begin(Renderer2D *renderer, Camera camera) {
	uint32_2 size = window_size(pstate->display);
	if (vulkan_frame_begin(pstate->context, size.x, size.y)) {
		DrawListDesc desc = {
			.color_attachments[0] = {
			  .clear = { .color = { .x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f } },
			},
			.color_attachment_count = 1
		};
		if (vulkan_drawlist_begin(pstate->context, desc)) {
			Matrix4f projection = matrix4f_orthographic(0, size.x, 0, size.y, -1, 1);
			Matrix4f view = matrix4f_translated(vector3f_negate(camera.position));
			vulkan_buffer_write(pstate->context, renderer->global_buffer, 0, sizeof(Matrix4f), &view);
			vulkan_buffer_write(pstate->context, renderer->global_buffer, sizeof(Matrix4f), sizeof(Matrix4f), &projection);

			PipelineDesc pipeline = DEFAULT_PIPELINE;
			pipeline.cull_mode = CULL_MODE_NONE;
			vulkan_shader_bind(pstate->context, renderer->shader, pipeline);

			RhiUniformSet set0 = vulkan_uniformset_push(pstate->context, renderer->shader, 0);
			vulkan_uniformset_bind_buffer(pstate->context, set0, 0, renderer->global_buffer);
			vulkan_uniformset_bind(pstate->context, set0);

			return true;
		} else
			return false;
	} else
		return false;
}

void frame_end(Renderer2D *renderer) {
	vulkan_drawlist_end(pstate->context);
	vulkan_frame_end(pstate->context);
}

RhiTexture texture_make(Renderer2D *renderer, String path) {
	ArenaTemp scratch = arena_scratch_begin(NULL);
	ImageSource image = importer_load_image(scratch.arena, path);
	RhiTexture result =
		vulkan_texture_make(pstate->context,
			image.width, image.height,
			TEXTURE_TYPE_2D,
			TEXTURE_FORMAT_RGBA8_SRGB, TEXTURE_USAGE_SAMPLED,
			image.pixels);
	arena_scratch_end(scratch);

	return result;
}

void texture_draw(Renderer2D *renderer, RhiTexture texture, Vector2f position, Vector2f size, Vector3f tint) {
	uint32_2 imagesize = vulkan_texture_size(pstate->context, texture);
	Rectangle source = { 0, 0, imagesize.x, imagesize.y };
	Rectangle dest = { position.x, position.y, size.x, size.y };
	texture_draw_ex(texture, source, dest, tint);
}

void texture_draw_ex(RhiTexture texture, Rectangle source, Rectangle dest, Vector3f tint) {
	uint32_2 size = vulkan_texture_size(pstate->context, texture);
	arena_array_put(pstate->renderer.draw_list, DrawCommand, { .texture = texture, .dest = dest, .source = source, .texture_size = size });
}
