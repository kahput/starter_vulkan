#pragma once

#include "assets/asset_types.h"

#include "common.h"
#include "core/arena.h"

#define MAX_ASSETS 2048

typedef enum {
	ASSET_TYPE_undefined,
	ASSET_TYPE_geometry,
	ASSET_TYPE_image,
	ASSET_TYPE_shader,
	ASSET_TYPE_MAX,
} AssetType;

typedef struct asset_entry {
	UUID id;
	String full_path;
	uint64_t last_modified;
} AssetEntry;

typedef struct asset_store {
	Arena *arena;
	ArenaTrie trie;

	String asset_directory;
	AssetEntry *assets[ASSET_TYPE_MAX];
	uint32_t asset_counts[ASSET_TYPE_MAX];
} AssetStore;

ENGINE_API AssetStore asset_store_make(Arena *arena);

ENGINE_API bool asset_store_track_directory(AssetStore *store, String directory);
ENGINE_API bool asset_store_track_file(AssetStore *store, String file_path);

ENGINE_API bool asset_store_serialize(AssetStore *store, String output);
ENGINE_API bool asset_store_deserialize(AssetStore *store, String src);

ENGINE_API UUID asset_store_register(AssetStore *store, AssetType type, String key);

ENGINE_API UUID asset_store_find(AssetStore *store, AssetType type, String key);
ENGINE_API UUID asset_store_find_shader(AssetStore *store, String key);
ENGINE_API UUID asset_store_find_model(AssetStore *store, String key);
ENGINE_API UUID asset_store_find_image(AssetStore *store, String key);

bool asset_store_clear(AssetStore *store);
