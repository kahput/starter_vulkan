// GAME.c

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
#include <core/strings.h>

#include <assets/json_parser.h>

#include <math.h>
#include <renderer.h>
#include <renderer/r_internal.h>
#include <renderer/backend/vulkan_api.h>

#include <assets.h>
#include <assets/mesh_source.h>
#include <assets/asset_types.h>

#define MAX_DRAW_COMMANDS 65536

typedef struct {
	float x, y;
	float width, height;
} Rectangle;

typedef enum {
	DMT_DrawTextureCommand,
} DrawCommandType;

typedef struct {
	RhiTexture texture;
	Rectangle source, dest;
} DrawTextureCommand;

typedef uint64_t SortKey;
typedef struct {
	RhiTexture texture;
	Rectangle source, dest;
	SortKey key;
} DrawCommand;

typedef struct {
	RhiTexture white;

	RhiSampler linear;
	RhiSampler nearest;

	RhiShader dynamic_shader;
	RhiShader static_shader;

	RhiBuffer global_buffer;
	RhiBuffer dynamic_ssbo;
} Renderer2D;

// NOTE: Entity
typedef float4 Color;
typedef struct {
	// Drawing
	RhiTexture texture;
	Rectangle src;
	float2 origin;
	uint32_t layer;
	Color color;

	// Location
	float rotation;
	float2 position, size;

	// Collision
	Rectangle collision_shape;
} Sprite;

typedef struct {
	void *children[4];
	uint32_t key;
	Sprite value;
} SpriteTrie;

typedef struct {
	RhiTexture *frames;
	uint32_t frame_count;

	Rectangle src, dest;
	float2 origin;
	float rotation;
	Color color;
} AnimatedSprite;

typedef struct {
	Arena arena;
	bool is_initialized;

	VulkanContext *context;
	Window *display;

	Renderer2D renderer;

	Sprite *tile_lookup;
	// TERRRAIN
	RhiTexture terrain_texture[8];
	RhiBuffer terrain_vbos[8];
	uint32_t terrain_quad_count[8];
	uint32_t terrain_count;
	uint32_t map_width, map_height;

	// Objects
	Sprite *entities;
	Camera camera;

	Sprite player;
	Sprite *coast;

	AnimatedSprite *animated_sprites;
	RhiTexture water[4];

} PermanentState;

typedef struct {
	Arena transient;
	bool is_initialized;

} TransientState;

static PermanentState *pstate = NULL;
static TransientState *tstate = NULL;

static inline SortKey sort_key(uint16_t layer, uint32_t y_sort, uint16_t texture_id) {
	uint64_t result = ((uint64_t)layer << 48) | ((uint64_t)y_sort << 16) | texture_id;
	return result;
}

Renderer2D renderer_make(void);

void draw_rectangle_lines(DrawCommand *commands, Rectangle rect, float thickness, uint32_t layer);

void draw_texture(DrawCommand *commands, RhiTexture texture, Rectangle src, float2 position, float2 size, float2 origin, uint32_t layer);
void draw_rectangle(DrawCommand *commands, Rectangle rect);

RhiTexture texture_make(Renderer2D *renderer, String path);
static int sort_draw_commands(const void *a, const void *b) {
	SortKey a_key = ((DrawCommand *)a)->key;
	SortKey b_key = ((DrawCommand *)b)->key;

	if (a_key < b_key)
		return -1;
	if (a_key > b_key)
		return 1;
	return 0;
}

typedef enum {
	DRAW_LAYER_BACKGROUND,
	DRAW_LAYER_WORLD,
	DRAW_LAYER_FOREGROUND,
	DRAW_LAYER_DEBUG
} DrawLayer;

typedef struct {
	bool hit;
	float t;
	float2 point, normal; // Point of the nearest hit
} Ray2HitResult;
static const Ray2HitResult RAY2_NO_HIT = { false, INFINITY, { 0, 0 }, { 0, 0 } };

Ray2HitResult ray2_line_segment_intersection(float2 ro, float2 rd, float2 l0, float2 l1) {
	float2 ld = float2_normalize(float2_subtract(l1, l0));
	float2 ln = (float2){ -ld.y, ld.x };

	float denominator = float2_dot(rd, ln);
	if (fabsf(denominator) < EPSILON)
		return RAY2_NO_HIT;

	if (float2_dot(rd, ln) > 0.0f)
		ln = float2_scale(ln, -1.0f);

	float wall_offset = float2_dot(l0, ln);
	float t = (wall_offset - float2_dot(ro, ln)) / float2_dot(rd, ln);

	if (t < 0.0f)
		return RAY2_NO_HIT;

	float2 hit_point = float2_add(ro, float2_scale(rd, t)); // ro + t * rd
	float hit_along_line = float2_dot(float2_subtract(hit_point, l0), ld);
	float segment_length = float2_length(float2_subtract(l1, l0));

	if (hit_along_line < 0.0f || hit_along_line > segment_length)
		return RAY2_NO_HIT;

	Ray2HitResult result = {
		.hit = true,
		.normal = ln,
		.point = hit_point,
		.t = t
	};

	return result;
}

