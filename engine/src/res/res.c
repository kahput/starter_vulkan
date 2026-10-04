#include "res.h"
#include "gfx.h"
#include "gfx/gfx_types.h"
#include "image_loader.c"

#include "core/arena.h"
#include "core/debug.h"

uint64_t res_instance_limits[RES_INSTANCE_TYPE_MAX] = {
	[RES_INSTANCE_TYPE_IMAGE] = RES_LIMIT_IMAGE_COUNT,
	[RES_INSTANCE_TYPE_TEXTURE] = RES_LIMIT_TEXTURE_COUNT,
	[RES_INSTANCE_TYPE_SHADER] = RES_LIMIT_SHADER_COUNT,
	[RES_INSTANCE_TYPE_FONT] = RES_LIMIT_FONT_COUNT,
};

uint64_t res_instance_strides[RES_INSTANCE_TYPE_MAX] = {
	[RES_INSTANCE_TYPE_IMAGE] = sizeof(RES_Image2D),
	[RES_INSTANCE_TYPE_TEXTURE] = sizeof(RES_Texture2D),
	[RES_INSTANCE_TYPE_SHADER] = sizeof(RES_Shader),
	[RES_INSTANCE_TYPE_FONT] = sizeof(RES_Font),
};

string8 res_font_style_to_display_string[RES_FONT_STYLE_MAX] = {
	[RES_FONT_STYLE_NORMAL] = comp8("normal"),
	[RES_FONT_STYLE_ITALIC] = comp8("italic"),
};

static RES_Font res__load_font(Arena *arena, string8 path, RES_FontWeight weight, uint32_t font_size);
static uint32_t res__find_closest_font_face(RES_FontFamilyMeta *meta, RES_FontWeight weight, RES_FontStyle style);
static RES_Catalog res__catalog_deep_copy(Arena *arena, const RES_Catalog *source);

static RES_Record *res__record(RES_Cache *cache, RES_AssetType asset_type, RES_AssetID id);
static RES_InstanceSlot *res__find_slot(RES_Record *record, RES_Key key);
static RES_InstanceSlot *res__ensure_slot(RES_Cache *cache, RES_AssetType type, RES_AssetID id, RES_Key key);
static void *res__instance_at(RES_Cache *cache, RES_InstanceType type, uint32_t index);
static void *res__acquire(RES_Cache *cache, RES_AssetType asset_type, RES_AssetID id, RES_Key key);

static OS_Timestamp res__combined_mtime(string8 *filepaths, uint32_t count);
static OS_Timestamp res__source_mtime(RES_Cache *cache, RES_AssetType asset_type, RES_AssetID id);

static bool res__loader_texture(RES_Cache *cache, RES_AssetID id, RES_Key key, RES_Texture2D *out) {
	RES_Image2D *image = res__acquire(cache, RES_ASSET_TYPE_IMAGE, id, (RES_Key){ .output_type = RES_INSTANCE_TYPE_IMAGE });
	if (image == 0) return false;

	RES_ImageMeta *meta = &cache->catalog.images[id];
	out->handle = gfx_image_make(cache->device, image->width, image->height,
		(ImageOptions){
		  .debug_name = (char *)fmt8(cache->arena, "%.*s[format=%.*s]", arg8(meta->name), arg8(pixel_format_to_display_string[(PixelFormat)key.variant])).bytes,
		  .format = (PixelFormat)key.variant,
		  .pixels = image->pixels,
		});
	out->width = image->width, out->height = image->height;
	return out->handle != 0;
}

static GFX_Shader *res__shader_make(Arena *arena, RES_Cache *cache, RES_ShaderMeta *meta) {
	string8 *path = meta->filepaths;
	char *name = (char *)meta->name.bytes;

	if (path[SHADER_STAGE_COMPUTE].length) {
		string8 bytecode = os_file_read(arena, path[SHADER_STAGE_COMPUTE]);
		return bytecode.length ? gfx_compute_make(cache->device, bytecode, name) : 0;
	}

	if (path[SHADER_STAGE_VERTEX].length && path[SHADER_STAGE_FRAGMENT].length) {
		string8 vs = os_file_read(arena, path[SHADER_STAGE_VERTEX]);
		string8 fs = os_file_read(arena, path[SHADER_STAGE_FRAGMENT]);
		return vs.length && fs.length ? gfx_shader_make(cache->device, vs, fs, name) : 0;
	}

	return 0;
}

