#include "assets.h"
#include "assets/asset_types.h"
#include "assets/importer.h"

#include "assets/json_parser.h"
#include "core/arena.h"

#include "common.h"
#include "core/debug.h"
#include "core/strings.h"
#include "core/identifiers.h"
#include "core/logger.h"

#include "platform/filesystem.h"

#include <stdalign.h>

static const char *extensions[ASSET_TYPE_MAX][8] = {
	[ASSET_TYPE_undefined] = { NULL },
	[ASSET_TYPE_geometry] = { "glb", "gltf", NULL },
	[ASSET_TYPE_image] = { "png", "jpeg", "jpg", NULL },
	[ASSET_TYPE_shader] = { "glsl", "spv" },
};

static AssetType file_extension_to_asset_type(String extension);
static String asset_type_to_string(AssetType type);
static AssetType asset_type_from_string(String type);

bool asset_store_track_directory(AssetStore *store, String directory) {
	ArenaTemp scratch = arena_scratch_begin(store->arena);

	ASSERT(store->asset_directory.length == 0);
	store->asset_directory = string_copy(store->arena, directory);
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

AssetStore asset_store_make(Arena *arena) {
	AssetStore result = { .arena = arena, .trie = arena_trie_make(arena) };
	for (uint32_t type = 0; type < ASSET_TYPE_MAX; ++type)
		result.assets[type] = arena_push_count(arena, MAX_ASSETS, AssetEntry);

	return result;
}

static inline String asset_path_key(AssetStore *store, String path, bool include) {
	ASSERT(store->asset_directory.length);
	int32_t directory_index = string_find_first(path, store->asset_directory);
	ASSERT(directory_index != -1);
	uint32_t slice_start = include ? (uint32_t)directory_index : directory_index + store->asset_directory.length + 1;
	uint32_t slice_end = path.length - slice_start;
	return string_slice(path, slice_start, slice_end);
}

static inline AssetEntry *asset_map_push(AssetStore *store, AssetType type, String key) {
	AssetEntry *entry = store->assets[type] + store->asset_counts[type]++;

	ArenaTrieNode *node = arena_trienode_push(&store->trie, buffer_wrap_string(key));
	ASSERT(node->payload == NULL);
	node->payload = entry;

	return entry;
}

bool asset_store_track_file(AssetStore *store, String file_path) {
	ArenaTemp scratch = arena_scratch_begin(store->arena);
	String name = string_copy(scratch.arena, stringpath_filename(file_path));

	AssetType type = file_extension_to_asset_type(stringpath_extension(file_path));

	AssetEntry *entry = asset_map_push(store, type, asset_path_key(store, file_path, true));
	/* arena_trienode_push(&store->trie, buffer_wrap_string(asset_path_key(store, file_path, false)))->payload = entry; */

	if (entry->full_path.length == 0) {
		entry->full_path = string_copy(store->arena, file_path);
		entry->last_modified = filesystem_last_modified(file_path);
		entry->id = uuid_generate();

		arena_scratch_end(scratch);
		return true;
	}

	arena_scratch_end(scratch);
	return false;
}

bool asset_store_serialize(AssetStore *store, String output) {
	ArenaTemp scratch = arena_scratch_begin(store->arena);
	JsonExporter exporter[] = { json_exporter_make(scratch.arena) };
	json_begin_map(exporter, S(""));
	json_begin_array(exporter, S("assets"));

	for (uint32_t type = 0; type < ASSET_TYPE_MAX; ++type) {
		AssetEntry *entires = store->assets[type];
		uint32_t count = store->asset_counts[type];

		for (uint32_t entry_index = 0; entry_index < count; ++entry_index) {
			AssetEntry *entry = entires + entry_index;

			json_begin_map(exporter, S(""));

			json_write_pair(exporter, S("id"), uint64_t, entry->id);
			if (string_find_first(entry->full_path, S("in_memory")) != -1)
				json_write_pair(exporter, S("name"), String, entry->full_path);
			else
				json_write_pair(exporter, S("path"), String, entry->full_path);
			json_write_pair(exporter, S("type"), String, asset_type_to_string(type));

			json_end_map(exporter);
		}
	}

	json_end_array(exporter);
	json_end_map(exporter);

	File file = filesystem_open(output, FILE_MODE_WRITE);

	file_write(&file, 1, exporter->arena->offset - exporter->start_offset, (uint8_t *)exporter->arena->base + exporter->start_offset);
	file_close(&file);

	arena_scratch_end(scratch);

	return true;
}

UUID asset_store_register(AssetStore *store, AssetType type, String key) {
	ArenaTemp scratch = arena_scratch_begin(store->arena);
	String k = string_format(scratch.arena, "in_memory:%s", string_cstring(scratch.arena, key));
	AssetEntry *entry = arena_trie_find(&store->trie, buffer_wrap_string(k), AssetEntry);
	if (entry) {
		arena_scratch_end(scratch);
		return entry->id;
	}

	entry = asset_map_push(store, type, key);
	entry->full_path = string_format(store->arena, "in_memory:%s", string_cstring(scratch.arena, key));
	entry->id = uuid_generate();
	entry->last_modified = 0;
	arena_scratch_end(scratch);

	return entry->id;
}

bool asset_store_deserialize(AssetStore *store, String src) {
	ArenaTemp scratch = arena_scratch_begin(store->arena);
	JsonNode *root = json_parse(scratch.arena, string_wrap_buffer(filesystem_read(scratch.arena, src)));

	store->asset_directory = string_copy(store->arena, stringpath_directory(src));
	for (JsonNode *node = json_list(root, S("assets")); node; node = node->next) {
		AssetType type = asset_type_from_string(json_find(node, S("type"), String));

		String path = json_find(node, S("path"), String);
		String name = json_find(node, S("name"), String);

		AssetEntry *entry = NULL;
		if (path.length) {
			entry = asset_map_push(store, type, asset_path_key(store, path, true));
			entry->full_path = string_copy(store->arena, path);
			/* arena_trienode_push(&store->trie, buffer_wrap_string(asset_path_key(store, path, false)))->payload = entry; */
		} else if (name.length) {
			entry = asset_map_push(store, type, name);
			entry->full_path = string_copy(store->arena, name);
		} else
			continue;

		entry->id = json_find(node, S("id"), uint64_t);
		entry->last_modified = filesystem_last_modified(entry->full_path);

		/* 		String name = string_copy(scratch.arena, stringpath_filename(entry->full_path)); */
		/* 		arena_trienode_push(&store->trie, buffer_wrap_string(name))->payload = entry; */
	}

	arena_scratch_end(scratch);
	return false;
}

UUID asset_store_find(AssetStore *store, AssetType type, String key) {
	switch (type) {
		case ASSET_TYPE_geometry:
			return asset_store_find_model(store, key);
		case ASSET_TYPE_image:
			return asset_store_find_image(store, key);
		case ASSET_TYPE_shader:
			return asset_store_find_shader(store, key);

		case ASSET_TYPE_undefined:
		case ASSET_TYPE_MAX:
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

	/* if (vs_entry->type != ASSET_TYPE_shader || fs_entry->type != ASSET_TYPE_shader) { */
	/* 	LOG_ERROR("Assets: Key '%.*s' is not a shader", SARG(key)); */
	/* 	arena_scratch_end(scratch); */
	/* 	return 0; */
	/* } */

	arena_scratch_end(scratch);

	return vs_entry->id;
}

UUID asset_store_find_model(AssetStore *store, String key) {
	AssetEntry *entry = arena_trie_find(&store->trie, buffer_wrap_string(key), AssetEntry);
	if (entry == NULL)
		return 0;

	/* if (entry->type != ASSET_TYPE_geometry) { */
	/* 	LOG_ERROR("AssetStore: finded asset '%.*s' is type %d", SARG(key), entry->type); */
	/* 	return 0; */
	/* } */

	return entry->id;
}

UUID asset_store_find_image(AssetStore *store, String key) {
	AssetEntry *entry = arena_trie_find(&store->trie, buffer_wrap_string(key), AssetEntry);
	if (entry == NULL) {
		LOG_WARN("AssetStore: No image '%.*s'", SARG(key));
		return 0;
	}

	/* if (entry->type != ASSET_TYPE_image) { */
	/* 	LOG_ERROR("AssetStore: finded asset '%.*s' is type %d", SARG(key), entry->type); */
	/* 	return 0; */
	/* } */

	return entry->id;
}

bool asset_store_clear_cache(AssetStore *store) {
	arena_reset(store->arena);
	LOG_INFO("AssetStore: Cache cleared. All tracking lost.");

	return true;
}

AssetType file_extension_to_asset_type(String extension) {
	for (uint32_t asset_index = 0; asset_index < ASSET_TYPE_MAX; ++asset_index) {
		uint32_t index = 0;
		const char *ext = extensions[asset_index][index];
		while (ext) {
			if (string_equals(extension, string_wrap(ext)))
				return asset_index;

			ext = extensions[asset_index][++index];
		}
	}

	return ASSET_TYPE_undefined;
}

String asset_type_to_string(AssetType type) {
	switch (type) {
		case ASSET_TYPE_geometry:
			return S("geometry");
		case ASSET_TYPE_image:
			return S("image");
		case ASSET_TYPE_shader:
			return S("shader");
		case ASSET_TYPE_MAX:
		case ASSET_TYPE_undefined:
			return S("<invalid>");
	}

	return S("<invalid>");
}

static AssetType asset_type_from_string(String type) {
	if (string_equals(S("geometry"), type))
		return ASSET_TYPE_geometry;
	else if (string_equals(S("image"), type))
		return ASSET_TYPE_image;
	else if (string_equals(S("shader"), type))
		return ASSET_TYPE_shader;
	else
		return ASSET_TYPE_undefined;
}
