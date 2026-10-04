#include "res.h"
#include "core/logger.h"

#include <stb/stb_image.h>
RES_Image2D res__load_image(Arena *arena, string8 path) {
	ArenaTemp scratch = arena_scratch_begin(arena);
	RES_Image2D result = { 0 };

	bool ok = arena && path.length;

	string8 file_content = { 0 };
	uint8_t *pixels = 0;

	if (ok) {
		file_content = os_file_read(scratch.arena, path);

		ok = file_content.length != 0;
		if (ok == false)
			LOG_WARN("[%.*s] failed to load", arg8(path));
	}

	if (ok) {
		result.channels = 4;
		pixels = stbi_load_from_memory(file_content.bytes, file_content.length, (int32_t *)&result.width, (int32_t *)&result.height, 0, 4);

		ok = pixels != 0;
		if (ok == false)
			LOG_WARN("[%.*s] failed to decode image payload", arg8(path));
	}

	if (ok) {
		result.pixels = arena_push_copy(arena, pixels, result.width * result.height * result.channels);
		stbi_image_free(pixels);

		string8 filename = pathfile8(path);
		LOG_INFO("'%.*s' loaded sucessfully (%ux%u, %s)", arg8(filename), result.width, result.height, result.channels == 4 ? "RGBA8" : "RGB8");
	}
	if (ok == false) result = (RES_Image2D){ 0 };

	arena_scratch_end(scratch);
	return result;
}

static bool default_loader_image(RES_Cache *cache, RES_AssetID id, RES_Image2D *out) {
	*out = res__load_image(cache->arena, cache->catalog.images[id].filepath);
	return out->pixels != 0;
}