static bool res__loader_shader(RES_Cache *cache, RES_AssetID id, RES_Key key, RES_Shader *out) {
	RES_ShaderMeta *meta = &cache->catalog.shaders[id];

	ArenaTemp scratch = arena_scratch_begin(cache->arena);
	out->handle = res__shader_make(scratch.arena, cache, meta);
	arena_scratch_end(scratch);

	bool is_compute = meta->filepaths[SHADER_STAGE_COMPUTE].length > 0;
	if (out->handle && is_compute == false) {
		for (uint32_t index = 0; index < meta->pipeline_count; ++index)
			gfx_pipeline_register(cache->device, out->handle, meta->pipelines[index]);
	}
	return out->handle != 0;
}

static bool res__loader_font(RES_Cache *cache, RES_AssetID id, RES_Key key, RES_Font *out) {
	RES_FontFamilyMeta *meta = &cache->catalog.fonts[id];

	uint16_t style = key.variant >> 48;
	uint16_t weight = (key.variant >> 32) & 0xFFFF;
	uint32_t font_size = (uint32_t)key.variant;

	uint32_t face_index = res__find_closest_font_face(meta, weight, style);

	ArenaTemp scratch = arena_scratch_begin(cache->arena);
	RES_Font font = res__load_font(scratch.arena, meta->faces[face_index].filepath, weight, font_size);

	if (font.img_atlas.pixels) {
        font.glyphs = arena_push_copy(cache->arena, font.glyphs, font.glyph_count);
		font.tex_atlas = (RES_Texture2D){
			.handle = gfx_image_make(cache->device, font.img_atlas.width, font.img_atlas.height,
				(ImageOptions){
				  .debug_name = (char *)fmt8(cache->arena, "%.*s[weight=%u, style=%.*s, size=%u]", arg8(meta->name), weight, arg8(res_font_style_to_display_string[style]), font_size).bytes,
				  .format = PIXEL_FORMAT_R8_UNORM,
				  .pixels = font.img_atlas.pixels,
				  .swizzle = {
					[0] = GFX_SWIZZLE_ONE,
					[1] = GFX_SWIZZLE_ONE,
					[2] = GFX_SWIZZLE_ONE,
					[3] = GFX_SWIZZLE_R,
				  },
				}),
			.width = font.img_atlas.width,
			.height = font.img_atlas.height,
		};
	}

	arena_scratch_end(scratch);
	*out = font;
	return font.tex_atlas.handle != 0;
}

static bool res__load(RES_Cache *cache, RES_AssetID id, RES_Key key, void *out) {
	switch (key.output_type) {
		case RES_INSTANCE_TYPE_IMAGE:
			return default_loader_image(cache, id, out);
		case RES_INSTANCE_TYPE_TEXTURE:
			return res__loader_texture(cache, id, key, out);
		case RES_INSTANCE_TYPE_SHADER:
			return res__loader_shader(cache, id, key, out);
		case RES_INSTANCE_TYPE_FONT:
			return res__loader_font(cache, id, key, out);
		default:
			ASSERT(0);
			return false;
	}
}

