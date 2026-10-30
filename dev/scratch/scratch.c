#include "core/arena.h"
#include "core/image.h"
#include "core/os.h"

#include "core/debug.h"
#include "core/logger.h"

#include "utils/input.h"

typedef bool (*TickFn)(Arena *arena, InputState *input);

int main(int32_t argc, const char *argv[]) {
	Arena arena[] = { arena_make(MiB(32)) };

	Image duck = imload(arena, s("resources/images/duck.jpeg"));
	Image gray = impush(arena, duck.width, duck.height, 1);

	float luma[] = { 0.2126f, 0.7152f, 0.0722f };
	for (uint32_t pixel_index = 0; pixel_index < impixels(duck); ++pixel_index) {
		float rgb[3] = { 0 };
		for (uint32_t channel = 0; channel < MIN(3, duck.channels); ++channel)
			rgb[channel] = (float)duck.pixels[pixel_index * duck.channels + channel] * luma[channel];

		gray.pixels[pixel_index] = (uint8_t)roundf(rgb[0] + rgb[1] + rgb[2]);
	}
	imsave(s("resources/images/duck_grayscale.png"), gray);

	rgba8 top = hex(0xF7EF81);
	rgba8 bot = hex(0xDD1C1A);

	Image gradient = impush(arena, 512, 128, 3);
	for (int32_t y = 0; y < (int32_t)gray.height; ++y) {
		for (int32_t x = 0; x < (int32_t)gray.width; ++x) {
			uint32_t pixel_index = x + y * gray.width;
			float t = (float)y / (gradient.height - 1);

			rgba8 color = color_lerp(top, bot, t);
			uint8_t output[] = { color.r, color.g, color.b, color.a };
			for (uint32_t channel = 0; channel < gradient.channels; ++channel)
				gradient.pixels[pixel_index * gradient.channels + channel] = output[channel];
		}
	}
	imsave(s("resources/images/gradient.png"), gradient);

	// clang-format off
    const uint32_t kw = 3, kh = 3;
	int sobelx[] = {
		-1, 0, 1,
		-2, 0, 2,
		-1, 0, 1,
	};
    ASSERT(countof(sobelx) == kw * kh);
	int sobely[] = {
		-1, -2, -1,
		 0,  0,  0,
		 1,  2,  1,
	};
    ASSERT(countof(sobely) == kw * kh);
	// clang-format on

	Image edges = impush(arena, gray.width, gray.height, 1);
	for (int32_t y = 0; y < (int32_t)gray.height; ++y) {
		for (int32_t x = 0; x < (int32_t)gray.width; ++x) {
			int32_t gx = 0, gy = 0;
			for (uint32_t j = 0; j < kh; ++j) {
				for (uint32_t i = 0; i < kw; ++i) {
					int32_t px = clamp1i((int32_t)x + i - 1, 0, gray.width - 1);
					int32_t py = clamp1i((int32_t)y + j - 1, 0, gray.height - 1);

					int32_t index = px + py * gray.width;
					gx += sobelx[i + j * kw] * gray.pixels[index];
					gy += sobely[i + j * kw] * gray.pixels[index];
				}
			}

			float magnitude = sqrtf(gx * gx + gy * gy) / 4.0f;
			edges.pixels[x + y * gray.width] = CLAMP(roundf(magnitude), 0, 255);
		}
	}
	imsave(s("resources/images/duck_sobel.png"), edges);

	return 0;
}
