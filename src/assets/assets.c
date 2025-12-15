#include "assets.h"

#include "allocators/arena.h"
#include "core/hash_trie.h"
#include "core/logger.h"
#include "platform/filesystem.h"

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

static const char *extensions[ASSET_TYPE_COUNT][8] = {
	[ASSET_TYPE_UNDEFINED] = { NULL },
	[ASSET_TYPE_MESH] = { "glb", "gltf", NULL },
	[ASSET_TYPE_TEXTURE] = { "png", "jpeg", "jpg", NULL }
};

static AssetType file_extension_to_asset_type(String extension);

bool asset_library_track_directory(String directory) {
	ArenaTemp scratch = arena_scratch(NULL);

	FileNode *head = filesystem_load_directory_files(scratch.arena, directory, true);

	while (head) {
		asset_library_track_file(head->path);

		head = head->next;
	}

	arena_release_scratch(scratch);
	return true;
}

bool asset_library_track_file(String file_path) {
	ArenaTemp scratch = arena_scratch(NULL);
	String name = string_filename_from_path(scratch.arena, file_path);
	AssetEntry *entry = hash_trie_insert(library->arena, &library->root, name, AssetEntry);

	if (entry->full_path.length == 0) {
		entry->full_path = string_copy(library->arena, file_path);
		LOG_INFO("Asset: File '%s' tracked", name.data);
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
