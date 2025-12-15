#include "assets.h"

#include "allocators/arena.h"
#include "core/hash_trie.h"
#include "core/logger.h"

static const char *extensions[ASSET_TYPE_COUNT][3] = {
	[ASSET_TYPE_UNDEFINED] = { NULL },
	[ASSET_TYPE_MESH] = { "glb", "gltf" },
	[ASSET_TYPE_TEXTURE] = { "png", "jpeg" }
};

static AssetType file_extension_to_asset_type(String extension);

bool asset_library_track_directory(AssetLibrary *library, String directory) {
	return true;
}

bool asset_library_track_file(AssetLibrary *library, String file_path) {
	ArenaTemp scratch_arena = arena_get_scratch(NULL);
	String name = string_filename_from_path(scratch_arena.arena, file_path);
	AssetEntry *entry = hash_trie_insert(&library->arena, &library->root, name, AssetEntry);

	if (entry->full_path.length == 0) {
		entry->full_path = string_copy(&library->arena, file_path);
		entry->type = file_extension_to_asset_type(string_extension_from_path(scratch_arena.arena, file_path));
		entry->source_data = NULL;
		entry->last_modified = 0;

		library->tracked_file_count++;

		arena_reset_scratch(scratch_arena);
		return true;
	}

	arena_reset_scratch(scratch_arena);
	return false;
}
static AssetType file_extension_to_asset_type(String extension) {
	for (uint32_t asset_index = 0; asset_index < ASSET_TYPE_COUNT; ++asset_index) {
		uint32_t extension_count = countof(extensions[asset_index]);
		for (uint32_t extension_index = 0; extension_index < extension_count; ++extension_index) {
			const char *ext = extensions[asset_index][extension_index];

			if (ext && string_equals(extension, S(ext)))
				return asset_index;
		}
	}

	return 0;
}
