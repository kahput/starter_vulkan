#include "core/arena.h"
#include "core/image.h"
#include "os.h"

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
	imsave(s("resources/images/duck_grayscale.jpg"), &gray);

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
	imsave(s("resources/images/gradient.png"), &gradient);

	int sobelx[3][3] = {
		{ -1, 0, 1 },
		{ -2, 0, 2 },
		{ -1, 0, 1 },
	};
	int sobely[3][3] = {
		{ -1, -2, -1 },
		{ 0, 0, 0 },
		{ 1, 2, 1 },
	};

	Image edges = impush(arena, gray.width, gray.height, 1);
	for (int32_t y = 0; y < (int32_t)gray.height; ++y) {
		for (int32_t x = 0; x < (int32_t)gray.width; ++x) {
			int32_t gx = 0, gy = 0;
			for (int32_t j = 0; j < 3; ++j) {
				for (int32_t i = 0; i < 3; ++i) {
					int32_t px = CLAMP(x + i - 1, 0, (int32_t)gray.width - 1);
					int32_t py = CLAMP(y + j - 1, 0, (int32_t)gray.height - 1);

					int32_t index = px + py * gray.width;
					gx += sobelx[j][i] * gray.pixels[index];
					gy += sobely[j][i] * gray.pixels[index];
				}
			}

			float magnitude = sqrtf(gx * gx + gy * gy) / 4.0f;
			edges.pixels[x + y * gray.width] = CLAMP(roundf(magnitude), 0, 255);
		}
	}
	imsave(s("resources/images/duck_sobel.png"), &edges);

	float4x4 translation = affine4x4(identity3x3(), float3(1.0f, 0.0f, 0.0f));
	float4x4 rotation = affine4x4(axisangle3x3(FLOAT3_UP, radians(45.0f)), float3(0.0f));

	float4x4 result = mul4x4(translation, rotation);

	return 0;
}
