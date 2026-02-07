#include "assets.h"
#include "assets/asset_types.h"
#include "assets/importer.h"

#include "core/arena.h"

#include "common.h"
#include "core/astring.h"
#include "core/hash_trie.h"
#include "core/identifiers.h"
#include "core/logger.h"

#include "platform/filesystem.h"

#include <stdalign.h>

static const char *extensions[ASSET_TYPE_COUNT][8] = {
	[ASSET_TYPE_UNDEFINED] = { NULL },
	[ASSET_TYPE_GEOMETRY] = { "glb", "gltf", NULL },
	[ASSET_TYPE_IMAGE] = { "png", "jpeg", "jpg", NULL },
	[ASSET_TYPE_SHADER] = { "glsl", "spv" },
};

static AssetType file_extension_to_asset_type(String extension);

bool asset_library_startup(AssetLibrary *library, void *memory, size_t size) {
	size_t minimum_footprint = sizeof(Arena);

	uintptr_t base_addr = (uintptr_t)memory;
	uintptr_t aligned_addr = aligned_address((uintptr_t)memory, alignof(Arena));
	size_t padding = aligned_addr - base_addr;

	if (size < minimum_footprint + padding) {
		LOG_ERROR("Asset: Failed to startup asset library, memory footprint too small");
		return false;
	}
	Arena *arena = (Arena *)aligned_addr;
	library->arena = arena;

	*arena = (Arena){
		.memory = arena + 1,
		.offset = 0,
		.capacity = size - sizeof(Arena)
	};

	return true;
}

bool asset_library_track_directory(AssetLibrary *library, String directory) {
	ArenaTemp scratch = arena_scratch(NULL);

	StringList file_list = filesystem_list_files(scratch.arena, directory, true);
	StringNode *file = file_list.first;
	uint32_t count = 0;

	logger_indent();
	while (file) {
		asset_library_track_file(library, file->string);
		count++;

		file = file->next;
	}
	logger_dedent();

	LOG_INFO("AssetLibrary: %d files tracked", count);
	arena_release_scratch(scratch);
	return true;
}

bool asset_library_track_file(AssetLibrary *library, String file_path) {
	ArenaTemp scratch = arena_scratch(NULL);
	String name = string_push_copy(scratch.arena, string_path_filename(file_path));
	AssetEntry *entry = hash_trie_insert(library->arena, &library->root, name, AssetEntry);

	if (entry->full_path.length == 0) {
		entry->full_path = string_push_copy(library->arena, file_path);
		entry->type = file_extension_to_asset_type(
			string_push_copy(scratch.arena, string_path_extension(file_path)));
		entry->source_data = NULL;

		entry->is_loaded = false;
		entry->last_modified = filesystem_last_modified(file_path);

		library->tracked_file_count++;

		arena_release_scratch(scratch);
		return true;
	}

	arena_release_scratch(scratch);
	return false;
}

UUID asset_library_load_shader(Arena *arena, AssetLibrary *library, String key, ShaderSource **out_shader) {
	ArenaTemp scratch = arena_scratch(arena);

	String vertex_shader_key = string_push_replace(scratch.arena, key, str_lit("glsl"), str_lit("vert.spv"));
	String fragment_shader_key = string_push_replace(scratch.arena, key, str_lit("glsl"), str_lit("frag.spv"));

	AssetEntry *vs_entry = hash_trie_lookup(&library->root, vertex_shader_key, AssetEntry);
	AssetEntry *fs_entry = hash_trie_lookup(&library->root, fragment_shader_key, AssetEntry);
	if (vs_entry == NULL || fs_entry == NULL) {
		LOG_WARN("Assets: Key '%.*s' is not tracked", str_expand(key));
		*out_shader = NULL;
		arena_release_scratch(scratch);
		return 0;
	}

	if (vs_entry->type != ASSET_TYPE_SHADER || fs_entry->type != ASSET_TYPE_SHADER) {
		LOG_ERROR("Assets: Key '%.*s' is not a shader", str_expand(key));
		*out_shader = NULL;
		arena_release_scratch(scratch);
		return 0;
	}

	*out_shader = arena_push_struct_zero(arena, ShaderSource);
	if (importer_load_shader(arena, vs_entry->full_path, fs_entry->full_path, *out_shader) == false) {
		LOG_WARN("Assets: Failed to load '%s'", key);
		return 0;
	}

	(*out_shader)->path = string_push_path_join(arena,
		string_path_folder(vs_entry->full_path), key);
	(*out_shader)->id = string_hash64((*out_shader)->path);

	arena_release_scratch(scratch);
	return (*out_shader)->id;
}

