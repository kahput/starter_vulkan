#pragma once

#include "common.h"
#include "gfx.h"
#include "core/strings.h"

// clang-format off
typedef uint32_t RES_ID; // registry specific index
typedef struct { uint64_t value; } RES_UUID; // unique
// clang-format on

typedef struct {
	RES_UUID uuid;

	string8 name;
	string8 filepath;
} RES_ImageMeta;

typedef struct {
	RES_UUID uuid;

	string8 name;
	string8 filepaths[SHADER_STAGE_MAX];
	PipelineOptions pipelines[8];
	uint32_t pipeline_count;
} RES_ShaderMeta;

typedef enum {
	RES_STATE_UNLOADED,
	RES_STATE_LOADING,
	RES_STATE_LOADED,

	RES_STATE_FAILED,
} RES_State;

typedef struct {
	RES_State state;

	uint8_t *pixels;
	int32_t width, height, channels;
} RES_Image2D;
INLINE Rectangle image_rect(RES_Image2D image) { return (Rectangle){ 0, 0, image.width, image.height }; }
INLINE float2 image_size(RES_Image2D image) { return (float2){ image.width, image.height }; }

typedef struct {
	RES_State state;

	GFX_Image *handle;
	uint32_t width, height;
} RES_Texture2D;
INLINE Rectangle texture_rect(RES_Texture2D texture) { return (Rectangle){ 0, 0, texture.width, texture.height }; }
INLINE float2 texture_size(RES_Texture2D texture) { return (float2){ texture.width, texture.height }; }

typedef struct {
	RES_State state;

	GFX_Shader *handle;
} RES_Shader;

typedef struct {
	RES_ImageMeta *images;
	uint32_t image_count;

	RES_ShaderMeta *shaders;
	uint32_t shader_count;
} RES_Registry;

typedef struct {
	Arena *arena;
	GFX_Device *device;

	RES_Registry registry;

	RES_Image2D *image_cache;
	RES_Texture2D *texture_cache;
	RES_Shader *shader_cache;

	RES_Image2D image_fallback;
	RES_Texture2D texture_fallback;
	RES_Shader shader_fallback;

#ifdef DEV_BUILD
	OS_Timestamp *image_mtimes;
	OS_Timestamp *shader_mtimes;
#endif
} RES_Cache;
ENGINE_API RES_Cache res_cache_make(Arena *arena, GFX_Device *device, const RES_Registry *registry);

INLINE bool res_image_valid(RES_Cache *cache, RES_ID id) { return cache && cache->registry.images && id < cache->registry.image_count && cache->image_cache[id].state == RES_STATE_LOADED; }

ENGINE_API RES_Image2D *res_image(RES_Cache *cache, RES_ID id);
ENGINE_API RES_Texture2D *res_texture(RES_Cache *cache, RES_ID id);
ENGINE_API RES_Shader *res_shader(RES_Cache *cache, RES_ID id);

ENGINE_API RES_UUID res_image_uuid(RES_Cache *cache, RES_ID id);
ENGINE_API RES_UUID res_shader_uuid(RES_Cache *cache, RES_ID id);

ENGINE_API void res_cache_tick(Arena *frame_arena, RES_Cache *cache);
