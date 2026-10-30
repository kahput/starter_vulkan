#pragma once

#include "common.h"
#include "core/strings.h"

typedef struct {
	uint8_t *pixels;
	uint32_t width, height, channels;
} Image;

INLINE Rectangle imrect(Image image) { return (Rectangle){ 0, 0, image.width, image.height }; }
INLINE uint64_t imbytes(Image image) { return (uint64_t)image.width * image.height * image.channels; }
INLINE uint64_t impixels(Image image) { return (uint64_t)image.width * image.height; }
INLINE Image impush(Arena *arena, uint32_t width, uint32_t height, uint32_t channels) { return (Image){ arena_push_count(arena, uint8_t, (uint64_t)width * height * channels), width, height, channels }; }
INLINE Image imcopy(Arena *arena, Image image) { return (Image){ arena_push_copy(arena, image.pixels, imbytes(image)), image.width, image.height, image.channels }; }

Image imload(Arena *arena, string8 path);
bool imsave(string8 path, Image image);

Image imfliter(Arena *arena, Image target, int32_t kernel[], uint32_t kw, uint32_t kh);
/* bool imtext(Font font, Image im, string8 text); */
