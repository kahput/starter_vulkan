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

#include <renderer.h>
#include <renderer/r_internal.h>
#include <renderer/backend/vulkan_api.h>

#include <assets.h>
#include <assets/mesh_source.h>
#include <assets/asset_types.h>

#define MAX_DRAW_COMMANDS 65536

typedef struct {
	float32 x, y;
	float32 width, height;
} Rectangle;

typedef struct {
	RhiTexture texture;
	Rectangle source, dest;
} DrawCommand;

typedef struct {
	RhiTexture white;

	RhiSampler linear;
	RhiSampler nearest;

	RhiShader dynamic_shader;
	RhiShader static_shader;

	RhiBuffer global_buffer;

	DrawCommand *draw_list;
	RhiBuffer dynamic_ssbo;
} Renderer2D;

// NOTE: Entity
typedef Vector4f Color;
typedef struct {
	RhiTexture texture;

	Rectangle src, dest;
	Vector2f origin;
	float rotation;
	Color color;
} Sprite;

typedef struct SpriteMap SpriteMap;
struct SpriteMap {
	SpriteMap *children[4];
	uint32_t key;
	Sprite value;
};

typedef struct {
	RhiTexture *frames;
	uint32_t frame_count;

	Rectangle source, dest;
	Vector2f origin;
	float rotation;
	Color color;
} AnimatedSprite;

typedef struct {
	Arena arena;
	bool is_initialized;

	VulkanContext *context;
	Window *display;

	Renderer2D renderer;
	RhiTexture sprite0, sprite1;

	Sprite *tile_lookup;
	// TERRRAIN
	RhiTexture terrain_texture[8];
	RhiBuffer terrain_vbos[8];
	uint32_t terrain_quad_count[8];
	uint32_t terrain_count;
	uint32_t map_width, map_height;

	// Objects
	Sprite *objects;

	Sprite *coast;
	Camera camera;

	Sprite player;

	AnimatedSprite *animated_sprites;

	RhiTexture water[4];

} PermanentState;

typedef struct {
	Arena transient;
	bool is_initialized;

} TransientState;

static PermanentState *pstate = NULL;
static TransientState *tstate = NULL;

Renderer2D renderer_make(void);
RhiTexture texture_make(Renderer2D *renderer, String path);
void texture_draw(Renderer2D *renderer, RhiTexture texture, Rectangle source, Rectangle dest, Vector2f origin, float32 rotation, Color tint);
static int sort_draw_commands(const void *a, const void *b) { return ((DrawCommand *)a)->texture.id - ((DrawCommand *)b)->texture.id; }

