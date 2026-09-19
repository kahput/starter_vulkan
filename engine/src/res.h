#pragma once

#include "gfx.h"
#include "core/strings.h"

typedef struct {
	uint8_t *pixels;
	int32_t width, height, channels;
	bool loaded;
} RES_Image2D;

typedef uint32_t RES_ID;

INLINE Rectangle image_rect(RES_Image2D image) { return (Rectangle){ 0, 0, image.width, image.height }; }
INLINE float2 image_size(RES_Image2D image) { return (float2){ image.width, image.height }; }

typedef struct {
	GFX_Image *handle;
	bool loaded;
} RES_Texture2D;

#define RES_CACHE_MAX_IMAGES 4096
typedef struct {
	Arena *arena;
	GFX_Device *device;

	RES_Image2D image_cache[RES_CACHE_MAX_IMAGES];
	RES_Image2D image_fallback;

	RES_Texture2D texture_cache[RES_CACHE_MAX_IMAGES];
	RES_Texture2D texture_fallback;
} RES_Cache;
RES_Cache res_init(Arena *arena, GFX_Device *device);

RES_Image2D *res_image(RES_Cache *cache, RES_ID id);
RES_Texture2D *res_texture(RES_Cache *cache, RES_ID id);