RES_Cache res_cache_make(Arena *arena, GFX_Device *device, const RES_Catalog *catalog) {
	ASSERT(arena && device && catalog && catalog->fonts && catalog->images && catalog->shaders);
	RES_Cache result = { .arena = arena, .device = device };

	for (uint32_t index = 0; index < RES_INSTANCE_TYPE_MAX; ++index) {
		RES_InstancePool *pool = &result.instances[index];

		pool->count = 0;
		pool->stride = res_instance_strides[index];
		pool->capacity = res_instance_limits[index];

		pool->data = arena_push(arena, (uint64_t)pool->stride * pool->capacity, alignof(long double), true);
	}

	result.catalog = res__catalog_deep_copy(arena, catalog);
	result.record_counts[RES_ASSET_TYPE_IMAGE] = result.catalog.image_count;
	result.record_counts[RES_ASSET_TYPE_SHADER] = result.catalog.shader_count;
	result.record_counts[RES_ASSET_TYPE_FONT] = result.catalog.font_count;
	for (uint32_t index = 0; index < RES_ASSET_TYPE_MAX; ++index)
		if (result.record_counts[index])
			result.records[index] = arena_push_count(arena, RES_Record, result.record_counts[index]);

	return result;
}

RES_Image2D *res_image(RES_Cache *cache, RES_AssetID id) {
	ASSERT(cache);
	RES_Image2D *result = res__acquire(cache, RES_ASSET_TYPE_IMAGE, id, (RES_Key){ .output_type = RES_INSTANCE_TYPE_IMAGE });
	return result ? result : &cache->fallback.image;
}

RES_Texture2D *res_texture(RES_Cache *cache, RES_AssetID id, PixelFormat fmt) {
	ASSERT(cache);
	RES_Texture2D *result = res__acquire(cache, RES_ASSET_TYPE_IMAGE, id, (RES_Key){ .output_type = RES_INSTANCE_TYPE_TEXTURE, .variant = fmt });
	return result ? result : &cache->fallback.texture;
}

RES_Shader *res_shader(RES_Cache *cache, RES_AssetID id) {
	ASSERT(cache);

	RES_Shader *result = res__acquire(cache, RES_ASSET_TYPE_SHADER, id, (RES_Key){ .output_type = RES_INSTANCE_TYPE_SHADER });
	return result ? result : &cache->fallback.shader;
}

ENGINE_API RES_Font *res_font_ex(RES_Cache *cache, RES_AssetID id, RES_FontWeight weight, RES_FontStyle style, uint32_t font_size) {
	ASSERT(cache && cache->catalog.fonts && id < cache->catalog.font_count);

	RES_FontFamilyMeta *meta = &cache->catalog.fonts[id];
	uint32_t face_index = res__find_closest_font_face(meta, weight, style);
	ASSERT(face_index != UINT32_MAX);

	weight = clamp1u(weight, meta->faces[face_index].min_weight, meta->faces[face_index].max_weight); // normalize

	RES_Font *result = res__acquire(cache, RES_ASSET_TYPE_FONT, id,
		(RES_Key){ .output_type = RES_INSTANCE_TYPE_FONT, .variant = ((uint64_t)style << 48) | ((uint64_t)weight << 32) | ((uint64_t)font_size) });
	return result ? result : &cache->fallback.font;
}

void res_cache_tick(Arena *frame_arena, RES_Cache *cache) {
	for (RES_AssetID id = 0; id < cache->catalog.shader_count; ++id) {
		RES_ShaderMeta *meta = &cache->catalog.shaders[id];
		RES_Record *record = &cache->records[RES_ASSET_TYPE_SHADER][id];

		OS_Timestamp mtime = res__combined_mtime(meta->filepaths, SHADER_STAGE_MAX);
		if (mtime == record->mtime) continue;

		RES_InstanceSlot *slot = record->first_instance;
		if (slot == 0 || slot->state != RES_STATE_LOADED) continue;

		RES_Shader *shader = res__instance_at(cache, RES_INSTANCE_TYPE_SHADER, record->first_instance->index);

		LOG_INFO("hot-reloading %.*s...", arg8(meta->name));
		record->mtime = mtime;

		GFX_Shader *new_handle = res__shader_make(frame_arena, cache, meta);
		if (new_handle == 0) {
			LOG_WARN("failed to hot-reload %.*s; keeping previous shader", arg8(meta->name));
			continue;
		}

		gfx_device_wait_idle(cache->device);
		gfx_shader_destroy(cache->device, shader->handle);
		shader->handle = new_handle;

		for (uint32_t pipline_index = 0; pipline_index < meta->pipeline_count; ++pipline_index)
			gfx_pipeline_register(cache->device, shader->handle, meta->pipelines[pipline_index]);
	}
}