UUID asset_library_load_model(Arena *arena, AssetLibrary *library, String key, ModelSource **out_model, bool use_cached_textures) {
	AssetEntry *entry = hash_trie_lookup(&library->root, key, AssetEntry);
	if (entry == NULL) {
		LOG_WARN("Assets: Key '%.*s' is not tracked", str_expand(key));
		*out_model = NULL;
		return 0;
	}

	if (entry->type != ASSET_TYPE_GEOMETRY) {
		LOG_ERROR("Assets: Key '%.*s' is not geometry", str_expand(key));
		*out_model = NULL;
		return 0;
	}

	*out_model = arena_push_struct_zero(arena, ModelSource);
	if (importer_load_gltf(arena, entry->full_path, *out_model) == false) {
		LOG_WARN("Assets: Failed to load '%s'", key);
		return 0;
	}

	ArenaTemp scratch = arena_scratch(arena);
	for (uint32_t image_index = 0; image_index < (*out_model)->image_count; ++image_index) {
		String path = (*out_model)->images[image_index].path;
		String name = string_push_copy(scratch.arena, string_path_filename(path));

		AssetEntry *entry = hash_trie_lookup(&library->root, name, AssetEntry);
		if (entry == NULL) {
			asset_library_track_file(library, path);
		}

		ImageSource *image = NULL;
		if (use_cached_textures)
			asset_library_request_image(library, name, &image);
		else
			asset_library_load_image(arena, library, name, &image);

		(*out_model)->images[image_index] = *image;
	}

	arena_release_scratch(scratch);
	return entry->node.hash;
}

UUID asset_library_load_image(Arena *arena, AssetLibrary *library, String key, ImageSource **out_texture) {
	AssetEntry *entry = hash_trie_lookup(&library->root, key, AssetEntry);

	if (entry == NULL) {
		LOG_WARN("Assets: Key '%.*s' is not tracked", str_expand(key));
		*out_texture = NULL;
		return 0;
	}

	if (entry->type != ASSET_TYPE_IMAGE) {
		LOG_ERROR("Assets: Key '%.*s' is not a image", str_expand(key));
		*out_texture = NULL;
		return 0;
	}

	*out_texture = arena_push_struct_zero(arena, ImageSource);
	importer_load_image(arena, entry->full_path, *out_texture);

	(*out_texture)->id = entry->node.hash;
	return entry->node.hash;
}

