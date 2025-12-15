#pragma once

#include "allocators/arena.h"
#include "common.h"
#include "core/hash_trie.h"

typedef enum {
	ASSET_TYPE_UNDEFINED ,
	ASSET_TYPE_MESH ,
	ASSET_TYPE_TEXTURE ,
	ASSET_TYPE_COUNT,
} AssetType;

typedef struct {
	HashTrieNode node;

	String full_path;
	AssetType type;
	uint64_t last_modified;

	void *source_data;
} AssetEntry;

typedef struct asset_library {
	AssetEntry *root;

	uint32_t tracked_file_count;

	Arena arena;
} AssetLibrary;

bool asset_library_track_directory(AssetLibrary *library, String directory);
bool asset_library_track_file(AssetLibrary *library, String file_path);
