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
} AssetTracker;

static inline AssetTracker asset_tracker_make(Arena *arena) { return (AssetTracker){ .arena = arena, .trie = arena_trie_make(arena) }; }

ENGINE_API bool asset_tracker_track_directory(AssetTracker *tracker, String directory);
ENGINE_API bool asset_tracker_track_file(AssetTracker *tracker, String file_path);

ENGINE_API UUID asset_tracker_request_shader(AssetTracker *tracker, String key);
ENGINE_API UUID asset_tracker_request_model(AssetTracker *tracker, String key);
ENGINE_API UUID asset_tracker_request_image(AssetTracker *tracker, String key);

bool asset_tracker_clear(AssetTracker *tracker);
