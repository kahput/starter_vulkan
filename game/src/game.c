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
	Arena arena;
	bool is_initialized;

	VulkanContext *context;
	Window *display;

	Renderer2D renderer;
	Sprite sprite0, sprite1;

	// TERRRAIN
	RhiBuffer terrain_ubo;
	RhiTexture terrain_atlas;
	RhiBuffer terrain_vbo;
	uint32_t terrain_columns;
	uint32_t terrain_id_offset;

	uint32_t tile_width, tile_height;

	uint32_t terrain_width, terrain_height;
	uint32_t *terrain_layer_data;

	// Objects
	uint32_t object_texture_count;

	uint32_t *object_gids;
	Vector3f *object_positions;
	Vector2f *object_dimensions;
	ArenaTrie object_lookup;
	uint32_t object_count;

	Camera camera;
	Vector3f player_position;

} PermanentState;

typedef struct {
	Arena transient;
	bool is_initialized;

} TransientState;

static PermanentState *p_state = NULL;
static TransientState *t_state = NULL;

Renderer2D renderer_make(void);
bool frame_begin(Renderer2D *renderer, Camera camera);
void frame_end(Renderer2D *renderer);
Sprite sprite_make(Renderer2D *renderer, String path);
void texture_draw(Renderer2D *renderer, Sprite sprite, Vector3f position, Vector3f scale, Vector3f tint);

