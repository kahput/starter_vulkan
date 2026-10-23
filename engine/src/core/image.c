#include "image.h"

#include "core/strings.h"
#include "os.h"
#include "core/logger.h"

#include <stb/stb_image.h>
Image imload(Arena *arena, string8 path) {
	ArenaTemp scratch = arena_scratch_begin(arena);
	Image result = { 0 };

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
		pixels = stbi_load_from_memory(file_content.bytes, file_content.length, (int32_t *)&result.width, (int32_t *)&result.height, (int32_t *)&result.channels, 0);

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
	if (ok == false) result = (Image){ 0 };

	arena_scratch_end(scratch);
	return result;
}

#include <stb/stb_image_write.h>
bool imsave(string8 path, const Image *image) {
	string8 ext = pathext8(path);

	bool ok = image != 0 && image->width > 0 && image->height > 0 && ext.length;
	if (ok) {
		if (eq8(s("png"), ext)) {
			stbi_write_png((char *)path.bytes, image->width, image->height, image->channels, image->pixels, image->channels * image->width);
		} else if (eq8(s("jpg"), ext) || eq8(s("jpeg"), ext)) {
			stbi_write_jpg((char *)path.bytes, image->width, image->height, image->channels, image->pixels, 100);
		}
	}

	return ok;
}

