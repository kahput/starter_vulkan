#include "assets.h"

#include "allocators/arena.h"
#include "assets/importer.h"
#include "core/hash_trie.h"
#include "core/logger.h"
#include "platform/filesystem.h"

static const char *extensions[ASSET_TYPE_COUNT][8] = {
	[ASSET_TYPE_UNDEFINED] = { NULL },
	[ASSET_TYPE_GEOMETRY] = { "glb", "gltf", NULL },
	[ASSET_TYPE_IMAGE] = { "png", "jpeg", "jpg", NULL }
};

static AssetLibrary *library = { 0 };

bool asset_library_startup(void *memory, size_t offset, size_t size) {
	size_t minimum_footprint = sizeof(AssetLibrary) + sizeof(Arena);

	if (size < minimum_footprint) {
		LOG_ERROR("Asset: Failed to startup asset library, memory footprint too small");
		return false;
	}
	library = (AssetLibrary *)((uint8_t *)memory + offset);
	library->arena = (Arena *)(library + 1);

	*library->arena = (Arena){
		.buffer = library->arena + 1,
		.offset = 0,
		.capacity = size - minimum_footprint
	};

	return true;
}

static AssetType file_extension_to_asset_type(String extension);

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
static AssetType file_extension_to_asset_type(String extension) {
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

ModelSource *asset_library_load_model(Arena *arena, String key) {
	AssetEntry *entry = hash_trie_lookup(&library->root, key, AssetEntry);

	if (entry == NULL) {
		LOG_WARN("Assets: Key '%s' is not tracked", key.data);
		return NULL;
	}

	if (entry->type != ASSET_TYPE_GEOMETRY) {
		LOG_ERROR("Assets: Key '%s' is not geometry");
		return NULL;
	}

	return importer_load_gltf(arena, entry->full_path);
}

TextureSource *asset_library_load_image(Arena *arena, String key) {
	AssetEntry *entry = hash_trie_lookup(&library->root, key, AssetEntry);

	if (entry == NULL) {
		LOG_WARN("Assets: Key '%s' is not tracked", key.data);
		return NULL;
	}

	if (entry->type != ASSET_TYPE_IMAGE) {
		LOG_ERROR("Assets: Key '%s' is not a image");
		return NULL;
	}

	return importer_load_image(arena, entry->full_path);
}
