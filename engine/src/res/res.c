#include "res.h"
#include "core/arena.h"
#include "core/debug.h"
#include "gfx/gfx_types.h"

#include <stb/stb_image.h>

static OS_Timestamp res__combined_mtime(string8 *filepaths, uint32_t count) {
	OS_Timestamp latest = { 0 };
	for (uint32_t i = 0; i < count; ++i) {
		if (filepaths[i].length == 0) continue;
		OS_Timestamp ts = os_file_mtime(filepaths[i]);
		if (ts > latest) latest = ts;
	}
	return latest;
}

RES_Cache res_cache_make(Arena *arena, GFX_Device *device, const RES_Registry *registry) {
	ASSERT(arena && device);
	RES_Cache result = { .arena = arena, .device = device };

	result.registry.image_count = registry->image_count;
	if (result.registry.image_count) {
		result.registry.images = arena_push_count(arena, RES_ImageMeta, registry->image_count);
		memory_copy_count(result.registry.images, registry->images, registry->image_count);

		result.image_cache = arena_push_count(arena, RES_Image2D, registry->image_count);
		result.texture_cache = arena_push_count(arena, RES_Texture2D, registry->image_count);
		result.image_mtimes = arena_push_count(arena, OS_Timestamp, registry->image_count);
	}

	result.registry.shader_count = registry->shader_count;
	if (result.registry.shader_count) {
		result.registry.shaders = arena_push_count(arena, RES_ShaderMeta, registry->shader_count);
		memory_copy_count(result.registry.shaders, registry->shaders, registry->shader_count);

		result.shader_cache = arena_push_count(arena, RES_Shader, registry->shader_count);
		result.shader_mtimes = arena_push_count(arena, OS_Timestamp, registry->shader_count);
	}

	uint8_t *magenta = arena_push_count(arena, uint8_t, 4);
	memory_copy(magenta, ((uint8_t[]){ 255, 0, 255, 255 }), 4);
	result.image_fallback = (RES_Image2D){
		.state = RES_STATE_LOADED,
		.width = 1,
		.height = 1,
		.channels = 4,
		.pixels = magenta,
	};

	result.texture_fallback = (RES_Texture2D){
		.state = RES_STATE_LOADED,
		.handle = gfx_image_make(device, 1, 1,
			(ImageOptions){
			  .debug_name = "default:white",
			  .format = PIXEL_FORMAT_RGBA8_SRGB,
			  .pixels = &(uint32_t){ 0xFFFFFFFF },
			}),
	};

	return result;
}

RES_Image2D *res_image(RES_Cache *cache, RES_ID id) {
	ArenaTemp scratch = arena_scratch_begin(cache->arena);
	RES_Image2D *result = &cache->image_cache[id];

	RES_ImageMeta *meta = &cache->registry.images[id];

	string8 file_content = { 0 };
	uint8_t *pixels = 0;

	bool ok = cache && result->state == RES_STATE_UNLOADED;
	if (ok) {
		file_content = os_file_read(scratch.arena, meta->filepath);

		ok = file_content.length != 0;
		if (ok == false)
			LOG_WARN("[%.*s] failed to load", arg8(meta->filepath));
	}

	if (ok) {
		result->channels = 4;
		pixels = stbi_load_from_memory(file_content.bytes, file_content.length, (int32_t *)&result->width, (int32_t *)&result->height, 0, 4);

		ok = pixels != 0;
		if (ok == false)
			LOG_WARN("[%.*s] failed to decode image payload", arg8(meta->filepath));
	}

	if (ok) {
		result->pixels = arena_push_copy(cache->arena, pixels, result->width * result->height * result->channels, 16);
		stbi_image_free(pixels);

		result->state = RES_STATE_LOADED;
	}

	if (result->state == RES_STATE_UNLOADED && ok == false) result->state = RES_STATE_FAILED;

	arena_scratch_end(scratch);
	return result->state == RES_STATE_LOADED ? result : &cache->image_fallback;
}

RES_Texture2D *res_texture(RES_Cache *cache, RES_ID id) {
	RES_Texture2D *tex = &cache->texture_cache[id];
	if (tex->state == false) {
		RES_Image2D *img = res_image(cache, id);
		RES_ImageMeta *meta = &cache->registry.images[id];

		tex->handle = gfx_image_make(cache->device, img->width, img->height,
			(ImageOptions){
			  .debug_name = (char *)copy8(cache->arena, meta->name).bytes,
			  .format = PIXEL_FORMAT_RGBA8_UNORM,
			  .pixels = img->pixels,
			});
		tex->width = img->width, tex->height = img->height;
		tex->state = RES_STATE_LOADED;
	}
	return tex;
}

