#include "assets.h"

#include "allocators/arena.h"
#include "assets/asset_types.h"
#include "assets/importer.h"
#include "core/astring.h"
#include "core/hash_trie.h"
#include "core/identifiers.h"
#include "core/logger.h"
#include "platform/filesystem.h"

static const char *extensions[ASSET_TYPE_COUNT][8] = {
	[ASSET_TYPE_UNDEFINED] = { NULL },
	[ASSET_TYPE_GEOMETRY] = { "glb", "gltf", NULL },
	[ASSET_TYPE_IMAGE] = { "png", "jpeg", "jpg", NULL },
	[ASSET_TYPE_SHADER] = { "glsl", "spv" },
};

static AssetLibrary *library = { 0 };

static AssetType file_extension_to_asset_type(String extension);

bool asset_library_startup(void *memory, size_t offset, size_t size) {
	size_t minimum_footprint = sizeof(AssetLibrary) + sizeof(Arena);

	if (size < minimum_footprint) {
		LOG_ERROR("Asset: Failed to startup asset library, memory footprint too small");
		return false;
	}
	library = (AssetLibrary *)((uint8_t *)memory + offset);
	library->arena = (Arena *)(library + 1);

	*library->arena = (Arena){
		.memory = library->arena + 1,
		.offset = 0,
		.capacity = size - minimum_footprint
	};

	return true;
}

bool asset_library_track_directory(String directory) {
	ArenaTemp scratch = arena_scratch(NULL);

	FileNode *head = filesystem_load_directory_files(scratch.arena, directory, true);
	uint32_t count = 0;

	while (head) {
		asset_library_track_file(head->path);
		count++;

		head = head->next;
	}

	LOG_INFO("AssetLibrary: %d files tracked", count);
	arena_release_scratch(scratch);
	return true;
}

