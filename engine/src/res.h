#pragma once

#include "common.h"
#include "core/mesh.h"
#include "gfx.h"
#include "core/strings.h"
#include "gfx/gfx_types.h"

// clang-format off
typedef uint32_t RES_AssetID; // catalog specific index
typedef struct { uint64_t value; } RES_UUID; // unique
// clang-format on

typedef struct {
	RES_UUID uuid;

	string8 name, description;
	string8 filepath;
} RES_ImageMeta;

typedef enum {
	RES_FONT_STYLE_NORMAL,
	RES_FONT_STYLE_ITALIC,

	RES_FONT_STYLE_MAX,
} RES_FontStyle;
extern string8 res_font_style_to_display_string[RES_FONT_STYLE_MAX];

typedef uint16_t RES_FontWeight;
enum {
	RES_FONT_WEIGHT_THIN = 100,
	RES_FONT_WEIGHT_EXTRA_LIGHT = 200,
	RES_FONT_WEIGHT_LIGHT = 300,
	RES_FONT_WEIGHT_REGULAR = 400,
	RES_FONT_WEIGHT_MEDIUM = 500,
	RES_FONT_WEIGHT_SEMI_BOLD = 600,
	RES_FONT_WEIGHT_BOLD = 700,
	RES_FONT_WEIGHT_EXTRA_BOLD = 800,
	RES_FONT_WEIGHT_BLACK = 900,
	RES_FONT_WEIGHT_EXTRA_BLACK = 950,
};

typedef struct {
	string8 filepath;
	RES_FontStyle style;

	RES_FontWeight min_weight;
	RES_FontWeight max_weight;
} RES_FontFaceMeta;

typedef struct {
	RES_UUID uuid;

	string8 name;
	RES_FontFaceMeta *faces;
	uint32_t face_count;
} RES_FontFamilyMeta;

typedef struct {
	RES_UUID uuid;

	string8 name;
	string8 filepaths[SHADER_STAGE_MAX];
	PipelineOptions pipelines[8];
	uint32_t pipeline_count;
} RES_ShaderMeta;

typedef struct {
	uint8_t *pixels;
	uint32_t width, height, channels;
} RES_Image2D;
INLINE Rectangle image_rect(RES_Image2D image) { return (Rectangle){ 0, 0, image.width, image.height }; }
INLINE float2 image_size(RES_Image2D image) { return (float2){ image.width, image.height }; }

typedef struct {
	GFX_Image *handle;
	uint32_t width, height;
} RES_Texture2D;
INLINE Rectangle texture_rect(RES_Texture2D texture) { return (Rectangle){ 0, 0, texture.width, texture.height }; }
INLINE float2 texture_size(RES_Texture2D texture) { return (float2){ texture.width, texture.height }; }

typedef struct {
	uint32_t codepoint;

	float advance;
	float2 bearing;

	Rectangle uv;
} RES_Glyph;

typedef struct {
	RES_Image2D img_atlas;
	RES_Texture2D tex_atlas;

	RES_Glyph *glyphs;
	uint32_t first_codepoint, glyph_count;
	float ascent, descent, line_gap;

	RES_FontWeight weight;
} RES_Font;

typedef struct {
	GFX_Shader *handle;
} RES_Shader;

typedef struct {
	RES_ImageMeta *images;
	uint32_t image_count;

	RES_ShaderMeta *shaders;
	uint32_t shader_count;

	RES_FontFamilyMeta *fonts;
	uint32_t font_count;
} RES_Catalog;

typedef enum {
	RES_LIMIT_IMAGE_COUNT = 1024,
	RES_LIMIT_TEXTURE_COUNT = 2048,
	RES_LIMIT_SHADER_COUNT = 32,
	RES_LIMIT_FONT_COUNT = 1024,
} RES_Limits;

typedef enum {
	RES_STATE_UNLOADED,
	RES_STATE_LOADING,
	RES_STATE_LOADED,

	RES_STATE_FAILED,
} RES_State;

typedef enum {
	RES_ASSET_TYPE_IMAGE,
	RES_ASSET_TYPE_SHADER,
	RES_ASSET_TYPE_FONT,

	RES_ASSET_TYPE_MAX,
} RES_AssetType;

typedef enum {
	RES_INSTANCE_TYPE_IMAGE,
	RES_INSTANCE_TYPE_TEXTURE,
	RES_INSTANCE_TYPE_SHADER,
	RES_INSTANCE_TYPE_FONT,

	RES_INSTANCE_TYPE_MAX,
} RES_InstanceType;

typedef struct {
	RES_InstanceType output_type;
	uint32_t subresource;
	uint64_t variant;
} RES_Key;

typedef struct RES_InstanceRef RES_InstanceSlot;
struct RES_InstanceRef {
	RES_InstanceSlot *next;

	RES_Key key;
	RES_State state;
	uint32_t index;
};

typedef struct {
	OS_Timestamp mtime;
	RES_InstanceSlot *first_instance;
} RES_Record;

typedef struct {
	void *data;
	uint32_t stride;
	uint32_t count, capacity;
} RES_InstancePool;

typedef struct {
	Arena *arena;
	GFX_Device *device;

	RES_Catalog catalog;
	RES_Record *records[RES_ASSET_TYPE_MAX];
	uint32_t record_counts[RES_ASSET_TYPE_MAX];
	RES_InstancePool instances[RES_INSTANCE_TYPE_MAX];

	struct {
		RES_Image2D image;
		RES_Texture2D texture;
		RES_Font font;
		RES_Shader shader;
	} fallback;
} RES_Cache;
ENGINE_API RES_Cache res_cache_make(Arena *arena, GFX_Device *device, const RES_Catalog *catalog);

ENGINE_API RES_Image2D *res_image(RES_Cache *cache, RES_AssetID id);
ENGINE_API RES_Texture2D *res_texture(RES_Cache *cache, RES_AssetID id, PixelFormat fmt);
ENGINE_API RES_Font *res_font_ex(RES_Cache *cache, RES_AssetID family, RES_FontWeight weight, RES_FontStyle style, uint32_t font_size);
INLINE RES_Font *res_font(RES_Cache *cache, RES_AssetID family, uint32_t font_size) { return res_font_ex(cache, family, RES_FONT_WEIGHT_REGULAR, RES_FONT_STYLE_NORMAL, font_size); }
ENGINE_API RES_Shader *res_shader(RES_Cache *cache, RES_AssetID id);

ENGINE_API RES_ImageMeta *res_image_meta(RES_Cache *cache, RES_AssetID id);
ENGINE_API RES_ShaderMeta *res_shader_meta(RES_Cache *cache, RES_AssetID id);
ENGINE_API RES_FontFamilyMeta *res_font_meta(RES_Cache *cache, RES_AssetID id);

ENGINE_API void res_cache_tick(Arena *frame_arena, RES_Cache *cache);
ENGINE_API float2 measure_text(RES_Font *font, string8 text);

ENGINE_API Mesh res_load_gltf(Arena *arena, string8 path);