Sprite *sprite_upsert(Arena *arena, SpriteMap **map, uint32_t key) {
	for (uint64_t hash = hash_struct(key); *map; hash <<= 2) {
		if (key == (*map)->key) {
			return &(*map)->value;
		}

		map = &(*map)->children[hash >> 62];
	}

	if (arena == NULL) {
		return 0;
	}

	*map = arena_push_struct(arena, SpriteMap);
	(*map)->key = key;
	return &(*map)->value;
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
		SpriteMap *tilemap = NULL;
		String map_path = S("assets/pokemon/data/maps/world.tmj");

		String source = filesystem_read(scratch.arena, map_path);
		JsonNode *root = json_parse(scratch.arena, source);

		uint32_t tile_count = 0;
		// Tilesets
		for (JsonNode *map_tileset = json_list(root, S("tilesets")); map_tileset; map_tileset = map_tileset->next) {
			String source = json_find(map_tileset, S("source"), String);
			uint32_t firstgid = json_find(map_tileset, S("firstgid"), uint32_t);

			source = stringpath_join(scratch.arena, stringpath_directory(map_path), source);
			source = stringpath_normalize(scratch.arena, source);

			source = filesystem_read(scratch.arena, source);
			JsonNode *tileset = json_parse(scratch.arena, source);
			tile_count += json_find(tileset, S("tilecount"), uint32_t);

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

				uint32_t tilewidth = json_find(tileset, S("tilewidth"), float32);
				uint32_t tileheight = json_find(tileset, S("tileheight"), float32);

				uint32_t columns = json_find(tileset, S("columns"), uint32_t);
				uint32_t rows = image_src.height / tileheight;

				for (uint32_t index = 0; index < json_find(tileset, S("tilecount"), uint32_t); ++index) {
					uint32_t tile_id = index + firstgid;
					uint32_t atlas_gridx = (tile_id - firstgid) % columns;
					uint32_t atlas_gridy = (tile_id - firstgid) / columns;

					Sprite tile = {
						.texture = texture,
						.src = {
						  .x = atlas_gridx * tilewidth,
						  .y = atlas_gridy * tileheight,
						  .width = tilewidth,
						  .height = tileheight,
						},
						.dest = { .width = tilewidth, .height = tileheight },
					};

					*sprite_upsert(scratch.arena, &tilemap, index + firstgid) = tile;
				}

			} else {
				for (JsonNode *image_node = json_list(tileset, S("tiles")); image_node; image_node = image_node->next) {
					uint32_t id = json_find(image_node, S("id"), uint32_t);
					String image_path = json_find(image_node, S("image"), String);

					image_path = stringpath_normalize(scratch.arena, image_path);
					image_path = stringpath_join(scratch.arena, stringpath_directory(map_path), image_path);
					ImageSource image_src = importer_load_image(scratch.arena, image_path);

					uint32_t tile_id = id + firstgid;

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
						.dest = source,
					};

					*sprite_upsert(scratch.arena, &tilemap, id + firstgid) = tile;

					// NOTE: id in tsj (tileset file) is local. Id repeats for each tileset.
					// Not guaranteed to be sequential either (can't use id == index)
				}
			}
		}

		pstate->tile_lookup = arena_array_make(&pstate->arena, tile_count + 1, Sprite);
		for (uint32_t index = 1; index < tile_count; ++index) {
			Sprite *tile = sprite_upsert(NULL, &tilemap, index);
			if (tile)
				pstate->tile_lookup[index] = *tile;
		}

		// Tile data
		pstate->map_width = json_find(root, S("width"), uint32_t);
		pstate->map_height = json_find(root, S("height"), uint32_t);

		Sprite *dyn = arena_array_make(scratch.arena, 512, Sprite);

		float32 *buffers[8] = { 0 };
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

					uint32_2 image_size = vulkan_texture_size(pstate->context, tile->texture);
					float u0 = tile->src.x / image_size.x;
					float v0 = tile->src.y / image_size.y;
					float u1 = (tile->src.x + tile->src.width) / image_size.x;
					float v1 = (tile->src.y + tile->src.height) / image_size.y;

					float32 x0 = x * tile->dest.width;
					float32 y0 = y * tile->dest.width;
					float32 x1 = x0 + tile->dest.width;
					float32 y1 = y0 + tile->dest.width;

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

					ArenaTrieNode *node = NULL;
					if ((node = arena_trienode_find(&buffer_lookup, hash_struct(tile->texture))) == NULL) {
						uint32_t current = pstate->terrain_count++;

						node = arena_trienode_push(&buffer_lookup, hash_struct(tile->texture));
						buffers[current] = arena_array_make(scratch.arena, floats_per_quad * pstate->map_width * pstate->map_height, float32);
						node->payload = buffers[current];
						pstate->terrain_texture[current] = tile->texture;
					}

					float32 *buffer = node->payload;
					memcpy(buffer + floats_per_quad * arena_array_count(buffer), quad, sizeof(quad));
					HEADER(node->payload, ArenaArrayHeader)->count++;
				}

			} else if (string_equals(S("objectgroup"), type)) {
				String name = json_find(layer, S("name"), String);
				if (string_equals(S("Objects"), name) == false)
					continue;

				for (JsonNode *object_node = json_list(layer, S("objects")); object_node; object_node = object_node->next) {
					uint32_t gid = 0;
					if ((gid = json_find(object_node, S("gid"), uint32_t) == 0))
						break;

					Sprite *object = arena_darray_push(scratch.arena, dyn, Sprite);
					Sprite *tile = &pstate->tile_lookup[json_find(object_node, S("gid"), uint32_t)];

					if (json_find(object_node, S("id"), uint32_t) == 210) {
						uint32_t x = 3;
					}

					*object = (Sprite){
						.texture = tile->texture,
						.src = tile->src,
						.dest = {
						  .x = json_find(object_node, S("x"), float32),
						  .y = json_find(object_node, S("y"), float32),
						  .width = tile->dest.width,
						  .height = tile->dest.height,
						},
					};
					object->dest.y -= object->dest.height;
				}
			}
		}

		for (uint32_t index = 0; index < pstate->terrain_count; ++index) {
			pstate->terrain_quad_count[index] = arena_array_count(buffers[index]);
			pstate->terrain_vbos[index] =
				vulkan_buffer_make(pstate->context,
					BUFFER_TYPE_VERTEX, BUFFER_MEMORY_DEVICE,
					sizeof(float32) * floats_per_quad * pstate->terrain_quad_count[index],
					buffers[index]);
		}

		// Other object data
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

			float32 entity_x = json_find(entity, S("x"), float32);
			float32 entity_y = json_find(entity, S("y"), float32);

			if (string_equals(S("Player"), name) && at_house) {
				LOG_INFO(SFMT ": %.2f, %.2f", SARG(name), entity_x, entity_y);
				pstate->player.dest.x = entity_x;
				pstate->player.dest.y = entity_y;
			} else if (string_equals(S("Character"), name)) {
				Sprite *object = arena_darray_push(scratch.arena, dyn, Sprite);

				JsonNode *graphic_property = json_find_where(json_list(entity, S("properties")), S("name"), S("graphic"));
				String graphic = json_find(graphic_property, S("value"), String);

				uint32_t hash = string_hash64(graphic);
				RhiTexture *texture = arena_trie_find(&entity_texture_lookup, hash, RhiTexture);
				if (texture == NULL) {
					texture = arena_trie_push(&entity_texture_lookup, hash, RhiTexture);

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
					.dest = {
					  .x = json_find(entity, S("x"), uint32_t),
					  .y = json_find(entity, S("y"), uint32_t),
					  .width = source.width,
					  .height = source.height },
				};
			}
		}

		pstate->objects = arena_array_copy(&pstate->arena, dyn, Sprite);

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

					arena_darray_put(scratch.arena, animated, AnimatedSprite,
						{ .frames = pstate->water, .frame_count = countof(pstate->water), .source = source, .dest = dest });
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

		ArenaTrie lookup = arena_trie_make(scratch.arena);
		arena_trie_put(&lookup, shash("topleft"), uint32_2, { 0, 0 });
		arena_trie_put(&lookup, shash("top"), uint32_2, { 1, 0 });
		arena_trie_put(&lookup, shash("topright"), uint32_2, { 2, 0 });

		arena_trie_put(&lookup, shash("left"), uint32_2, { 0, 1 });
		arena_trie_put(&lookup, shash("right"), uint32_2, { 2, 1 });

		arena_trie_put(&lookup, shash("bottomleft"), uint32_2, { 0, 2 });
		arena_trie_put(&lookup, shash("bottom"), uint32_2, { 1, 2 });
		arena_trie_put(&lookup, shash("bottomright"), uint32_2, { 2, 2 });

		arena_trie_put(&lookup, shash("grass"), uint32, 0);
		arena_trie_put(&lookup, shash("grass_i"), uint32, 1);

		arena_trie_put(&lookup, shash("sand_i"), uint32, 2);
		arena_trie_put(&lookup, shash("sand"), uint32, 3);

		arena_trie_put(&lookup, shash("rock"), uint32, 4);
		arena_trie_put(&lookup, shash("rock_i"), uint32, 5);

		arena_trie_put(&lookup, shash("ice"), uint32, 6);
		arena_trie_put(&lookup, shash("ice_i"), uint32, 7);

		pstate->coast = arena_array_make(&pstate->arena, json_list_count(coast, S("objects")), Sprite);
		for (JsonNode *node = json_list(coast, S("objects")); node; node = node->next) {
			uint32_t x = json_find(node, S("x"), uint32_t);
			uint32_t y = json_find(node, S("y"), uint32_t);
			uint32_t width = json_find(node, S("width"), uint32_t);
			uint32_t height = json_find(node, S("height"), uint32_t);

			JsonNode *properties = json_list(node, S("properties"));

			String sidestr = json_find(json_find_where(properties, S("name"), S("side")), S("value"), String);
			String terrainstr = json_find(json_find_where(properties, S("name"), S("terrain")), S("value"), String);

			uint32_2 side = *arena_trie_find(&lookup, string_hash64(sidestr), uint32_2);
			uint32_t terrain = *arena_trie_find(&lookup, string_hash64(terrainstr), uint32) * 3;

			float32 x0 = (terrain * width) + side.x * width;
			float y0 = side.y * height;

			Rectangle source = { x0, y0, width, height };
			Rectangle dest = { x, y, width, height };

			arena_array_put(pstate->coast, Sprite, { .texture = coast_texture, .src = source, .dest = dest });
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

		// Animated sprites
		scratch = arena_scratch_begin(NULL);

		AssetTracker tracker = asset_tracker_make(scratch.arena);
		asset_tracker_track_directory(&tracker, S("assets/"));

		ImageSource water_frames[] = {
			importer_load_image(scratch.arena, arena_trie_find(&tracker.trie, shash("0.png"), AssetEntry)->full_path),
			importer_load_image(scratch.arena, arena_trie_find(&tracker.trie, shash("1.png"), AssetEntry)->full_path),
			importer_load_image(scratch.arena, arena_trie_find(&tracker.trie, shash("2.png"), AssetEntry)->full_path),
			importer_load_image(scratch.arena, arena_trie_find(&tracker.trie, shash("3.png"), AssetEntry)->full_path)
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

		ImageSource player_src = importer_load_image(scratch.arena, S("assets/pokemon/graphics/characters/player.png"));
		pstate->player.src.width = (float32)player_src.width / 4;
		pstate->player.src.height = (float32)player_src.height / 4;
		pstate->player.dest.width = (float32)player_src.width / 4;
		pstate->player.dest.height = (float32)player_src.height / 4;
		pstate->player.texture = vulkan_texture_make(
			pstate->context, player_src.width, player_src.height,
			TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8_SRGB, TEXTURE_USAGE_SAMPLED,
			player_src.pixels);

		arena_scratch_end(scratch);

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

	pstate->player.dest.x += input.x * speed * dt;
	pstate->player.dest.y += input.y * speed * dt;

	uint32_2 size = window_size_pixel(context->display);

	pstate->camera.position.x = MAX(pstate->player.dest.x - size.x * .5f + 32, 0);
	pstate->camera.position.y = MAX(pstate->player.dest.y - size.y * .5f + 32, 0);

	ArenaTemp scratch = arena_scratch_begin(NULL);
	pstate->renderer.draw_list = arena_array_make(scratch.arena, MAX_DRAW_COMMANDS, DrawCommand);

	// Push to drawlist
	Rectangle source = { .x = 0, .y = 0, .width = 16, .height = 16 };
	Rectangle dest = { .x = 0.0f, .y = 0.0f, .width = 64, .height = 64 };
	texture_draw(&pstate->renderer, pstate->sprite1, source, dest, (Vector2f){ 0 }, 0.0f, (Color){ 0.0f, 1.0f, 1.0f, 1.0f });

	Sprite *p = &pstate->player;

	texture_draw(&pstate->renderer, p->texture, p->src, p->dest, (Vector2f){ 0 }, 0.0f, (Color){ 0.0f, 1.0f, 1.0f, 1.0f });

	for (uint32_t index = 0; index < arena_array_count(pstate->objects); ++index) {
		Sprite s = pstate->objects[index];
		texture_draw(&pstate->renderer, s.texture, s.src, s.dest, s.origin, s.rotation, (Color){ 1.0f, 1.0f, 1.0f, 1.0f });
	}

	static float32 t = 0.0f;
	static uint32_t frame_index = 0;

	if (t >= 0.4f) {
		t = 0.0f;
		LOG_INFO("FPS: %.2f", 1 / dt);
		frame_index = (frame_index + 1) % 4;
	}

	if (vector2f_length(input) > 0) {
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

	t += dt;
	for (uint32_t index = 0; index < arena_array_count(pstate->animated_sprites); ++index) {
		AnimatedSprite s = pstate->animated_sprites[index];
		texture_draw(&pstate->renderer, s.frames[frame_index], s.source, s.dest, s.origin, s.rotation, (Vector4f){ 1.0f, 1.0f, 1.0f, 1.0f });
	}
	for (uint32_t index = 0; index < arena_array_count(pstate->coast); ++index) { // Actually also animated, but only single image
		Sprite s = pstate->coast[index];
		s.src.y = s.src.y + (frame_index * (s.src.height * 3));
		texture_draw(&pstate->renderer, s.texture, s.src, s.dest, s.origin, s.rotation, (Color){ 1.0f, 1.0f, 1.0f, 1.0f });
	}

	if (vulkan_frame_begin(pstate->context, size.x, size.y)) {
		// Create single large vbo
		uint32_t count = arena_array_count(pstate->renderer.draw_list);
		if (count > 0) {
			qsort(pstate->renderer.draw_list, count, sizeof(DrawCommand), sort_draw_commands);

			size_t floats_per_quad = 24;
			float32 *vertices = arena_push_count(scratch.arena, count * floats_per_quad, float32);

			uint32_t quad_index = 0;
			for (uint32_t index = 0; index < count; ++index) {
				DrawCommand *cmd = &pstate->renderer.draw_list[index];

				uint32_2 tex_size = vulkan_texture_size(pstate->context, cmd->texture);
				float u0 = cmd->source.x / tex_size.x;
				float v0 = cmd->source.y / tex_size.y;
				float u1 = (cmd->source.x + cmd->source.width) / tex_size.x;
				float v1 = (cmd->source.y + cmd->source.height) / tex_size.y;

				float32 x0 = cmd->dest.x;
				float32 y0 = cmd->dest.y;
				float32 x1 = cmd->dest.x + cmd->dest.width;
				float32 y1 = cmd->dest.y + cmd->dest.height;
				if (cmd->texture.id == 35) {
					uint32_t x = 0;
					(void)x;
				}

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
				memory_copy(&vertices[quad_index++ * floats_per_quad], quad, sizeof(quad));
			}

			vulkan_buffer_write(pstate->context, pstate->renderer.dynamic_ssbo, 0, count * 24 * sizeof(float32), vertices);
		}

		DrawListDesc desc = {
			.color_attachments[0] = {
			  .clear = { .color = { .x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f } },
			},
			.color_attachment_count = 1
		};
		if (vulkan_drawlist_begin(pstate->context, desc)) {
			Renderer2D *renderer = &pstate->renderer;
			Camera camera = pstate->camera;

			Matrix4f projection = matrix4f_orthographic(0, size.x, 0, size.y, -1, 1);
			Matrix4f view = matrix4f_translated(vector3f_negate(camera.position));
			vulkan_buffer_write(pstate->context, renderer->global_buffer, 0, sizeof(Matrix4f), &view);
			vulkan_buffer_write(pstate->context, renderer->global_buffer, sizeof(Matrix4f), sizeof(Matrix4f), &projection);

			PipelineDesc no_cull = DEFAULT_PIPELINE;
			no_cull.cull_mode = CULL_MODE_NONE;
			vulkan_shader_bind(pstate->context, renderer->static_shader, no_cull);

			RhiUniformSet set0 = vulkan_uniformset_push(pstate->context, renderer->static_shader, 0);
			vulkan_uniformset_bind_buffer(pstate->context, set0, 0, renderer->global_buffer);
			vulkan_uniformset_bind(pstate->context, set0);
			Matrix4f identity = matrix4f_identity();
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
				DrawCommand *cmd = &pstate->renderer.draw_list[index];

				bool is_last = (index == count - 1);
				quad_index++;

				if (is_last || pstate->renderer.draw_list[index + 1].texture.id != cmd->texture.id) {
					uint32_t batch_quad_count = quad_index - batch_start_quad;

					RhiUniformSet set1 = vulkan_uniformset_push(pstate->context, pstate->renderer.dynamic_shader, 1);
					vulkan_uniformset_bind_texture(pstate->context, set1, 1, cmd->texture, pstate->renderer.nearest);
					vulkan_uniformset_bind(pstate->context, set1);
					vulkan_renderer_draw_offset(pstate->context, batch_quad_count * 6, batch_start_quad * 6);

					batch_start_quad = quad_index;
				}
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

void texture_draw(Renderer2D *renderer, RhiTexture texture, Rectangle source, Rectangle dest, Vector2f origin, float32 rotation, Color tint) {
	arena_array_put(pstate->renderer.draw_list, DrawCommand, { .texture = texture, .dest = dest, .source = source });
}
