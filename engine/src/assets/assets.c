#include "assets.h"
#include "assets/asset_types.h"
#include "assets/importer.h"

#include "core/arena.h"

#include "common.h"
#include "core/astring.h"
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

bool asset_tracker_track_directory(AssetTracker *tracker, String directory) {
	ArenaTemp scratch = arena_scratch(NULL);

	StringList file_list = filesystem_list_files(scratch.arena, directory, true);
	StringNode *file = file_list.first;
	uint32_t count = 0;

	logger_indent();
	while (file) {
		asset_tracker_track_file(tracker, file->string);
		count++;

		file = file->next;
	}
	logger_dedent();

	LOG_INFO("AssetTracker: %d files tracked", count);
	arena_scratch_release(scratch);
	return true;
}

bool asset_tracker_track_file(AssetTracker *tracker, String file_path) {
	ArenaTemp scratch = arena_scratch(NULL);
	String name = string_push_copy(scratch.arena, string_path_filename(file_path));
	AssetEntry *entry = arena_trie_push(&tracker->trie, string_hash64(name), AssetEntry);

	if (entry->full_path.length == 0) {
		entry->full_path = string_push_copy(tracker->arena, file_path);
		entry->type = file_extension_to_asset_type(string_path_extension(file_path));
		entry->last_modified = filesystem_last_modified(file_path);

		tracker->tracked_file_count++;

		arena_scratch_release(scratch);
		return true;
	}

	arena_scratch_release(scratch);
	return false;
}

UUID asset_tracker_request_shader(AssetTracker *tracker, String key) {
	ArenaTemp scratch = arena_scratch(NULL);

	String vertex_shader_key = string_push_replace(scratch.arena, key, S("glsl"), S("vert.spv"));
	String fragment_shader_key = string_push_replace(scratch.arena, key, S("glsl"), S("frag.spv"));

	AssetEntry *vs_entry = arena_trie_find(&tracker->trie, string_hash64(vertex_shader_key), AssetEntry);
	AssetEntry *fs_entry = arena_trie_find(&tracker->trie, string_hash64(fragment_shader_key), AssetEntry);
	if (vs_entry == NULL || fs_entry == NULL) {
		LOG_WARN("Assets: Key '%.*s' is not tracked", SARG(key));
		arena_scratch_release(scratch);
		return 0;
	}

	if (vs_entry->type != ASSET_TYPE_SHADER || fs_entry->type != ASSET_TYPE_SHADER) {
		LOG_ERROR("Assets: Key '%.*s' is not a shader", SARG(key));
		arena_scratch_release(scratch);
		return 0;
	}

	arena_scratch_release(scratch);

	return vs_entry->id;
}

UUID asset_tracker_request_model(AssetTracker *tracker, String key) {
	AssetEntry *entry = arena_trie_find(&tracker->trie, string_hash64(key), AssetEntry);
	if (entry == NULL)
		return 0;

	if (entry->type != ASSET_TYPE_GEOMETRY) {
		LOG_ERROR("AssetTracker: Requested asset '%.*s' is type %d", SARG(key), entry->type);
		return 0;
	}

	return entry->id;
}

UUID asset_tracker_request_image(AssetTracker *tracker, String key) {
	AssetEntry *entry = arena_trie_find(&tracker->trie, string_hash64(key), AssetEntry);
	if (entry == NULL) {
		LOG_WARN("AssetTracker: No image '%.*s'", SARG(key));
		return 0;
	}

	if (entry->type != ASSET_TYPE_IMAGE) {
		LOG_ERROR("AssetTracker: Requested asset '%.*s' is type %d", SARG(key), entry->type);
		return 0;
	}

	return entry->id;
}

bool asset_tracker_clear_cache(AssetTracker *tracker) {
	arena_clear(tracker->arena);
	LOG_INFO("AssetTracker: Cache cleared. All tracking lost.");

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