bool asset_library_track_file(String file_path) {
	ArenaTemp scratch = arena_scratch(NULL);
	String name = string_filename_from_path(scratch.arena, file_path);
	AssetEntry *entry = hash_trie_insert(library->arena, &library->root, name, AssetEntry);

	if (entry->full_path.length == 0) {
		entry->full_path = string_copy(library->arena, file_path);
		entry->type = file_extension_to_asset_type(string_extension_from_path(scratch.arena, file_path));
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

UUID asset_library_load_shader(Arena *arena, String key, ShaderSource **out_shader) {
	// if (out_shader == NULL)
	// 	return INVALID_UUID;

	ArenaTemp scratch = arena_scratch(arena);
	String vertex_shader_key = string_find_and_replace(scratch.arena, key, SLITERAL("glsl"), SLITERAL("vert.spv"));
	String fragment_shader_key = string_find_and_replace(scratch.arena, key, SLITERAL("glsl"), SLITERAL("frag.spv"));

	AssetEntry *vs_entry = hash_trie_lookup(&library->root, vertex_shader_key, AssetEntry);
	AssetEntry *fs_entry = hash_trie_lookup(&library->root, vertex_shader_key, AssetEntry);
	if (vs_entry == NULL || fs_entry == NULL) {
		LOG_WARN("Assets: Key '%s' is not tracked", key.data);
		*out_shader = NULL;
		arena_release_scratch(scratch);
		return INVALID_UUID;
	}

	if (vs_entry->type != ASSET_TYPE_SHADER || fs_entry->type != ASSET_TYPE_SHADER) {
		LOG_ERROR("Assets: Key '%s' is not geometry");
		*out_shader = NULL;
		arena_release_scratch(scratch);
		return INVALID_UUID;
	}

	*out_shader = arena_push_struct_zero(arena, ShaderSource);
	if (importer_load_shader(arena, vs_entry->full_path, fs_entry->full_path, *out_shader) == false) {
		LOG_WARN("Assets: Failed to load '%s'", key);
		return INVALID_UUID;
	}

	arena_release_scratch(scratch);
	(*out_shader)->id = vs_entry->node.hash;
	return vs_entry->node.hash;
}

UUID asset_library_load_model(Arena *arena, String key, ModelSource **out_model, bool use_cached_textures) {
	AssetEntry *entry = hash_trie_lookup(&library->root, key, AssetEntry);
	if (entry == NULL) {
		LOG_WARN("Assets: Key '%s' is not tracked", key.data);
		*out_model = NULL;
		return INVALID_UUID;
	}

	if (entry->type != ASSET_TYPE_GEOMETRY) {
		LOG_ERROR("Assets: Key '%s' is not geometry");
		*out_model = NULL;
		return INVALID_UUID;
	}

	*out_model = arena_push_struct_zero(arena, ModelSource);
	if (importer_load_gltf(arena, entry->full_path, *out_model) == false) {
		LOG_WARN("Assets: Failed to load '%s'", key);
		return INVALID_UUID;
	}

	ArenaTemp scratch = arena_scratch(arena);
	for (uint32_t image_index = 0; image_index < (*out_model)->image_count; ++image_index) {
		String path = (*out_model)->images[image_index].path;
		String name = string_filename_from_path(scratch.arena, path);

		AssetEntry *entry = hash_trie_lookup(&library->root, name, AssetEntry);
		if (entry == NULL) {
			asset_library_track_file(path);
		}

		Image *image = NULL;
		if (use_cached_textures)
			asset_library_request_image(name, &image);
		else
			asset_library_load_image(arena, name, &image);

		(*out_model)->images[image_index] = *image;
	}

	arena_release_scratch(scratch);
	return entry->node.hash;
}

UUID asset_library_load_image(Arena *arena, String key, Image **out_texture) {
	AssetEntry *entry = hash_trie_lookup(&library->root, key, AssetEntry);

	if (entry == NULL) {
		LOG_WARN("Assets: Key '%s' is not tracked", key.data);
		*out_texture = NULL;
		return INVALID_UUID;
	}

	if (entry->type != ASSET_TYPE_IMAGE) {
		LOG_ERROR("Assets: Key '%s' is not a image");
		*out_texture = NULL;
		return INVALID_UUID;
	}

	*out_texture = arena_push_struct_zero(arena, Image);
	importer_load_image(arena, entry->full_path, *out_texture);

	(*out_texture)->id = entry->node.hash;
	return entry->node.hash;
}

AssetType file_extension_to_asset_type(String extension) {
	for (uint32_t asset_index = 0; asset_index < ASSET_TYPE_COUNT; ++asset_index) {
		uint32_t index = 0;
		const char *ext = extensions[asset_index][index];
		while (ext) {
			if (string_equals(extension, S(ext)))
				return asset_index;

			ext = extensions[asset_index][++index];
		}
	}

	return ASSET_TYPE_UNDEFINED;
}

UUID asset_library_request_model(String key, ModelSource **out_model) {
	AssetEntry *entry = hash_trie_lookup(&library->root, key, AssetEntry);
	if (!entry)
		return INVALID_UUID;

	if (entry->type != ASSET_TYPE_GEOMETRY) {
		LOG_ERROR("AssetLibrary: Requested model '%s' is type %d", key.data, entry->type);
		return INVALID_UUID;
	}

	if (entry->is_loaded) {
		*out_model = (ModelSource *)entry->source_data;
		return entry->node.hash;
	}

	*out_model = arena_push_struct(library->arena, ModelSource);
	if (importer_load_gltf(library->arena, entry->full_path, *out_model) == false) {
		LOG_WARN("AssetLibrary: Failed to load model '%s'", key);
		return INVALID_UUID;
	}

	ArenaTemp scratch = arena_scratch(NULL);
	for (uint32_t image_index = 0; image_index < (*out_model)->image_count; ++image_index) {
		String path = (*out_model)->images[image_index].path;
		String name = string_filename_from_path(scratch.arena, path);

		if (hash_trie_lookup(&library->root, name, AssetEntry) == NULL) {
			asset_library_track_file(path);
		}

		Image *cached_image = NULL;
		asset_library_request_image(name, &cached_image);

		if (cached_image)
			(*out_model)->images[image_index] = *cached_image;
	}

	arena_release_scratch(scratch);

	entry->source_data = *out_model;
	entry->is_loaded = true;
	return entry->node.hash;
}

UUID asset_library_request_image(String key, Image **out_image) {
	AssetEntry *entry = hash_trie_lookup(&library->root, key, AssetEntry);
	if (entry == NULL) {
		LOG_WARN("AssetLibrary: No image '%s'", key);
		return INVALID_UUID;
	}

	if (entry->type != ASSET_TYPE_IMAGE) {
		LOG_ERROR("AssetLibrary: Requested image '%s' is type type %d", key.data, entry->type);
		return INVALID_UUID;
	}

	if (entry->is_loaded) {
		*out_image = (Image *)entry->source_data;
		return entry->node.hash;
	}

	*out_image = arena_push_struct(library->arena, Image);
	if (importer_load_image(library->arena, entry->full_path, *out_image)) {
		(*out_image)->id = entry->node.hash;

		entry->source_data = *out_image;
		entry->is_loaded = true;
		return entry->node.hash;
	}

	LOG_WARN("AssetLibrary: Failed to load image '%s'", key);
	return INVALID_UUID;
}

bool asset_library_clear_cache(void) {
	arena_clear(library->arena);

	library->root = NULL;
	library->tracked_file_count = 0;

	LOG_INFO("AssetLibrary: Cache cleared. All tracking lost.");

	return true;
}
