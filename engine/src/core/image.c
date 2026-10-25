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

Image imfliter(Arena *arena, Image target, int32_t kernel[], uint32_t kw, uint32_t kh) {
	Image result = impush(arena, target.width, target.height, target.channels);

	for (int32_t y = 0; y < (int32_t)target.height; ++y) {
		for (int32_t x = 0; x < (int32_t)target.width; ++x) {
			int32_t sum = 0;

			for (uint32_t j = 0; j < kh; ++j) {
				for (uint32_t i = 0; i < kw; ++i) {
					int32_t px = clamp1s((int32_t)x + i - 1, 0, target.width - 1);
					int32_t py = clamp1s((int32_t)y + j - 1, 0, target.height - 1);

					int32_t index = px + py * target.width;

					sum += kernel[i + j * kw] * target.pixels[index];
				}
			}

			result.pixels[x + y * target.width] = (uint8_t)sum;
		}
	}

    return result;
}
