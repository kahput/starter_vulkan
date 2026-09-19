#include "res.h"
#include "core/arena.h"
#include "core/debug.h"
#include "gfx/gfx_types.h"

#include <stb/stb_image.h>

/* RES_Cache res_init(Arena *arena, GFX_Device *device) { */
/* ASSERT(arena && device); */
/* RES_Cache result = { .arena = arena, .device = device }; */

/* uint8_t *magenta = arena_push_count(arena, uint8_t, 4); */
/* memory_copy(magenta, ((uint8_t[]){ 255, 0, 255, 255 }), 4); */
/* result.image_fallback = (RES_Image2D){ */
/* .width = 1, */
/* .height = 1, */
/* .channels = 4, */
/* .pixels = magenta, */
/* .loaded = true */
/* }; */

/* result.texture_fallback = (RES_Texture2D){ */
/* .handle = gfx_image_make(device, 1, 1, */
/* (ImageOptions){ */
/* .debug_name = (char *)str8_pushf(arena, s("default:white")).text, */
/* .format = PIXEL_FORMAT_RGBA8_SRGB, */
/* .pixels = &(uint32_t){ 0xFFFFFFFF }, */
/* }), */
/* .loaded = true */
/* }; */

/* return result; */
/* } */

/* RES_Image2D *res_image(RES_Cache *cache, RES_ID id) { */
/* ArenaTemp scratch = arena_scratch_begin(cache->arena); */

/* RES_Image2D *result = &cache->image_cache[id]; */
/* RES_ImageMetadata *meta = &res_image_metadata[id]; */

/* String8 file_content = { 0 }; */
/* uint8_t *pixels = 0; */

/* bool ok = result->loaded == false; */
/* if (ok) { */
/* result->loaded = true; */

/* file_content = os_file_read_entire(scratch.arena, meta->filepath); */

/* ok = file_content.length != 0; */
/* if (ok == false) */
/* LOG_WARN("[%s] failed to load", meta->filepath.text); */
/* } */

/* if (ok) { */
/* result->channels = 4; */
/* pixels = stbi_load_from_memory(file_content.text, file_content.length, (int32_t *)&result->width, (int32_t *)&result->height, 0, 4); */

/* ok = pixels != 0; */
/* if (ok == false) */
/* LOG_WARN("[%s] failed to decode image payload", meta->filepath.text); */
/* } */

/* if (ok) { */
/* result->pixels = arena_push_copy(cache->arena, pixels, result->width * result->height * result->channels, 16); */
/* stbi_image_free(pixels); */
/* } */

/* arena_scratch_end(scratch); */
/* return result->pixels == 0 ? &cache->image_fallback : result; */
/* } */

/* RES_Texture2D *res_texture(RES_Cache *cache, RES_ID id) { */
/* RES_Texture2D *tex = &cache->texture_cache[id]; */
/* if (tex->loaded == false) { */
/* RES_Image2D *img = res_image(cache, id); */
/* RES_ImageMetadata *meta = &res_image_metadata[id]; */

/* tex->handle = gfx_image_make(cache->device, img->width, img->height, */
/* (ImageOptions){ */
/* .debug_name = (char *)str8_pushf(cache->arena, meta->name).text, */
/* .format = PIXEL_FORMAT_RGBA8_UNORM, */
/* .pixels = img->pixels, */
/* }); */
/* tex->loaded = true; */
/* } */
/* return tex; */
/* } */