float2 measure_text(RES_Font *font, string8 text) {
	float2 result = { 0.0f, 0.0f };
	float x_offset = 0.0f;

	bool ok = font && font->glyphs && text.bytes && text.length;
	/* if (ok) { */
	/* 	for (uint32_t index = 0; index < text.length; ++index) { */
	/* 		uint8_t codepoint = text.bytes[index]; */

	/* 		if (codepoint == '\n') { */
	/* 			result.x = maxf(result.x, x_offset); */
	/* 			result.y += font->bake_size; */
	/* 			x_offset = 0.0f; */
	/* 			continue; */
	/* 		} */

	/* 		uint32_t first = font->first_codepoint; */
	/* 		uint32_t last = first + font->glyph_count; */
	/* 		if (codepoint < first || codepoint >= last) */
	/* 			codepoint = '?'; */

	/* 		RES_Glyph *glyph = &font->glyphs[codepoint - first]; */
	/* 		x_offset += glyph->advance; */
	/* 	} */

	/* 	result.x = maxf(result.x, x_offset); */
	/* 	result.y += font->greatest_top_y + font->greatest_bottom_y; */
	/* } */
	return result;
}

#include <stb/stb_truetype.h>
#define RES_FONT_ATLAS_MAX 1024
RES_Font res__load_font(Arena *arena, string8 path, RES_FontWeight weight, uint32_t font_size) {
	ArenaTemp scratch = arena_scratch_begin(arena);
	RES_Font result = { 0 };

	string8 file_content = os_file_read(scratch.arena, path);
	stbtt_fontinfo font_info = { 0 };

	bool ok = arena && file_content.length;
	if (ok) {
		ok = stbtt_InitFont(&font_info, file_content.bytes, 0);

		if (ok == false)
			LOG_WARN("%s - failed to process font data", __func__);
	}

	if (ok) {
		float scale_factor = stbtt_ScaleForPixelHeight(&font_info, (float)font_size);

		int32_t ascent = 0, descent = 0, line_gap = 0;
		if (!stbtt_GetFontVMetricsOS2(&font_info, &ascent, &descent, &line_gap))
			stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &line_gap);

		result.ascent = ascent * scale_factor;
		result.descent = descent * scale_factor;
		result.line_gap = line_gap * scale_factor;

		result.first_codepoint = 0x20;
		result.glyph_count = 0x100 - 0x20; // 224
		result.glyphs = arena_push_count(arena, RES_Glyph, result.glyph_count);
		result.weight = weight;

		result.img_atlas = (RES_Image2D){
			.pixels = arena_push_count(arena, uint8_t, RES_FONT_ATLAS_MAX *RES_FONT_ATLAS_MAX),
			.channels = 1,
			.width = RES_FONT_ATLAS_MAX,
			.height = RES_FONT_ATLAS_MAX,
		};

		uint32_t padding = 2;
		uint32_t row = 0;
		uint32_t col = padding;

		for (uint32_t index = 0; index < result.glyph_count; ++index) {
			int32_t codepoint = result.first_codepoint + index;

			int glyph_index = stbtt_FindGlyphIndex(&font_info, codepoint);
			if (glyph_index == 0) continue;

			int32_t x0, y0, x1, y1, advance;
			stbtt_GetGlyphBitmapBox(&font_info, glyph_index, scale_factor, scale_factor, &x0, &y0, &x1, &y1);
			stbtt_GetGlyphHMetrics(&font_info, glyph_index, &advance, NULL);

			uint32_t width = x1 - x0, height = y1 - y0;
			if (col + width + padding >= RES_FONT_ATLAS_MAX) {
				col = padding;
				row += (uint32_t)(font_size + 0.5f);
				ASSERT(row + y1 - y0 < RES_FONT_ATLAS_MAX);
			}
			stbtt_MakeGlyphBitmap(&font_info, (uint8_t *)result.img_atlas.pixels + col + (row * RES_FONT_ATLAS_MAX), width, height, RES_FONT_ATLAS_MAX, scale_factor, scale_factor, glyph_index);
			result.glyphs[index] = (RES_Glyph){
				.codepoint = codepoint,
				.uv = { .x = col, .y = row, .width = width, .height = height },
				.bearing = { x0, y0 },
				.advance = (int32_t)(advance * scale_factor),
			};

			col += width + padding;
		}
	}

	arena_scratch_end(scratch);
	return result;
}

