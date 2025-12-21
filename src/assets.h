#pragma once

#include "assets/asset_types.h"

#include "common.h"
#include "core/hash_trie.h"

#include "allocators/arena.h"

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

bool asset_library_startup(void *memory, size_t size);

bool asset_library_track_directory(String directory);
bool asset_library_track_file(String file_path);

UUID asset_library_model_mesh_id(String key, uint32_t index);

UUID asset_library_load_shader(Arena *arena, String key, ShaderSource **out_shader);
UUID asset_library_load_model(Arena *arena, String key, ModelSource **out_model, bool use_cached_textures);
UUID asset_library_load_image(Arena *arena, String key, Image **out_texture);

UUID asset_library_request_shader(String key, ShaderSource **out_shader);
UUID asset_library_request_model(String key, ModelSource **out_model);
UUID asset_library_request_image(String key, Image **out_image);

bool asset_library_clear_cache(void);