Ray2HitResult ray2_rectangle_intersection(float2 ro, float2 rd, float2 min, float2 max) {
	Ray2HitResult result = RAY2_NO_HIT;
	Ray2HitResult temp;

	float length = float2_length(rd);
	if (length) {
		temp = ray2_line_segment_intersection(ro, rd, min, (float2){ max.x, min.y });
		if (temp.t < result.t)
			result = temp;

		temp = ray2_line_segment_intersection(ro, rd, min, (float2){ min.x, max.y });
		if (temp.t < result.t)
			result = temp;

		temp = ray2_line_segment_intersection(ro, rd, max, (float2){ min.x, max.y });
		if (temp.t < result.t)
			result = temp;

		temp = ray2_line_segment_intersection(ro, rd, max, (float2){ max.x, min.y });
		if (temp.t < result.t)
			result = temp;
	}

	return result;
}

bool rectangles_overlapping(Rectangle a, Rectangle b) {
	bool result = false;

	if ((a.x + a.width > b.x && a.x < b.x + b.width) &&
		(a.y + a.height > b.y && a.y < b.y + b.height))
		result = true;

	return result;
}

Rectangle rectangle(float2 position, float2 size, float2 origin) {
	Rectangle result = {
		.x = position.x - origin.x,
		.y = position.y - origin.y,
		.width = size.x,
		.height = size.y,
	};

	return result;
}