uint32_t res__find_closest_font_face(RES_FontFamilyMeta *meta, RES_FontWeight weight, RES_FontStyle style) {
	ASSERT(meta->faces && meta->face_count);

	uint32_t result = UINT32_MAX;
	uint32_t min_diff = UINT32_MAX;

	for (uint32_t index = 0; index < meta->face_count; ++index) {
		RES_FontFaceMeta *face = &meta->faces[index];
		if (face->style != style) { continue; }

		uint32_t clamped = clamp1u(weight, face->min_weight, face->max_weight);
		uint32_t diff = clamped > weight ? clamped - weight : weight - clamped;
		if (diff < min_diff) {
			min_diff = diff;
			result = index;
		}
	}

	ASSERT(result != UINT32_MAX);
	return result;
}

static RES_Catalog res__catalog_deep_copy(Arena *arena, const RES_Catalog *catalog) {
	RES_Catalog result = { 0 };

	result.image_count = catalog->image_count;
	result.font_count = catalog->font_count;
	result.shader_count = catalog->shader_count;

	if (result.image_count) {
		result.images = arena_push_count(arena, RES_ImageMeta, result.image_count);
		for (uint32_t image_index = 0; image_index < result.image_count; ++image_index) {
			RES_ImageMeta *image_dst = &result.images[image_index];
			RES_ImageMeta *image_src = &catalog->images[image_index];

			image_dst->uuid = image_src->uuid;
			image_dst->name = copy8(arena, image_src->name);
			image_dst->description = copy8(arena, image_src->description);
			image_dst->filepath = copy8(arena, image_src->filepath);
		}
	}

	if (result.font_count) {
		result.fonts = arena_push_count(arena, RES_FontFamilyMeta, result.font_count);

		for (uint32_t family_index = 0; family_index < result.font_count; ++family_index) {
			RES_FontFamilyMeta *family_dst = &result.fonts[family_index];
			RES_FontFamilyMeta *family_src = &catalog->fonts[family_index];

			family_dst->uuid = family_src->uuid;
			family_dst->name = copy8(arena, family_src->name);
			family_dst->face_count = family_src->face_count;

			if (family_dst->face_count) {
				family_dst->faces = arena_push_count(arena, RES_FontFaceMeta, family_dst->face_count);

				for (uint32_t face_index = 0; face_index < family_dst->face_count; ++face_index) {
					RES_FontFaceMeta *face_dst = &family_dst->faces[face_index];
					RES_FontFaceMeta *face_src = &family_src->faces[face_index];

					face_dst->filepath = copy8(arena, face_src->filepath);
					face_dst->style = face_src->style;
					face_dst->min_weight = face_src->min_weight;
					face_dst->max_weight = face_src->max_weight;
				}
			}
		}
	}

	if (result.shader_count) {
		result.shaders = arena_push_count(arena, RES_ShaderMeta, result.shader_count);

		for (uint32_t shader_index = 0; shader_index < result.shader_count; ++shader_index) {
			RES_ShaderMeta *shader_dst = &result.shaders[shader_index];
			RES_ShaderMeta *shader_src = &catalog->shaders[shader_index];

			shader_dst->uuid = shader_src->uuid;
			shader_dst->name = copy8(arena, shader_src->name);
			for (uint32_t stage_index = 0; stage_index < SHADER_STAGE_MAX; ++stage_index)
				shader_dst->filepaths[stage_index] = copy8(arena, shader_src->filepaths[stage_index]);

			shader_dst->pipeline_count = shader_src->pipeline_count;
			memory_copy_count(shader_dst->pipelines, shader_src->pipelines, shader_dst->pipeline_count);
		}
	}

	return result;
}
RES_Record *res__record(RES_Cache *cache, RES_AssetType asset_type, RES_AssetID id) {
	if (cache == 0 || asset_type >= RES_ASSET_TYPE_MAX || id >= cache->record_counts[asset_type])
		return 0;

	return &cache->records[asset_type][id];
}
RES_InstanceSlot *res__find_slot(RES_Record *record, RES_Key key) {
	RES_InstanceSlot *result = 0;

	bool ok = record;
	if (ok) {
		for (RES_InstanceSlot *slot = record->first_instance; slot; slot = slot->next) {
			if (key.output_type == slot->key.output_type && key.subresource == slot->key.subresource && key.variant == slot->key.variant) {
				result = slot;
				break;
			}
		}
	}

	return result;
}
static RES_InstanceSlot *res__ensure_slot(RES_Cache *cache, RES_AssetType type, RES_AssetID id, RES_Key key) {
	RES_Record *record = 0;
	RES_InstanceSlot *result = 0;

	bool ok = cache && (record = res__record(cache, type, id));
	if (ok) {
		if (record->mtime == 0)
			record->mtime = res__source_mtime(cache, type, id);

		result = res__find_slot(record, key);
		if (result == 0) {
			RES_InstancePool *pool = &cache->instances[key.output_type];
			ASSERT_MESSAGE(pool->count < pool->capacity, "Resource Pool overflow.");
			if (pool->count < pool->capacity) {
				result = arena_push_count(cache->arena, RES_InstanceSlot, 1);
				result->next = record->first_instance;
				record->first_instance = result;

				result->index = cache->instances[key.output_type].count++;
				result->key = key;
			}
		}
	}

	return result;
}
void *res__instance_at(RES_Cache *cache, RES_InstanceType type, uint32_t index) {
	RES_InstancePool *pool = &cache->instances[type];
	ASSERT(index < pool->count);
	return (uint8_t *)pool->data + (size_t)pool->stride * index;
}
void *res__acquire(RES_Cache *cache, RES_AssetType asset_type, RES_AssetID id, RES_Key key) {
	void *result = 0;

	RES_InstanceSlot *slot = res__ensure_slot(cache, asset_type, id, key);
	bool ok = slot != 0;

	if (ok) {
		result = res__instance_at(cache, key.output_type, slot->index);
		if (slot->state == RES_STATE_UNLOADED) {
			slot->state = RES_STATE_LOADING;

			slot->state = res__load(cache, id, key, result) ? RES_STATE_LOADED : RES_STATE_FAILED;
		}

		result = slot->state == RES_STATE_LOADED ? result : 0;
	}

	return result;
}
OS_Timestamp res__combined_mtime(string8 *filepaths, uint32_t count) {
	OS_Timestamp latest = { 0 };
	for (uint32_t i = 0; i < count; ++i) {
		if (filepaths[i].length == 0) continue;
		OS_Timestamp ts = os_file_mtime(filepaths[i]);
		if (ts > latest) latest = ts;
	}
	return latest;
}
OS_Timestamp res__source_mtime(RES_Cache *cache, RES_AssetType asset_type, RES_AssetID id) {
	OS_Timestamp latest = 0;
	switch (asset_type) {
		case RES_ASSET_TYPE_IMAGE:
			latest = res__combined_mtime(&cache->catalog.images[id].filepath, 1);
			break;
		case RES_ASSET_TYPE_SHADER:
			latest = res__combined_mtime(cache->catalog.shaders[id].filepaths, SHADER_STAGE_MAX);
			break;
		case RES_ASSET_TYPE_FONT: {
			RES_FontFamilyMeta *family = &cache->catalog.fonts[id];
			for (uint32_t face_index = 0; face_index < family->face_count; ++face_index) {
				uint64_t mtime = os_file_mtime(family->faces[face_index].filepath);
				latest = MAX(latest, mtime);
			}
		} break;
		default:
			break;
	}
	return latest;
}