// TODO: Request functions call load functions which searches up entry agian, make use of internal load function that takes entry
UUID asset_library_request_shader(AssetLibrary *library, String key, ShaderSource **out_shader) {
	ArenaTemp scratch = arena_scratch(NULL);

	String vertex_shader_key = string_push_replace(scratch.arena, key, str_lit("glsl"), str_lit("vert.spv"));
	String fragment_shader_key = string_push_replace(scratch.arena, key, str_lit("glsl"), str_lit("frag.spv"));

	AssetEntry *vs_entry = hash_trie_lookup(&library->root, vertex_shader_key, AssetEntry);
	AssetEntry *fs_entry = hash_trie_lookup(&library->root, fragment_shader_key, AssetEntry);
	if (vs_entry == NULL || fs_entry == NULL) {
		LOG_WARN("Assets: Key '%.*s' is not tracked", str_expand(key));
		*out_shader = NULL;
		arena_release_scratch(scratch);
		return 0;
	}

	if (vs_entry->type != ASSET_TYPE_SHADER || fs_entry->type != ASSET_TYPE_SHADER) {
		LOG_ERROR("Assets: Key '%.*s' is not a shader", str_expand(key));
		*out_shader = NULL;
		arena_release_scratch(scratch);
		return 0;
	}

	if (vs_entry->is_loaded && fs_entry->is_loaded) {
		*out_shader = (ShaderSource *)vs_entry->source_data;
		arena_release_scratch(scratch);
		return vs_entry->node.hash;
	}

	if (asset_library_load_shader(library->arena, library, key, out_shader) == 0) {
		LOG_WARN("AssetLibrary: Failed to load shader '%s'", key.memory);
		return 0;
	}

	vs_entry->source_data = *out_shader;
	vs_entry->is_loaded = true;
	vs_entry->node.hash = string_hash64((*out_shader)->path);

	fs_entry->source_data = *out_shader;
	fs_entry->is_loaded = true;
	fs_entry->node.hash = vs_entry->node.hash;
	arena_release_scratch(scratch);
	LOG_INFO("Shader '%.*s' loaded to memory", str_expand(key));
	return vs_entry->node.hash;
}

UUID asset_library_request_model(AssetLibrary *library, String key, ModelSource **out_model) {
	AssetEntry *entry = hash_trie_lookup(&library->root, key, AssetEntry);
	if (entry == NULL)
		return 0;

	if (entry->type != ASSET_TYPE_GEOMETRY) {
		LOG_ERROR("AssetLibrary: Requested asset '%.*s' is type %d", str_expand(key), entry->type);
		return 0;
	}

	if (entry->is_loaded) {
		*out_model = (ModelSource *)entry->source_data;
		return entry->node.hash;
	}

	if (asset_library_load_model(library->arena, library, key, out_model, true) == 0) {
		LOG_ERROR("Failed to load '%.*s'", str_expand(key));
		return 0;
	}

	entry->source_data = *out_model;
	entry->is_loaded = true;
	return entry->node.hash;
}

UUID asset_library_request_image(AssetLibrary *library, String key, ImageSource **out_image) {
	AssetEntry *entry = hash_trie_lookup(&library->root, key, AssetEntry);
	if (entry == NULL) {
		LOG_WARN("AssetLibrary: No image '%.*s'", str_expand(key));
		return 0;
	}

	if (entry->type != ASSET_TYPE_IMAGE) {
		LOG_ERROR("AssetLibrary: Requested asset '%.*s' is type %d", str_expand(key), entry->type);
		return 0;
	}

	if (entry->is_loaded) {
		*out_image = (ImageSource *)entry->source_data;
		return entry->node.hash;
	}

	if (asset_library_load_image(library->arena, library, key, out_image) == 0) {
		LOG_WARN("AssetLibrary: Failed to load image '%s'", key.memory);
		return 0;
	}

	entry->source_data = *out_image;
	entry->is_loaded = true;
	return entry->node.hash;
}

bool asset_library_clear_cache(AssetLibrary *library) {
	arena_clear(library->arena);

	library->root = NULL;
	library->tracked_file_count = 0;

	LOG_INFO("AssetLibrary: Cache cleared. All tracking lost.");

	return true;
}

AssetType file_extension_to_asset_type(String extension) {
	for (uint32_t asset_index = 0; asset_index < ASSET_TYPE_COUNT; ++asset_index) {
		uint32_t index = 0;
		const char *ext = extensions[asset_index][index];
		while (ext) {
			if (string_equals(extension, string_from_cstr(ext)))
				return asset_index;

			ext = extensions[asset_index][++index];
		}
	}

	return ASSET_TYPE_UNDEFINED;
}
