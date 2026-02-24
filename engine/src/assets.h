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
	AssetEntry *root;

	uint32_t tracked_file_count;
} AssetLibrary;

bool asset_library_startup(AssetLibrary *library, void *memory, size_t size);

bool asset_library_track_directory(AssetLibrary *library, String directory);
bool asset_library_track_file(AssetLibrary *library, String file_path);

ENGINE_API UUID asset_library_load_shader(Arena *arena, AssetLibrary *library, String key, ShaderSource *out_shader);
ENGINE_API UUID asset_library_load_model(Arena *arena, AssetLibrary *library, String key, SModel *out_model);
ENGINE_API UUID asset_library_load_image(Arena *arena, AssetLibrary *library, String key, ImageSource *out_texture);

ENGINE_API UUID asset_library_request_shader(AssetLibrary *library, String key);
ENGINE_API UUID asset_library_request_model(AssetLibrary *library, String key);
ENGINE_API UUID asset_library_request_image(AssetLibrary *library, String key);

bool asset_library_clear_cache(AssetLibrary *library);