FrameInfo game_on_update_and_render(GameContext *context, float dt) {
	p_state = (PermanentState *)context->permanent_memory;
	t_state = (TransientState *)context->transient_memory;

	ArenaTemp scratch = arena_scratch(NULL);
	if (p_state->is_initialized == false) {
		p_state->context = context->vk_context;
		p_state->display = context->display;
		p_state->arena = (Arena){
			.memory = p_state + 1,
			.offset = 0,
			.capacity = context->permanent_memory_size - sizeof(PermanentState)
		};

		p_state->renderer = renderer_make();
		p_state->sprite0 = sprite_make(&p_state->renderer, S("assets/sprites/kenney/tile_0085.png"));
		p_state->sprite1 = sprite_make(&p_state->renderer, S("assets/sprites/kenney/tile_0086.png"));

		ArenaTemp scratch = arena_scratch(NULL);
		String map_path = S("assets/pokemon/data/maps/world.tmj");

		String source = filesystem_read(scratch.arena, map_path);
		JsonNode *root = json_parse(scratch.arena, source);

		p_state->terrain_columns = 0;
		uint32_t columns, rows;
		ImageSource world_atlas = { 0 };
		(void)world_atlas;
		p_state->object_lookup = arena_trie_make(&p_state->arena);

		for (JsonNode *tileset_meta = json_list(root, S("tilesets")); tileset_meta; tileset_meta = tileset_meta->next) {
			String source = json_find(tileset_meta, S("source"), String);
			uint32_t firstgid = json_find(tileset_meta, S("firstgid"), uint32_t);

			source = string_path_join(scratch.arena, string_path_directory(map_path), source);
			source = string_path_clean(scratch.arena, source);

			source = filesystem_read(scratch.arena, source);
			JsonNode *tileset = json_parse(scratch.arena, source);

			if (json_find(tileset, S("columns"), uint32_t)) {
				p_state->terrain_id_offset = firstgid;
				p_state->tile_width = json_find(tileset, S("tilewidth"), uint32_t);
				p_state->tile_height = json_find(tileset, S("tileheight"), uint32_t);

				String image_path = json_find(tileset, S("image"), String);
				image_path = string_path_clean(scratch.arena, image_path);
				image_path = string_path_join(scratch.arena, string_path_directory(map_path), image_path);
				LOG_INFO("ImagePath: " SFMT, SARG(image_path));

				world_atlas = importer_load_image(scratch.arena, image_path);

				columns = json_find(tileset, S("columns"), uint32_t);
				rows = world_atlas.height / p_state->tile_height;
			} else {
				for (JsonNode *image_node = json_list(tileset, S("tiles")); image_node; image_node = image_node->next) {
					String image_path = json_find(image_node, S("image"), String);
					image_path = string_path_clean(scratch.arena, image_path);
					image_path = string_path_join(scratch.arena, string_path_directory(map_path), image_path);
					/* LOG_INFO("ImagePath: " SFMT, SARG(image_path)); */

					ImageSource image_src = importer_load_image(scratch.arena, image_path);

					// NOTE: id in tsj (tileset file) is local. Id repeats for each tileset.
					// Not guaranteed to be sequential either (can't use id == index)
					RhiTexture *texture = arena_trie_push(&p_state->object_lookup, json_find(image_node, S("id"), uint32_t) + firstgid, RhiTexture);
					*texture =
						vulkan_texture_make(context->vk_context,
							image_src.width, image_src.height,
							TEXTURE_TYPE_2D,
							TEXTURE_FORMAT_RGBA8_SRGB, TEXTURE_USAGE_SAMPLED,
							image_src.pixels);
				}
			}
		}

		JsonNode *layers = json_list(root, S("layers"));

		JsonNode *terrain = json_find_where(layers, S("name"), S("Terrain"));
		JsonNode *entities = json_find_where(layers, S("name"), S("Entities"));
		JsonNode *objects = json_find_where(layers, S("name"), S("Objects"));

		p_state->terrain_width = json_find(terrain, S("width"), uint32_t);
		p_state->terrain_height = json_find(terrain, S("height"), uint32_t);

		uint32_t index = 0;
		p_state->terrain_layer_data = arena_push_count(scratch.arena, p_state->terrain_width * p_state->terrain_height, uint32_t);
		for (JsonNode *data_node = json_list(terrain, S("data")); data_node; data_node = data_node->next)
			p_state->terrain_layer_data[index++] = json_as(data_node, uint32_t);

		for (JsonNode *entity = json_list(entities, S("objects")); entity; entity = entity->next) {
			String name = json_find(entity, S("name"), String);
			JsonNode *properties = json_list(entity, S("properties"));
			bool at_house = json_find_where(properties, S("value"), S("house")) != NULL;

			float32 entity_x = json_find(entity, S("x"), float32);
			float32 entity_y = json_find(entity, S("y"), float32);

			if (string_equals(S("Player"), name) && at_house) {
				LOG_INFO(SFMT ": %.2f, %.2f", SARG(name), entity_x, entity_y);
				p_state->player_position.x = entity_x;
				p_state->player_position.y = entity_y;
			}
		}

		for (JsonNode *object = json_list(objects, S("objects")); object; object = object->next)
			p_state->object_count++;
		LOG_INFO("Object count = %d", p_state->object_count);

		p_state->object_gids = arena_push_count(&p_state->arena, p_state->object_count, uint32_t);
		p_state->object_dimensions = arena_push_count(&p_state->arena, p_state->object_count, Vector2f);
		p_state->object_positions = arena_push_count(&p_state->arena, p_state->object_count, Vector3f);
		index = 0;
		for (JsonNode *object = json_list(objects, S("objects")); object; object = object->next) {
			p_state->object_gids[index] = json_find(object, S("gid"), uint32_t);
			p_state->object_dimensions[index] = (Vector2f){
				.x = json_find(object, S("width"), float32),
				.y = json_find(object, S("height"), float32),
			};
			p_state->object_positions[index++] = (Vector3f){
				.x = json_find(object, S("x"), float32),
				.y = json_find(object, S("y"), float32),
				.z = 0.0f
			};
		}

		float32 *map_batch = arena_push_count(scratch.arena, p_state->terrain_width * p_state->terrain_height * 24, float32);
		for (uint32_t y = 0; y < p_state->terrain_height; ++y) {
			for (uint32_t x = 0; x < p_state->terrain_width; ++x) {
				uint32_t index = y * p_state->terrain_width + x;

				uint32_t tile_id = p_state->terrain_layer_data[index];

				uint32_t atlas_grid_x = (tile_id - p_state->terrain_id_offset) % columns;
				uint32_t atlas_grid_y = (tile_id - p_state->terrain_id_offset) / columns;

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

		p_state->terrain_vbo = vulkan_buffer_make(context->vk_context, BUFFER_TYPE_VERTEX, sizeof(float) * 24 * p_state->terrain_width * p_state->terrain_height, map_batch);
		p_state->terrain_ubo = vulkan_buffer_make(p_state->context, BUFFER_TYPE_UNIFORM, sizeof(Vector4f), NULL);
		p_state->terrain_atlas = vulkan_texture_make(
			p_state->context,
			world_atlas.width, world_atlas.height,
			TEXTURE_TYPE_2D,
			TEXTURE_FORMAT_RGBA8_SRGB, TEXTURE_USAGE_SAMPLED,
			world_atlas.pixels);

		arena_scratch_release(scratch);

		p_state->camera = (Camera){
			.projection = CAMERA_PROJECTION_ORTHOGRAPHIC,
			.position = { 1300, 600, 0.f },
			.target = { 0.0f, 0.0f, 0.0f },
			.up = { 0.0f, 1.0f, 0.0f },
			.fov = 45.f,
		};

		p_state->is_initialized = true;
	}

	if (t_state->is_initialized == false) {
		t_state->transient = (Arena){
			.memory = t_state + 1,
			.capacity = context->transient_memory_size - sizeof(TransientState)
		};

#if 0
		p_state->player_position = (Vector3f){
			0, 0, 0
		};
#endif

		t_state->is_initialized = true;
	}

	Vector2f input = {
		.x = input_key_down(KEY_CODE_D) - input_key_down(KEY_CODE_A),
		.y = input_key_down(KEY_CODE_S) - input_key_down(KEY_CODE_W),
	};
	input = vector2f_normalize(input);

	float32 speed = 100;
	if (input_key_down(KEY_CODE_LEFTSHIFT))
		speed = 1000;

	p_state->player_position.x += input.x * speed * dt;
	p_state->player_position.y += input.y * speed * dt;

	uint32_2 size = window_size_pixel(context->display);

	p_state->camera.position.x = MAX(p_state->player_position.x - size.x * .5f + 32, 0);
	p_state->camera.position.y = MAX(p_state->player_position.y - size.y * .5f + 32, 0);

	if (frame_begin(&p_state->renderer, p_state->camera)) {
		RhiUniformSet set1 = vulkan_uniformset_push(p_state->context, p_state->renderer.shader, 1);
		vulkan_uniformset_bind_buffer(p_state->context, set1, 0, p_state->terrain_ubo);
		vulkan_uniformset_bind_texture(p_state->context, set1, 1, p_state->terrain_atlas, p_state->renderer.nearest);
		vulkan_buffer_write(p_state->context, p_state->terrain_ubo, 0, sizeof(Vector3f), &(Vector4f){ 1.0f, 1.0f, 1.0f, 1.0f });
		vulkan_uniformset_bind(p_state->context, set1);

		Matrix4f mat = matrix4f_identity();
		vulkan_push_constants(p_state->context, 0, sizeof(Matrix4f), &mat);
		vulkan_buffer_bind(p_state->context, p_state->terrain_vbo, 0);
		vulkan_renderer_draw(p_state->context, p_state->terrain_width * p_state->terrain_height * 6);

		texture_draw(&p_state->renderer, p_state->sprite1, (Vector3f){ .x = (size.x + 64) * 0.5f, (size.y + 64) * 0.5f, 0.0f }, (Vector3f){ 64.f, 64.f, 64.f }, (Vector3f){ 0.0f, 1.0f, 1.0f });
		texture_draw(&p_state->renderer, p_state->sprite0, p_state->player_position, (Vector3f){ 64.f, 64.f, 64.f }, (Vector3f){ 1.0f, 1.0f, 1.0f });

		for (uint32_t index = 0; index < p_state->object_count; ++index) {
			uint32_t gid = p_state->object_gids[index];
			Vector3f position = p_state->object_positions[index];
			Vector2f size = p_state->object_dimensions[index];
			position.y -= size.y;

			Sprite temp = {
				.buffer = p_state->sprite0.buffer,
				.texture = *arena_trie_find(&p_state->object_lookup, gid, RhiTexture)
			};

			if (index == 535) {
				LOG_INFO("Drawing at %.2f, %2.f", position.x, position.y);
				LOG_INFO("Drawing with size %.2f, %.2f", size.x, size.y);
			}

			texture_draw(&p_state->renderer, temp, position, (Vector3f){ size.x, size.y, 1.0f }, (Vector3f){ 1.0f, 1.0f, 1.0f });
		}

		frame_end(&p_state->renderer);
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
	result.white = vulkan_texture_make(p_state->context, 1, 1, TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8, TEXTURE_USAGE_SAMPLED, WHITE);
	result.linear = vulkan_sampler_make(p_state->context, LINEAR_SAMPLER);
	result.nearest = vulkan_sampler_make(p_state->context, NEAREST_SAMPLER);

	ArenaTemp scratch = arena_scratch(NULL);

	result.shader = vulkan_shader_make(
		scratch.arena,
		p_state->context,
		importer_load_shader(scratch.arena, S("assets/shaders/unlit.vert.spv"), S("assets/shaders/unlit.frag.spv")),
		NULL);
	arena_scratch_release(scratch);

	result.global_buffer = vulkan_buffer_make(p_state->context, BUFFER_TYPE_UNIFORM, sizeof(Matrix4f) * 2, NULL);

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
	result.vbo = vulkan_buffer_make(p_state->context, BUFFER_TYPE_VERTEX, sizeof(vertices), vertices);

	return result;
}

bool frame_begin(Renderer2D *renderer, Camera camera) {
	uint32_2 size = window_size(p_state->display);
	if (vulkan_frame_begin(p_state->context, size.x, size.y)) {
		DrawListDesc desc = {
			.color_attachments[0] = {
			  .clear = { .color = { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f } },
			},
			.color_attachment_count = 1
		};
		if (vulkan_drawlist_begin(p_state->context, desc)) {
			Matrix4f projection = matrix4f_orthographic(0, size.x, 0, size.y, -1, 1);
			Matrix4f view = matrix4f_translated(vector3f_negate(camera.position));
			vulkan_buffer_write(p_state->context, renderer->global_buffer, 0, sizeof(Matrix4f), &view);
			vulkan_buffer_write(p_state->context, renderer->global_buffer, sizeof(Matrix4f), sizeof(Matrix4f), &projection);

			PipelineDesc pipeline = DEFAULT_PIPELINE;
			pipeline.cull_mode = CULL_MODE_NONE;
			vulkan_shader_bind(p_state->context, renderer->shader, pipeline);

			RhiUniformSet set0 = vulkan_uniformset_push(p_state->context, renderer->shader, 0);
			vulkan_uniformset_bind_buffer(p_state->context, set0, 0, renderer->global_buffer);
			vulkan_uniformset_bind(p_state->context, set0);
			return true;
		} else
			return false;
	} else
		return false;
}
void frame_end(Renderer2D *renderer) {
	vulkan_drawlist_end(p_state->context);
	vulkan_frame_end(p_state->context);
}

Sprite sprite_make(Renderer2D *renderer, String path) {
	Sprite result = { 0 };

	ArenaTemp scratch = arena_scratch(NULL);
	ImageSource image = importer_load_image(scratch.arena, path);
	Vector4f tint = { 1.0f, 1.0f, 1.0f, 1.0f };

	result.texture = vulkan_texture_make(p_state->context, image.width, image.height, TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8_SRGB, TEXTURE_USAGE_SAMPLED, image.pixels);
	result.buffer = vulkan_buffer_make(p_state->context, BUFFER_TYPE_UNIFORM, sizeof(Vector4f), &tint);

	arena_scratch_release(scratch);

	return result;
}

void texture_draw(Renderer2D *renderer, Sprite sprite, Vector3f position, Vector3f scale, Vector3f tint) {
	Matrix4f transform = matrix4f_translated(position);
	transform = matrix4f_scale(transform, scale);

	vulkan_push_constants(p_state->context, 0, sizeof(Matrix4f), &transform);

	RhiUniformSet set1 = vulkan_uniformset_push(p_state->context, renderer->shader, 1);
	vulkan_uniformset_bind_buffer(p_state->context, set1, 0, sprite.buffer);
	vulkan_uniformset_bind_texture(p_state->context, set1, 1, sprite.texture, renderer->nearest);
	vulkan_buffer_write(p_state->context, sprite.buffer, 0, sizeof(Vector3f), &tint);
	vulkan_uniformset_bind(p_state->context, set1);

	vulkan_buffer_bind(p_state->context, renderer->vbo, 0);
	vulkan_renderer_draw(p_state->context, 6);
}