ENGINE_API RES_Shader *res_shader(RES_Cache *cache, RES_ID id) {
	RES_Shader *result = 0;
	RES_ShaderMeta *metadata = 0;

	bool ok = cache && cache->registry.shaders && id < cache->registry.shader_count;
	if (ok) {
		metadata = &cache->registry.shaders[id];
		result = &cache->shader_cache[id];

		ok = (metadata->filepaths[SHADER_STAGE_VERTEX].length && metadata->filepaths[SHADER_STAGE_FRAGMENT].length) ||
			metadata->filepaths[SHADER_STAGE_COMPUTE].length;
	}

	if (ok && cache->shader_cache[id].state == RES_STATE_UNLOADED) {
		ArenaTemp scratch = arena_scratch_begin(NULL);

		bool is_compute = metadata->filepaths[SHADER_STAGE_COMPUTE].length > 0;
		if (is_compute) {
			cache->shader_mtimes[id] = os_file_mtime(metadata->filepaths[SHADER_STAGE_COMPUTE]);
			string8 bytecode = os_file_read(scratch.arena, metadata->filepaths[SHADER_STAGE_COMPUTE]);

			cache->shader_cache[id] = (RES_Shader){
				.state = RES_STATE_LOADED,
				.handle = gfx_compute_make(cache->device, bytecode, (char *)metadata->name.bytes),
			};
		} else {
			string8 vs_bytecode = os_file_read(scratch.arena, metadata->filepaths[SHADER_STAGE_VERTEX]);
			string8 fs_bytecode = os_file_read(scratch.arena, metadata->filepaths[SHADER_STAGE_FRAGMENT]);

			cache->shader_mtimes[id] = res__combined_mtime(metadata->filepaths, countof(metadata->filepaths));
			cache->shader_cache[id] = (RES_Shader){
				.state = RES_STATE_LOADED,
				.handle = gfx_shader_make(cache->device, vs_bytecode, fs_bytecode, (char *)metadata->name.bytes),
			};

			for (uint32_t index = 0; index < metadata->pipeline_count; ++index)
				gfx_pipeline_register(cache->device, cache->shader_cache[id].handle, metadata->pipelines[index]);
		}

		arena_scratch_end(scratch);
	}

	if (result == 0 || result->state != RES_STATE_LOADED) result = &cache->shader_fallback;
	return result;
}

#ifdef DEV_BUILD
void res_cache_tick(Arena *frame_arena, RES_Cache *cache) {
	for (RES_ID id = 0; id < cache->registry.shader_count; ++id) {
		RES_ShaderMeta *meta = &cache->registry.shaders[id];
		if (cache->shader_cache[id].state != RES_STATE_LOADED) continue;

		OS_Timestamp now = res__combined_mtime(meta->filepaths, SHADER_STAGE_MAX);
		if (now == cache->shader_mtimes[id]) continue;

		LOG_INFO("hot-reloading %.*s...", arg8(meta->name));
		gfx_device_wait_idle(cache->device);
		gfx_shader_destroy(cache->device, cache->shader_cache[id].handle);

		bool is_compute = meta->filepaths[SHADER_STAGE_COMPUTE].length > 0;
		if (is_compute) {
			string8 bytecode = os_file_read(frame_arena, meta->filepaths[SHADER_STAGE_COMPUTE]);
			cache->shader_cache[id].handle = gfx_compute_make(cache->device, bytecode, (char *)meta->name.bytes);
		} else {
			string8 vs = os_file_read(frame_arena, meta->filepaths[SHADER_STAGE_VERTEX]);
			string8 fs = os_file_read(frame_arena, meta->filepaths[SHADER_STAGE_FRAGMENT]);
			cache->shader_cache[id].handle = gfx_shader_make(cache->device, vs, fs, (char *)meta->name.bytes);
		}

		for (uint32_t p = 0; p < meta->pipeline_count; ++p) {
			gfx_pipeline_ensure(cache->device, cache->shader_cache[id].handle, meta->pipelines[p],
				(GFX_DrawTargetLayout){ .color_formats[0] = PIXEL_FORMAT_BGRA8_UNORM, .color_count = 1 });
		}

		cache->shader_mtimes[id] = now;
	}
}
#else
void res_cache_tick(Arena *frame_arena, RES_Cache *cache) { (void)frame_arena, (void)cache; }
#endif
