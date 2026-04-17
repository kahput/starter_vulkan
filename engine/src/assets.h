#pragma once

#include "assets/asset_types.h"

#include "common.h"
#include "core/arena.h"

typedef struct asset_entry {
	struct asset_entry *children[4];
	UUID id;

	String full_path;
	AssetType type;
	uint64_t last_modified;
} AssetEntry;

typedef struct asset_library {
	Arena *arena;
	ArenaTrie trie;

	uint32_t tracked_file_count;
} AssetLibrary;

static inline AssetLibrary asset_library_make(Arena *arena) { return (AssetLibrary){ .arena = arena, .trie = arena_trie_make(arena) }; }

ENGINE_API bool asset_library_track_directory(AssetLibrary *tracker, String directory);
ENGINE_API bool asset_library_track_file(AssetLibrary *tracker, String file_path);

ENGINE_API UUID asset_library_request_shader(AssetLibrary *tracker, String key);
ENGINE_API UUID asset_library_request_mesh(AssetLibrary *tracker, String key);
ENGINE_API UUID asset_library_request_image(AssetLibrary *tracker, String key);

bool asset_library_clear(AssetLibrary *tracker);
