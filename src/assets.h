#pragma once

#include "assets/asset_types.h"

#include "common.h"
#include "core/hash_trie.h"

#include "allocators/arena.h"

typedef enum {
	ASSET_TYPE_UNDEFINED,
	ASSET_TYPE_GEOMETRY,
	ASSET_TYPE_IMAGE,
	ASSET_TYPE_COUNT,
} AssetType;

typedef struct {
	HashTrieNode node;

	String full_path;
	AssetType type;
	uint64_t last_modified;

	bool is_loaded;
	void *source_data;
} AssetEntry;

typedef struct asset_library {
	Arena *arena;
	AssetEntry *root;

	uint32_t tracked_file_count;
} AssetLibrary;

bool asset_library_startup(void *memory, size_t offset, size_t size);

bool asset_library_track_directory(String directory);
bool asset_library_track_file(String file_path);

ModelSource *asset_library_load_model(Arena *arena, String key);
TextureSource *asset_library_load_image(Arena *arena, String key);
