#include "input.h"
#include "input/input_types.h"
#include "platform.h"
#include "platform/filesystem.h"
#include "scene.h"

#include <game_interface.h>

#include <common.h>
#include <core/cmath.h>
#include <core/arena.h>
#include <core/debug.h>
#include <core/logger.h>
#include <core/astring.h>

#include <renderer.h>
#include <renderer/r_internal.h>
#include <renderer/backend/vulkan_api.h>

#include <assets.h>
#include <assets/mesh_source.h>
#include <assets/asset_types.h>

#include <math.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

typedef struct {
	float mousex, mousey;
	uint32_t mouse_down;

	uint32_t hot, active;
} UIState;

typedef struct {
	Arena arena;

	bool is_initialized;
} GameState;

static GameState *state = NULL;

typedef struct Entry {
	ArenaTrieNode node;
	struct Entry *next;

	uint32_t edge_count;
	float probability;
} ChainEntry;

bool is_digit(char c) {
	return c >= '0' && c <= '9';
}

bool is_aplha(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

String peek_next_token(String source) {
	uint32_t start = 0;
	uint32_t current = 0;

	while (current < source.length) {
		start = current;
		uint32_t c = source.memory[current++];

		if (is_aplha(c)) {
			uint32_t next = 0;
			while (is_aplha(next = source.memory[current]) || next == '\'') {
				current++;
			}

			return (String){ .memory = &source.memory[start], .length = current - start };
		}

		if (is_digit(c)) {
			uint32_t next = 0;
			while (is_digit(next = source.memory[current])) {
				current++;
			}

			String token = { .memory = &source.memory[start], .length = current - start };
			return (String){ .memory = &source.memory[start], .length = current - start };
		}

		switch (c) {
			case '"': {
				uint32_t next = 0;
				while ((next = source.memory[current++]) != '"') {}

				String token = { .memory = &source.memory[start], .length = current - start };
				return (String){ .memory = &source.memory[start], .length = current - start };
			} break;

			case ',':
			case '!':
			case '.':
			case ';':
			case ':': {
				return (String){ .memory = &source.memory[start], .length = current - start };
			} break;

			case ' ':
			case '\r':
			case '\n':
			case '\t':
				continue;
		}
	}

	return (String){ 0 };
}

ArenaTrie parse(Arena *arena, String source) {
	ArenaTrie trie = arena_trie_make(arena);

	uint32_t start = 0;
	uint32_t current = 0;

	while (current < source.length) {
		start = current;
		uint32_t c = source.memory[current++];

		if (is_aplha(c)) {
			uint32_t next = 0;
			while (is_aplha(next = source.memory[current]) || next == '\'') {
				current++;
			}

			String token = { .memory = &source.memory[start], .length = current - start };
			StringList *list = arena_trie_push(&trie, string_hash64(token), StringList);
			stringlist_push(arena, list, peek_next_token((String){ .memory = token.memory + token.length, .length = source.length }));
		}

		if (is_digit(c)) {
			uint32_t next = 0;
			while (is_digit(next = source.memory[current])) {
				current++;
			}

			String token = { .memory = &source.memory[start], .length = current - start };
			StringList *list = arena_trie_push(&trie, string_hash64(token), StringList);
			stringlist_push(arena, list, peek_next_token((String){ .memory = token.memory + token.length, .length = source.length }));
		}

		switch (c) {
			case '"': {
				uint32_t next = 0;
				while ((next = source.memory[current++]) != '"') {}

				String token = { .memory = &source.memory[start], .length = current - start };
				StringList *list = arena_trie_push(&trie, string_hash64(token), StringList);
				stringlist_push(arena, list, peek_next_token((String){ .memory = token.memory + token.length, .length = source.length }));
			} break;

			case ',':
			case '!':
			case '.':
			case ';':
			case ':':

			{
				String token = { .memory = &source.memory[start], .length = current - start };
				StringList *list = arena_trie_push(&trie, string_hash64(token), StringList);
				stringlist_push(arena, list, peek_next_token((String){ .memory = token.memory + token.length, .length = source.length }));
			} break;
			case ' ':
			case '\r':
			case '\n':
			case '\t':
				continue;
		}
	}

	return trie;
}

FrameInfo game_on_update_and_render(GameContext *context, float dt) {
	state = (GameState *)context->permanent_memory;

	ArenaTemp scratch = arena_scratch(NULL);
	if (state->is_initialized == false) {
		state->arena = (Arena){
			.memory = state + 1,
			.offset = 0,
			.capacity = context->permanent_memory_size - sizeof(GameState)
		};

		String source = filesystem_read(scratch.arena, slit("input.txt"));

		ArenaTrie parsed = parse(scratch.arena, source);
		StringList *list = arena_trie_find(&parsed, shash("First"), StringList);

		srand(platform_time_absolute());

		String current = slit("the");
		StringList generated = { 0 };
		stringlist_push(scratch.arena, &generated, current);

		for (uint32_t index = 0; index < 32; ++index) {
			StringList *options = arena_trie_find(&parsed, string_hash64(current), StringList);
			if (options == NULL)
				break;
			StringNode *selected = options->first;
			uint32_t selected_index = rand() % options->count;

			uint32_t current_index = 0;
			while (selected && selected_index != current_index) {
				current_index++;
				selected = selected->next;
			}

			stringlist_push(scratch.arena, &generated, selected->string);
			current = selected->string;
		}

		LOG_INFO(sfmt, sarg(stringlist_join(scratch.arena, &generated, slit(" "))));

		state->is_initialized = true;
	}

	arena_scratch_release(scratch);
	return (FrameInfo){ 0 };
}

static GameInterface interface;
GameInterface *game_hookup(void) {
	interface = (GameInterface){
		.on_update = game_on_update_and_render,
	};
	return &interface;
}
