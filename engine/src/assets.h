#pragma once

#include "assets/asset_types.h"

#include "common.h"
#include "core/arena.h"

typedef struct asset_entry {
	UUID id;
	String full_path;

	AssetType type;
	uint64_t last_modified;
} AssetEntry;

typedef struct asset_store {
	Arena *arena;
	ArenaTrie trie;

	uint32_t tracked_file_count;
} AssetStore;

static inline AssetStore asset_store_make(Arena *arena) { return (AssetStore){ .arena = arena, .trie = arena_trie_make(arena) }; }

ENGINE_API bool asset_store_track_directory(AssetStore *store, String directory);
ENGINE_API bool asset_store_track_file(AssetStore *store, String file_path);

ENGINE_API UUID asset_store_find(AssetStore *store, AssetType type, String key);
ENGINE_API UUID asset_store_find_shader(AssetStore *store, String key);
ENGINE_API UUID asset_store_find_model(AssetStore *store, String key);
ENGINE_API UUID asset_store_find_image(AssetStore *store, String key);

bool asset_store_clear(AssetStore *store);