FrameInfo game_on_update_and_render(GameContext *context, float dt) {
	pstate = (PermanentState *)context->permanent_memory;
	tstate = (TransientState *)context->transient_memory;

	if (pstate->is_initialized == false) {
		pstate->context = context->vk_context;
		pstate->display = context->display;
		pstate->arena = arena_wrap(pstate + 1, context->permanent_memory_size - sizeof(PermanentState));
		pstate->renderer = renderer_make();

		ArenaTemp scratch = arena_scratch_begin(NULL);
		SpriteTrie *tilemap = NULL;
		String map_path = S("assets/pokemon/data/maps/world.tmj");

		String source = string_wrap_span(filesystem_read(scratch.arena, map_path));
		JsonNode *root = json_parse(scratch.arena, source);

		uint32_t max_tile = 0;

		// Tilesets
		for (JsonNode *map_tileset = json_list(root, S("tilesets")); map_tileset; map_tileset = map_tileset->next) {
			String source = json_find(map_tileset, S("source"), String);
			uint32_t firstgid = json_find(map_tileset, S("firstgid"), uint32_t);

			source = stringpath_join(scratch.arena, stringpath_directory(map_path), source);
			source = stringpath_normalize(scratch.arena, source);

			source = string_wrap_span(filesystem_read(scratch.arena, source));
			JsonNode *tileset = json_parse(scratch.arena, source);

			if (json_find(tileset, S("columns"), uint32_t)) {
				String image_path = json_find(tileset, S("image"), String);
				image_path = stringpath_normalize(scratch.arena, image_path);
				image_path = stringpath_join(scratch.arena, stringpath_directory(map_path), image_path);
				LOG_INFO("ImagePath: " SFMT, SARG(image_path));

				ImageSource image_src = importer_load_image(scratch.arena, image_path);
				RhiTexture texture = vulkan_texture_make(pstate->context,
					image_src.width, image_src.height,
					TEXTURE_TYPE_2D,
					TEXTURE_FORMAT_RGBA8_SRGB, TEXTURE_USAGE_SAMPLED,
					image_src.pixels);

				float tilewidth = json_find(tileset, S("tilewidth"), float);
				float tileheight = json_find(tileset, S("tileheight"), float);

				uint32_t columns = json_find(tileset, S("columns"), uint32_t);
				uint32_t rows = image_src.height / tileheight;

				for (uint32_t index = 0; index < json_find(tileset, S("tilecount"), uint32_t); ++index) {
					uint32_t tile_id = index + firstgid;
					uint32_t atlas_gridx = index % columns;
					uint32_t atlas_gridy = index / columns;

					if (tile_id > max_tile)
						max_tile = tile_id;

					Sprite tile = {
						.texture = texture,
						.src = {
						  .x = atlas_gridx * tilewidth,
						  .y = atlas_gridy * tileheight,
						  .width = tilewidth,
						  .height = tileheight,
						},
						.position = { 0, 0 },
						.size = { tilewidth, tileheight },
					};

					arena_triestruct_put(scratch.arena, tilemap, span_struct(tile_id), Sprite, tile);
				}

			} else {
				for (JsonNode *image_node = json_list(tileset, S("tiles")); image_node; image_node = image_node->next) {
					uint32_t id = json_find(image_node, S("id"), uint32_t);
					String image_path = json_find(image_node, S("image"), String);

					image_path = stringpath_normalize(scratch.arena, image_path);
					image_path = stringpath_join(scratch.arena, stringpath_directory(map_path), image_path);

					ImageSource image_src = importer_load_image(scratch.arena, image_path);

					uint32_t tile_id = id + firstgid;

					if (tile_id > max_tile)
						max_tile = tile_id;

					Rectangle source = {
						.x = 0,
						.y = 0,
						.width = image_src.width,
						.height = image_src.height,
					};

					Sprite tile = {
						.texture = vulkan_texture_make(pstate->context,
							image_src.width, image_src.height,
							TEXTURE_TYPE_2D,
							TEXTURE_FORMAT_RGBA8_SRGB, TEXTURE_USAGE_SAMPLED,
							image_src.pixels),
						.src = source,
						.size = { source.width, source.height },
					};

					arena_triestruct_put(scratch.arena, tilemap, span_struct(tile_id), Sprite, tile);

					// NOTE: id in tsj (tileset file) is local. Id repeats for each tileset.
					// Not guaranteed to be sequential either (can't use id == index)
				}
			}
		}

		pstate->tile_lookup = arena_array_make(&pstate->arena, max_tile, Sprite);
		for (uint32_t index = 1; index < max_tile; ++index) {
			Sprite *tile = arena_triestruct_find(tilemap, span_struct(index), Sprite);
			if (tile)
				pstate->tile_lookup[index] = *tile;
		}

		// Tile data
		pstate->map_width = json_find(root, S("width"), uint32_t);
		pstate->map_height = json_find(root, S("height"), uint32_t);

		Sprite *dyn = arena_array_make(scratch.arena, 512, Sprite);

		float *buffers[8] = { 0 };
		uint32_t floats_per_quad = 24;
		ArenaTrie buffer_lookup = arena_trie_make(scratch.arena);

		for (JsonNode *layer = json_list(root, S("layers")); layer; layer = layer->next) {
			String type = json_find(layer, S("type"), String);

			// Create a single vbo
			if (string_equals(S("tilelayer"), type)) {
				uint32_t index = 0;

				for (JsonNode *data_node = json_list(layer, S("data")); data_node; data_node = data_node->next, index++) {
					uint32_t gid = json_as(data_node, uint32_t);

					Sprite *tile = &pstate->tile_lookup[gid];
					if (tile->texture.id == 0)
						continue;

					ASSERT(json_find(layer, S("width"), uint32_t) == pstate->map_width);
					ASSERT(json_find(layer, S("height"), uint32_t) == pstate->map_height);

					uint32_t x = index % pstate->map_width;
					uint32_t y = index / pstate->map_width;

					uint32x2 image_size = vulkan_texture_size(pstate->context, tile->texture);

					float u0 = tile->src.x / image_size.x;
					float v0 = tile->src.y / image_size.y;
					float u1 = (tile->src.x + tile->src.width) / image_size.x;
					float v1 = (tile->src.y + tile->src.height) / image_size.y;

					float x0 = x * tile->size.x;
					float y0 = y * tile->size.y;
					float x1 = x0 + tile->size.x;
					float y1 = y0 + tile->size.y;

					// clang-format off
                    float quad[] = {
                        // pos      // tex
                        x0,  y1, u0, v1,
                        x1,  y0, u1, v0,
                        x0,  y0, u0, v0, 

                        x0, y1, u0, v1,
                        x1, y1, u1, v1,
                        x1, y0, u1, v0
                    };
					// clang-format on

					ArenaTrieNode *node = NULL;
					if ((node = arena_trienode_find(&buffer_lookup, span_struct(tile->texture))) == NULL) {
						uint32_t current = pstate->terrain_count++;

						node = arena_trienode_push(&buffer_lookup, span_struct(tile->texture));
						buffers[current] = arena_array_make(scratch.arena, floats_per_quad * pstate->map_width * pstate->map_height, float);
						node->payload = buffers[current];
						pstate->terrain_texture[current] = tile->texture;
					}

					float *buffer = node->payload;
					memcpy(buffer + floats_per_quad * arena_array_count(buffer), quad, sizeof(quad));
					HEADER(node->payload, ArenaArrayHeader)->count++;
				}

			} else if (string_equals(S("objectgroup"), type)) {
				String name = json_find(layer, S("name"), String);
				if (string_equals(S("Objects"), name) == false && string_equals(S("Monsters"), name) == false)
					continue;

				for (JsonNode *object_node = json_list(layer, S("objects")); object_node; object_node = object_node->next) {
					uint32_t gid = 0;
					if ((gid = json_find(object_node, S("gid"), uint32_t)) == 0)
						break;
					Sprite *tile = &pstate->tile_lookup[json_find(object_node, S("gid"), uint32_t)];
					if (tile->texture.id == 0)
						break;

					Sprite *object = arena_darray_push(scratch.arena, dyn, Sprite);

					*object = (Sprite){
						.texture = tile->texture,
						.src = tile->src,
						.position = {
						  .x = json_find(object_node, S("x"), float),
						  .y = json_find(object_node, S("y"), float),
						},
						.size = tile->size
					};
					object->position.x += object->size.x * 0.5f;
					object->position.y -= object->size.y * 0.5f;
					object->layer = DRAW_LAYER_WORLD;

					object->collision_shape = (Rectangle){
						.x = 0,
						.y = -object->size.y * 0.25,
						.width = object->size.x,
						.height = object->size.y * 0.5f,
					};

					object->origin = (float2){
						.x = object->size.x * 0.5f,
						.y = object->size.y * 0.5f,
					};

					String name = json_find(object_node, S("name"), String);

					if (string_equals(name, S("top")))
						object->layer = DRAW_LAYER_FOREGROUND;
				}
			}
		}

		for (uint32_t index = 0; index < pstate->terrain_count; ++index) {
			pstate->terrain_quad_count[index] = arena_array_count(buffers[index]);
			pstate->terrain_vbos[index] =
				vulkan_buffer_make(pstate->context,
					BUFFER_TYPE_VERTEX, BUFFER_MEMORY_DEVICE,
					sizeof(float) * floats_per_quad * pstate->terrain_quad_count[index],
					buffers[index]);
		}

		// Object/Entity data
		JsonNode *layers = json_list(root, S("layers"));

		JsonNode *entities = json_find_where(layers, S("name"), S("Entities"));
		JsonNode *water = json_find_where(layers, S("name"), S("Water"));
		JsonNode *coast = json_find_where(layers, S("name"), S("Coast"));

		ArenaTrie entity_texture_lookup =
			arena_trie_make(scratch.arena);

		String character_folder = S("assets/pokemon/graphics/characters");
		for (JsonNode *entity = json_list(entities, S("objects")); entity; entity = entity->next) {
			String name = json_find(entity, S("name"), String);
			JsonNode *properties = json_list(entity, S("properties"));
			bool at_house = json_find_where(properties, S("value"), S("house")) != NULL;

			float entity_x = json_find(entity, S("x"), float);
			float entity_y = json_find(entity, S("y"), float);

			if (string_equals(S("Player"), name) && at_house) {
				LOG_INFO(SFMT ": %.2f, %.2f", SARG(name), entity_x, entity_y);
				pstate->player.position.x = entity_x;
				pstate->player.position.y = entity_y;
			} else if (string_equals(S("Character"), name)) {
				Sprite *object = arena_darray_push(scratch.arena, dyn, Sprite);

				JsonNode *graphic_property = json_find_where(json_list(entity, S("properties")), S("name"), S("graphic"));
				String graphic = json_find(graphic_property, S("value"), String);

				uint32_t offset = entity_texture_lookup.arena->offset;
				RhiTexture *texture = arena_trie_push(&entity_texture_lookup, span_string(graphic), RhiTexture);
				if (entity_texture_lookup.arena->offset != offset) {
					String file_path = stringpath_join(scratch.arena, character_folder, graphic);
					file_path = string_concat(scratch.arena, file_path, S(".png"));
					ImageSource grahpic_src = importer_load_image(scratch.arena, file_path);

					*texture = vulkan_texture_make(
						pstate->context, grahpic_src.width, grahpic_src.height,
						TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8_SRGB,
						TEXTURE_USAGE_SAMPLED, grahpic_src.pixels);
				}

				JsonNode *direction_property = json_find_where(json_list(entity, S("properties")), S("name"), S("direction"));
				String direction = json_find(direction_property, S("value"), String);

				Rectangle source = { .width = 128, .height = 128 };

				if (string_equals(S("down"), direction))
					source.y = source.height * 0;
				if (string_equals(S("left"), direction))
					source.y = source.height * 1;
				if (string_equals(S("right"), direction))
					source.y = source.height * 2;
				if (string_equals(S("up"), direction))
					source.y = source.height * 3;

				*object = (Sprite){
					.texture = *texture,
					.src = source,
					.position = {
					  .x = json_find(entity, S("x"), uint32_t),
					  .y = json_find(entity, S("y"), uint32_t),
					},
					.size = { source.width, source.height }
				};
				object->layer = DRAW_LAYER_WORLD;
				object->position.x += object->size.x * 0.5f;
				object->position.y -= object->size.y * 0.5f;

				object->collision_shape = (Rectangle){
					.x = 0,
					.y = -object->size.y * 0.25f,
					.width = object->size.x * 0.5f,
					.height = object->size.y * 0.4f,
				};

				object->origin = (float2){
					.x = object->size.x * 0.5f,
					.y = object->size.y * 0.5f,
				};
			}
		}

		pstate->entities = arena_array_copy(&pstate->arena, dyn, Sprite);

		AnimatedSprite *animated = NULL;
		for (JsonNode *node = json_list(water, S("objects")); node; node = node->next) {
			uint32_t width = json_find(node, S("width"), uint32_t);
			uint32_t height = json_find(node, S("height"), uint32_t);
			uint32_t x0 = json_find(node, S("x"), uint32_t);
			uint32_t x1 = x0 + width;
			uint32_t y0 = json_find(node, S("y"), uint32_t);
			uint32_t y1 = y0 + height;

			for (uint32_t y = y0; y < y1; y += 64) {
				for (uint32_t x = x0; x < x1; x += 64) {
					Rectangle source = { .x = 0.0f, .y = 0.0f, .width = 64, .height = 64 };
					Rectangle dest = { x, y, source.width, source.height };

					AnimatedSprite as = {
						.frames = pstate->water,
						.frame_count = countof(pstate->water),
						.src = source,
						.dest = dest,
					};

					arena_darray_put(scratch.arena, animated, AnimatedSprite, as);
				}
			}
		}
		pstate->animated_sprites = arena_array_copy(&pstate->arena, animated, AnimatedSprite);

		ImageSource coast_src = importer_load_image(scratch.arena, S("assets/pokemon/graphics/tilesets/coast.png"));
		RhiTexture coast_texture = vulkan_texture_make(pstate->context, coast_src.width, coast_src.height, TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8_SRGB, TEXTURE_USAGE_SAMPLED, coast_src.pixels);

		/*
		 * topleft = [ 0, 0 ]
		 * top = [ 1, 0 ]
		 * topright = [ 2, 0 ]
		 *
		 * left = [ 0, 1 ]
		 * right = [ 1, 1 ]
		 *
		 * bottomleft = [ 0, 2 ]
		 * bottom = [ 1, 2 ]
		 * bottomright = [ 2, 2 ]
		 */

		ArenaTrie coast_lookup = arena_trie_make(scratch.arena);
		arena_trie_store(&coast_lookup, uint32x2,
			{
			  { span_literal("topleft"), { 0, 0 } },
			  { span_literal("top"), { 1, 0 } },
			  { span_literal("topright"), { 2, 0 } },
			  { span_literal("left"), { 0, 1 } },
			  { span_literal("right"), { 2, 1 } },
			  { span_literal("bottomleft"), { 0, 2 } },
			  { span_literal("bottom"), { 1, 2 } },
			  { span_literal("bottomright"), { 2, 2 } },
			});
		arena_trie_store(&coast_lookup, uint32,
			{
			  { span_literal("grass"), 0 },
			  { span_literal("grass_i"), 1 },
			  { span_literal("sand_i"), 2 },
			  { span_literal("sand"), 3 },
			  { span_literal("rock"), 4 },
			  { span_literal("rock_i"), 5 },
			  { span_literal("ice"), 6 },
			  { span_literal("ice_i"), 7 },

			});

		pstate->coast = arena_array_make(&pstate->arena, json_list_count(coast, S("objects")), Sprite);
		for (JsonNode *node = json_list(coast, S("objects")); node; node = node->next) {
			uint32_t x = json_find(node, S("x"), uint32_t);
			uint32_t y = json_find(node, S("y"), uint32_t);
			uint32_t width = json_find(node, S("width"), uint32_t);
			uint32_t height = json_find(node, S("height"), uint32_t);

			JsonNode *properties = json_list(node, S("properties"));

			String sidestr = json_find(json_find_where(properties, S("name"), S("side")), S("value"), String);
			String terrainstr = json_find(json_find_where(properties, S("name"), S("terrain")), S("value"), String);

			uint32x2 side = *arena_trie_find(&coast_lookup, span_string(sidestr), uint32x2);
			uint32 terrain = *arena_trie_find(&coast_lookup, span_string(terrainstr), uint32) * 3;

			float x0 = (terrain * width) + side.x * width;
			float y0 = side.y * height;

			Rectangle source = { x0, y0, width, height };
			float2 pos = { x, y };
			float2 size = { width, height };

			arena_array_put(pstate->coast, Sprite, { .texture = coast_texture, .src = source, .position = pos, .size = size, .layer = DRAW_LAYER_BACKGROUND });
		}

		arena_scratch_end(scratch);

		pstate->camera = (Camera){
			.projection = CAMERA_PROJECTION_ORTHOGRAPHIC,
			.position = { 1300, 600, 0.f },
			.target = { 0.0f, 0.0f, 0.0f },
			.up = { 0.0f, 1.0f, 0.0f },
			.fov = 45.f,
		};

		// Animated sprites
		scratch = arena_scratch_begin(NULL);

		AssetTracker tracker = asset_tracker_make(scratch.arena);
		asset_tracker_track_directory(&tracker, S("assets/"));

		ImageSource water_frames[] = {
			importer_load_image(scratch.arena, arena_trie_find(&tracker.trie, span_literal("0.png"), AssetEntry)->full_path),
			importer_load_image(scratch.arena, arena_trie_find(&tracker.trie, span_literal("1.png"), AssetEntry)->full_path),
			importer_load_image(scratch.arena, arena_trie_find(&tracker.trie, span_literal("2.png"), AssetEntry)->full_path),
			importer_load_image(scratch.arena, arena_trie_find(&tracker.trie, span_literal("3.png"), AssetEntry)->full_path)
		};

		for (uint32_t index = 0; index < countof(pstate->water); ++index) {
			ImageSource frame = water_frames[index];
			pstate->water[index] = vulkan_texture_make(
				pstate->context, frame.width, frame.height,
				TEXTURE_TYPE_2D,
				TEXTURE_FORMAT_RGBA8_SRGB, TEXTURE_USAGE_SAMPLED,
				frame.pixels);
		}
		LOG_INFO("Asset count = %d", tracker.tracked_file_count);

		// PLAYER_INIT
		ImageSource player_src = importer_load_image(scratch.arena, S("assets/pokemon/graphics/characters/player.png"));
		pstate->player.src.width = (float)player_src.width / 4;
		pstate->player.src.height = (float)player_src.height / 4;
		pstate->player.size.x = (float)player_src.width / 4;
		pstate->player.size.y = (float)player_src.height / 4;
		pstate->player.origin = (float2){
			pstate->player.size.x * .5f,
			pstate->player.size.y * .5f,
		};

		pstate->player.collision_shape = (Rectangle){
			.x = 0,
			.y = -pstate->player.size.y * 0.33f,
			.width = pstate->player.size.x * 0.25f,
			.height = pstate->player.size.y * 0.25f,
		};

		pstate->player.texture = vulkan_texture_make(
			pstate->context, player_src.width, player_src.height,
			TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8_SRGB, TEXTURE_USAGE_SAMPLED,
			player_src.pixels);

		arena_scratch_end(scratch);

		pstate->is_initialized = true;
	}
	if (tstate->is_initialized == false) {
		tstate->transient = (Arena){
			.base = tstate + 1,
			.capacity = context->transient_memory_size - sizeof(TransientState)
		};

		tstate->is_initialized = true;
	}

	ArenaTemp scratch = arena_scratch_begin(NULL);
	DrawCommand *commands = arena_array_make(scratch.arena, MAX_DRAW_COMMANDS, DrawCommand);

	float2 input = {
		.x = input_key_down(KEY_CODE_D) - input_key_down(KEY_CODE_A),
		.y = input_key_down(KEY_CODE_S) - input_key_down(KEY_CODE_W),
	};
	input = float2_normalize(input);

	float speed = 100;
	if (input_key_down(KEY_CODE_LEFTSHIFT))
		speed = 1000;

	float2 old_position = {
		pstate->player.position.x - pstate->player.collision_shape.x,
		pstate->player.position.y - pstate->player.collision_shape.y,
	};
	float2 new_position = float2_add(old_position, float2_scale(input, speed * dt));
	float2 move_delta = float2_subtract(new_position, old_position);

	Rectangle player_collision_rect = rectangle(
		old_position,
		(float2){ pstate->player.collision_shape.width, pstate->player.collision_shape.height },
		(float2){ pstate->player.collision_shape.width * .5f, pstate->player.collision_shape.height * .5f });
	/* draw_rectangle_lines(commands, player_collision_rect, 5, DRAW_LAYER_DEBUG); */
	/* Rectangle player_origin_rect = rectangle(entity->position, (float2){ 5, 5 }, entity->origin); */
	/* draw_rectangle_lines(commands, entity_origin_rect, 5, DRAW_LAYER_DEBUG); */

	float t_remaining = 1.0f;
	for (uint32_t iteration = 0; iteration < 4 && t_remaining > 0.0f; ++iteration) {
		move_delta = float2_scale(move_delta, t_remaining);
		old_position = (float2){
			pstate->player.position.x - pstate->player.collision_shape.x,
			pstate->player.position.y - pstate->player.collision_shape.y,
		};

		Ray2HitResult result = RAY2_NO_HIT;

		for (uint32_t index = 0; index < arena_array_count(pstate->entities); ++index) {
			Sprite *entity = &pstate->entities[index];
			if (entity->layer > DRAW_LAYER_WORLD)
				continue;

			Rectangle entity_collision_rect = {
				.x = entity->position.x - entity->collision_shape.x - (entity->collision_shape.width * .5f),
				.y = entity->position.y - entity->collision_shape.y - (entity->collision_shape.height * .5f),
				.width = entity->collision_shape.width,
				.height = entity->collision_shape.height,
			};
			/* draw_rectangle_lines(commands, entity_collision_rect, 5, DRAW_LAYER_DEBUG); */

			float2 min = {
				entity_collision_rect.x - player_collision_rect.width * 0.5f,
				entity_collision_rect.y - player_collision_rect.height * 0.5f,
			};
			float2 max = {
				entity_collision_rect.x +
					entity_collision_rect.width +
					player_collision_rect.width * 0.5f,
				entity_collision_rect.y +
					entity_collision_rect.height +
					player_collision_rect.height * 0.5f,
			};

			if (rectangles_overlapping(player_collision_rect, entity_collision_rect)) { // This sometiems happens when moving fast. Stuck inside entity collsion shape. Can escape by moving around fast for a while
				draw_rectangle_lines(commands, player_collision_rect, 3, DRAW_LAYER_DEBUG);
				draw_rectangle_lines(commands, entity_collision_rect, 3, DRAW_LAYER_DEBUG);
			}

			/* Rectangle collision_shape_visual = { */
			/* 	min.x, min.y, max.x - min.x, max.y - min.y */
			/* }; */
			/* draw_rectangle_lines(commands, collision_shape_visual, 3, DRAW_LAYER_DEBUG); */

			Ray2HitResult temp = ray2_rectangle_intersection(old_position, move_delta, min, max);
			if (temp.t < result.t) {
				result = temp;
			}
		}

		float t_min = fminf(1.0f, result.t);

		pstate->player.position.x += (t_min - 0.001f) * move_delta.x;
		pstate->player.position.y += (t_min - 0.001f) * move_delta.y;

		move_delta = float2_subtract(move_delta, float2_scale(result.normal, float2_dot(move_delta, result.normal)));
		t_remaining -= t_min * t_remaining;
	}

	DrawCommand player_origin_marker = {
		.key = sort_key(DRAW_LAYER_DEBUG, 0, 0),
		.dest = {
		  .x = pstate->player.position.x,
		  .y = pstate->player.position.y,
		  .width = 5.f,
		  .height = 5.f,
		}
	};

	uint2 size = window_size_pixel(context->display);

	pstate->camera.position.x = MAX(pstate->player.position.x - size.x * .5f + 32, 0);
	pstate->camera.position.y = MAX(pstate->player.position.y - size.y * .5f + 32, 0);

	// Push to drawlist
	Sprite *p = &pstate->player;
	draw_texture(commands, p->texture, p->src, p->position, p->size, p->origin, DRAW_LAYER_WORLD);

	for (uint32_t index = 0; index < arena_array_count(pstate->entities); ++index) {
		Sprite s = pstate->entities[index];
		draw_texture(commands, s.texture, s.src, s.position, s.size, s.origin, s.layer);
	}

	static float t = 0.0f;
	t += dt;
	static uint32_t frame_index = 0;

	if (t >= 0.4f) {
		t = 0.0f;
		LOG_INFO("FPS: %.2f", 1 / dt);
		frame_index = (frame_index + 1) % 4;
	}

	if (float2_length(input) > 0) {
		if (input.y > 0)
			p->src.y = p->src.height * 0;
		if (input.y < 0)
			p->src.y = p->src.height * 3;
		if (input.x > 0)
			p->src.y = p->src.height * 2;
		if (input.x < 0)
			p->src.y = p->src.height * 1;

		p->src.x = p->src.width * frame_index;
	} else {
		p->src.x = 0;
	}

	for (uint32_t index = 0; index < arena_array_count(pstate->animated_sprites); ++index) {
		AnimatedSprite s = pstate->animated_sprites[index];
		uint32_t key = sort_key(DRAW_LAYER_BACKGROUND, 0, s.frames[frame_index].id);
		arena_array_put(commands, DrawCommand, { .texture = s.frames[frame_index], .dest = s.dest, .source = s.src });
	}
	for (uint32_t index = 0; index < arena_array_count(pstate->coast); ++index) {
		Sprite s = pstate->coast[index];
		s.src.y = s.src.y + (frame_index * (s.src.height * 3));
		draw_texture(commands, s.texture, s.src, s.position, s.size, s.origin, s.layer);
	}

	if (vulkan_frame_begin(pstate->context, size.x, size.y)) {
		// Write to this frames storage buffer
		uint32_t count = arena_array_count(commands);
		if (count > 0) {
			qsort(commands, count, sizeof(DrawCommand), sort_draw_commands);

			size_t floats_per_quad = 24;
			float *vertices = arena_push_count(scratch.arena, count * floats_per_quad, float);

			uint32_t quad_index = 0;
			for (uint32_t index = 0; index < count; ++index) {
				DrawCommand *cmd = &commands[index];
				float x0 = cmd->dest.x;
				float y0 = cmd->dest.y;
				float x1 = cmd->dest.x + cmd->dest.width;
				float y1 = cmd->dest.y + cmd->dest.height;

				float u0 = 0.0f;
				float v0 = 0.0f;
				float u1 = 1.0f;
				float v1 = 1.0f;

				if (cmd->texture.id) {
					uint32x2 tex_size = vulkan_texture_size(pstate->context, cmd->texture);
					u0 = cmd->source.x / tex_size.x;
					v0 = cmd->source.y / tex_size.y;
					u1 = (cmd->source.x + cmd->source.width) / tex_size.x;
					v1 = (cmd->source.y + cmd->source.height) / tex_size.y;
				}

				// clang-format off
                    float quad[] = {
                        // pos      // tex
                        x0,  y1, u0, v1,
                        x1,  y0, u1, v0,
                        x0,  y0, u0, v0, 

                        x0, y1, u0, v1,
                        x1, y1, u1, v1,
                        x1, y0, u1, v0
                    };
				// clang-format on

				memory_copy(&vertices[quad_index++ * floats_per_quad], quad, sizeof(quad));
			}

			vulkan_buffer_write(pstate->context, pstate->renderer.dynamic_ssbo, 0, count * floats_per_quad * sizeof(float), vertices);
		}

		DrawListDesc desc = {
			.color_attachments[0] = {
			  .clear = { .color = { .x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f } },
			},
			.color_attachment_count = 1
		};
		if (vulkan_drawlist_begin(pstate->context, desc)) {
			uint32_t drawcall_count = 0;
			(void)drawcall_count;
			Renderer2D *renderer = &pstate->renderer;
			Camera camera = pstate->camera;

			Matrix4f projection = float44_orthographic(0, size.x, 0, size.y, -1, 1);
			Matrix4f view = float44_translated(float3_negate(camera.position));
			vulkan_buffer_write(pstate->context, renderer->global_buffer, 0, sizeof(Matrix4f), &view);
			vulkan_buffer_write(pstate->context, renderer->global_buffer, sizeof(Matrix4f), sizeof(Matrix4f), &projection);

			PipelineDesc no_cull = pipeline_opt(.cull_mode = CULL_MODE_NONE);
			vulkan_shader_bind(pstate->context, renderer->static_shader, no_cull);

			RhiUniformSet set0 = vulkan_uniformset_push(pstate->context, renderer->static_shader, 0);
			vulkan_uniformset_bind_buffer(pstate->context, set0, 0, renderer->global_buffer);
			vulkan_uniformset_bind(pstate->context, set0);
			Matrix4f identity = float44_identity();
			vulkan_push_constants(pstate->context, 0, sizeof(Matrix4f), &identity);

			for (uint32_t index = 0; index < pstate->terrain_count; ++index) {
				RhiTexture texture = pstate->terrain_texture[index];
				RhiBuffer buffer = pstate->terrain_vbos[index];
				uint32_t quad_count = pstate->terrain_quad_count[index];
				RhiUniformSet set1 = vulkan_uniformset_push(pstate->context, pstate->renderer.dynamic_shader, 1);
				vulkan_uniformset_bind_texture(pstate->context, set1, 1, texture, pstate->renderer.nearest);
				vulkan_uniformset_bind(pstate->context, set1);
				vulkan_buffer_bind(pstate->context, buffer, 0);
				vulkan_renderer_draw(pstate->context, quad_count * 6);
				drawcall_count++;
			}

			vulkan_shader_bind(pstate->context, renderer->dynamic_shader, no_cull);

			set0 = vulkan_uniformset_push(pstate->context, renderer->dynamic_shader, 0);
			vulkan_uniformset_bind_buffer(pstate->context, set0, 0, renderer->global_buffer);
			vulkan_uniformset_bind_buffer(pstate->context, set0, 1, renderer->dynamic_ssbo);
			vulkan_uniformset_bind(pstate->context, set0);

			uint32_t quad_index = 0;
			uint32_t batch_start_quad = 0;

			vulkan_push_constants(pstate->context, 0, sizeof(Matrix4f), &identity);

			for (uint32_t index = 0; index < count; ++index) {
				DrawCommand *cmd = &commands[index];

				bool is_last = (index == count - 1);
				quad_index++;

				if (is_last || commands[index + 1].texture.id != cmd->texture.id) {
					uint32_t batch_quad_count = quad_index - batch_start_quad;

					RhiUniformSet set1 = vulkan_uniformset_push(pstate->context, pstate->renderer.dynamic_shader, 1);
					if (cmd->texture.id)
						vulkan_uniformset_bind_texture(pstate->context, set1, 1, cmd->texture, pstate->renderer.nearest);
					else
						vulkan_uniformset_bind_texture(pstate->context, set1, 1, pstate->renderer.white, pstate->renderer.nearest);

					vulkan_uniformset_bind(pstate->context, set1);
					vulkan_renderer_draw_offset(pstate->context, batch_quad_count * 6, batch_start_quad * 6);
					drawcall_count++;

					batch_start_quad = quad_index;
				}
			}

			if (t == 0.0f) {
				LOG_INFO("Drawcalls = %d", drawcall_count);
			}

			vulkan_drawlist_end(pstate->context);
		}

		vulkan_frame_end(pstate->context);
	}
	arena_scratch_end(scratch);

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

	result.dynamic_shader = vulkan_shader_make(
		scratch.arena,
		pstate->context,
		importer_load_shader(scratch.arena, S("assets/shaders/dynamic.vert.spv"), S("assets/shaders/dynamic.frag.spv")),
		NULL);

	result.static_shader = vulkan_shader_make(
		scratch.arena,
		pstate->context,
		importer_load_shader(scratch.arena, S("assets/shaders/static.vert.spv"), S("assets/shaders/static.frag.spv")),
		NULL);
	arena_scratch_end(scratch);

	result.global_buffer = vulkan_buffer_make(pstate->context, BUFFER_TYPE_UNIFORM, BUFFER_MEMORY_SHARED, sizeof(Matrix4f) * 2, NULL);

	uint32_t buffer_size = MAX_DRAW_COMMANDS * 24 * sizeof(float);
	// vulkan_buffer_make(pstate->context, BUFFER_TYPE_STORAGE, MAX_DRAW_COMMANDS, STRIDE, NULL);
	result.dynamic_ssbo = vulkan_buffer_make(pstate->context, BUFFER_TYPE_STORAGE, BUFFER_MEMORY_SHARED, buffer_size, NULL);

	/* // clang-format off */
	/* float vertices[] = {  */
	/* // pos      // tex */
	/* 0.0f, 1.0f, 0.0f, 1.0f, */
	/* 1.0f, 0.0f, 1.0f, 0.0f, */
	/* 0.0f, 0.0f, 0.0f, 0.0f,  */

	/* 0.0f, 1.0f, 0.0f, 1.0f, */
	/* 1.0f, 1.0f, 1.0f, 1.0f, */
	/* 1.0f, 0.0f, 1.0f, 0.0f */
	/* }; */
	/* // clang-format on */
	/* result.vbo = vulkan_buffer_make(pstate->context, BUFFER_TYPE_VERTEX, BUFFER_MEMORY_DEVICE, sizeof(vertices), vertices); */

	return result;
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

void draw_rectangle_lines(DrawCommand *commands, Rectangle rect, float thickness, uint32_t layer) {
	DrawCommand outline[] = {
		{
		  .key = sort_key(layer, 0, 0),
		  .dest = {
			.x = rect.x,
			.y = rect.y,
			.width = rect.width,
			.height = thickness,
		  },
		},
		{
		  .key = sort_key(layer, 0, 0),
		  .dest = {
			.x = rect.x,
			.y = rect.y,
			.width = thickness,
			.height = rect.height,
		  },
		},

		{
		  .key = sort_key(layer, 0, 0),
		  .dest = {
			.x = rect.x + rect.width,
			.y = rect.y,
			.width = thickness,
			.height = rect.height + thickness,
		  },
		},
		{
		  .key = sort_key(layer, 0, 0),
		  .dest = {
			.x = rect.x,
			.y = rect.y + rect.height,
			.width = rect.width + thickness,
			.height = thickness,
		  },
		},
	};
	for (uint32_t index = 0; index < countof(outline); ++index)
		arena_array_put(commands, DrawCommand, outline[index]);
}

void draw_texture(DrawCommand *commands, RhiTexture texture, Rectangle src, float2 position, float2 size, float2 origin, uint32_t layer) {
	uint64_t player_sort_key = sort_key(
		layer,
		(uint32_t)(position.y + size.y * .5f),
		texture.id);

	Rectangle dest = rectangle(position, size, origin);

	arena_array_put(commands, DrawCommand, { .texture = texture, .dest = dest, .source = src, .key = player_sort_key });
}
