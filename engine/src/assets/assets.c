#include "assets.h"
#include "assets/asset_types.h"
#include "assets/importer.h"

#include "core/arena.h"

#include "common.h"
#include "core/debug.h"
#include "core/strings.h"
#include "core/identifiers.h"
#include "core/logger.h"

#include "platform/filesystem.h"

#include <stdalign.h>

static const char *extensions[ASSET_TYPE_COUNT][8] = {
	[ASSET_TYPE_UNDEFINED] = { NULL },
	[ASSET_TYPE_GEOMETRY] = { "glb", "gltf", NULL },
	[ASSET_TYPE_IMAGE] = { "png", "jpeg", "jpg", NULL },
	[ASSET_TYPE_SHADER] = { "glsl", "spv" },
};

static AssetType file_extension_to_asset_type(String extension);

bool asset_store_track_directory(AssetStore *store, String directory) {
	ArenaTemp scratch = arena_scratch_begin(store->arena);

	StringList file_list = filesystem_directory_files(scratch.arena, directory, true);
	StringNode *file = file_list.first;
	uint32_t count = 0;

	logger_indent();
	while (file) {
		asset_store_track_file(store, file->string);
		count++;

		file = file->next;
	}
	logger_dedent();

	LOG_INFO("AssetStore: %d files tracked", count);
	arena_scratch_end(scratch);
	return true;
}

bool asset_store_track_file(AssetStore *store, String file_path) {
	ArenaTemp scratch = arena_scratch_begin(store->arena);
	String name = string_copy(scratch.arena, stringpath_filename(file_path));
	AssetEntry *entry = arena_trie_push(&store->trie, buffer_wrap_string(name), AssetEntry);

	if (entry->full_path.length == 0) {
		entry->full_path = string_copy(store->arena, file_path);
		entry->type = file_extension_to_asset_type(stringpath_extension(file_path));
		entry->last_modified = filesystem_last_modified(file_path);
		entry->id = string_hash64(entry->full_path);

		store->tracked_file_count++;

		arena_scratch_end(scratch);
		return true;
	}

	arena_scratch_end(scratch);
	return false;
}

UUID asset_store_find(AssetStore *store, AssetType type, String key) {
	switch (type) {
		case ASSET_TYPE_GEOMETRY:
			return asset_store_find_model(store, key);
		case ASSET_TYPE_IMAGE:
			return asset_store_find_image(store, key);
		case ASSET_TYPE_SHADER:
			return asset_store_find_shader(store, key);

		case ASSET_TYPE_UNDEFINED:
		case ASSET_TYPE_COUNT:
			return 0;
	};

	ASSERT(false);
}

UUID asset_store_find_shader(AssetStore *store, String key) {
	ArenaTemp scratch = arena_scratch_begin(store->arena);

	String vertex_shader_key = string_replace(scratch.arena, key, S("glsl"), S("vert.spv"));
	String fragment_shader_key = string_replace(scratch.arena, key, S("glsl"), S("frag.spv"));

	AssetEntry *vs_entry = arena_trie_find(&store->trie, buffer_wrap_string(vertex_shader_key), AssetEntry);
	AssetEntry *fs_entry = arena_trie_find(&store->trie, buffer_wrap_string(fragment_shader_key), AssetEntry);
	if (vs_entry == NULL || fs_entry == NULL) {
		LOG_WARN("Assets: Key '%.*s' is not tracked", SARG(key));
		arena_scratch_end(scratch);
		return 0;
	}

	if (vs_entry->type != ASSET_TYPE_SHADER || fs_entry->type != ASSET_TYPE_SHADER) {
		LOG_ERROR("Assets: Key '%.*s' is not a shader", SARG(key));
		arena_scratch_end(scratch);
		return 0;
	}

	arena_scratch_end(scratch);

	return vs_entry->id;
}

UUID asset_store_find_model(AssetStore *store, String key) {
	AssetEntry *entry = arena_trie_find(&store->trie, buffer_wrap_string(key), AssetEntry);
	if (entry == NULL)
		return 0;

	if (entry->type != ASSET_TYPE_GEOMETRY) {
		LOG_ERROR("AssetStore: finded asset '%.*s' is type %d", SARG(key), entry->type);
		return 0;
	}

	return entry->id;
}

UUID asset_store_find_image(AssetStore *store, String key) {
	AssetEntry *entry = arena_trie_find(&store->trie, buffer_wrap_string(key), AssetEntry);
	if (entry == NULL) {
		LOG_WARN("AssetStore: No image '%.*s'", SARG(key));
		return 0;
	}

	if (entry->type != ASSET_TYPE_IMAGE) {
		LOG_ERROR("AssetStore: finded asset '%.*s' is type %d", SARG(key), entry->type);
		return 0;
	}

	return entry->id;
}

bool asset_store_clear_cache(AssetStore *store) {
	arena_reset(store->arena);
	LOG_INFO("AssetStore: Cache cleared. All tracking lost.");

	return true;
}

AssetType file_extension_to_asset_type(String extension) {
	for (uint32_t asset_index = 0; asset_index < ASSET_TYPE_COUNT; ++asset_index) {
		uint32_t index = 0;
		const char *ext = extensions[asset_index][index];
		while (ext) {
			if (string_equals(extension, string_wrap(ext)))
				return asset_index;

			ext = extensions[asset_index][++index];
		}
	}

	return ASSET_TYPE_UNDEFINED;
}
