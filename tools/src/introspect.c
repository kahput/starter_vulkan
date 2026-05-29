#include "assets/asset_types.h"
#include "common.h"
#include "core/arena.h"
#include "core/debug.h"
#include "core/strings.h"
#include <core/logger.h>
#include <core/lexer.h>

#include <platform/filesystem.h>
#include <stdio.h>
#include <stdlib.h>

#define INDENT(level) (level * 4), ""

typedef enum {
	TYPE_MEMBER_FLAG_CONST = 1 << 0,
	TYPE_MEMBER_FLAG_POINTER = 1 << 1,
	TYPE_MEMBER_FLAG_ARRAY = 1 << 2,
} MetatypeMemberFlags;

const char *flags_to_string(MetatypeMemberFlags flags) {
	bool is_pointer = flags & TYPE_MEMBER_FLAG_POINTER;
	bool is_const = flags & TYPE_MEMBER_FLAG_CONST;
	bool is_array = flags & TYPE_MEMBER_FLAG_ARRAY;
	ASSERT_MESSAGE(!(is_pointer && is_array), "Can't be both pointer and fixed size array");
	return is_array && is_const	 ? "TYPE_MEMBER_FLAG_CONST | TYPE_MEMBER_FLAG_ARRAY"
		: is_pointer && is_const ? "TYPE_MEMBER_FLAG_CONST | TYPE_MEMBER_FLAG_POINTER"
		: is_const				 ? "TYPE_MEMBER_FLAG_CONST"
		: is_array				 ? "TYPE_MEMBER_FLAG_ARRAY"
		: is_pointer			 ? "TYPE_MEMBER_FLAG_POINTER"
								 : "0";
}

String emit_member(Arena *arena, String stream, Lexer *lexer, Token struct_type_name, String prefix, uint32_t *member_index, uint32_t indent_level) {
	Token type_name = { 0 }, field_name = { 0 };
	String array_size = { 0 };
	MetatypeMemberFlags flags = 0;
	uint32_t indirection = 0;

	String result = stream;
	while (true) {
		Token token = lexer_next(lexer);
		if (token.type == TOKEN_EOF)
			break;

		if (token.type == TOKEN_IDENTIFIER && type_name.type == 0)
			type_name = token;

		if (token.type == TOKEN_CONST)
			flags |= TYPE_MEMBER_FLAG_CONST;

		if (token.type == TOKEN_STAR) {
			flags |= TYPE_MEMBER_FLAG_POINTER;
			while (token.type == TOKEN_STAR) {
				indirection++;
				token = lexer_next(lexer);
			}
		}

		if (token.type == TOKEN_LEFT_BRACKET) { // array member
			flags |= TYPE_MEMBER_FLAG_ARRAY;

			token = lexer_next(lexer);
			while (token.type != TOKEN_EOF && token.type != TOKEN_RIGHT_BRACKET) {
				array_size = string_concat(arena, array_size, token.string);
				token = lexer_next(lexer);
			}
			token = lexer_next(lexer); // skip right bracket
		}

		if (token.type == TOKEN_COMMA || token.type == TOKEN_SEMICOLON) {
			if (prefix.length)
				field_name.string = string_format(arena, "%.*s.%.*s", SARG(prefix), SARG(field_name.string));

			result = string_concat(
				arena,
				result,
				string_format(arena, "%*s[%u] = {\n", INDENT(indent_level), *member_index, SARG(type_name.string)));
			*member_index += 1;

			indent_level++;

			// Type
			result = string_concat(
				arena,
				result,
				string_format(arena, "%*s.type = TYPE_%.*s,\n", INDENT(indent_level), SARG(type_name.string)));
			result = string_concat(
				arena,
				result,
				string_format(arena, "%*s.field_name = \"%.*s\",\n", INDENT(indent_level), SARG(field_name.string)));

			result = string_concat(
				arena,
				result,
				string_format(arena, "%*s.flags = %s,\n",
					INDENT(indent_level),
					flags_to_string(flags)));
			result = string_concat(
				arena,
				result,
				string_format(arena, "%*s.indirection = %u,\n",
					INDENT(indent_level),
					indirection));

			// offset & size
			result = string_concat(
				arena,
				result,
				string_format(arena, "%*s.offset = offsetof(%.*s, %.*s),\n", INDENT(indent_level), SARG(struct_type_name.string), SARG(field_name.string)));
			result = string_concat(
				arena,
				result,
				string_format(arena, "%*s.size = sizeof(((%.*s*)0)->%.*s),\n", INDENT(indent_level), SARG(struct_type_name.string), SARG(field_name.string)));

			result = string_concat(
				arena,
				result,
				string_format(arena, "%*s.stride = sizeof(%.*s),\n", INDENT(indent_level), SARG(type_name.string)));
			if (FLAG_GET(flags, TYPE_MEMBER_FLAG_ARRAY)) {
				result = string_concat(
					arena,
					result,
					string_format(arena, "%*s.count = %.*s,\n", INDENT(indent_level), SARG(array_size)));
			} else {
				result = string_concat(
					arena,
					result,
					string_format(arena, "%*s.count = 1,\n", INDENT(indent_level)));
			}

			indent_level--;
			result = string_concat(
				arena,
				result,
				string_format(arena, "%*s},\n", INDENT(indent_level), SARG(type_name.string)));

			flags = 0;
			array_size = (String){ 0 };
			indirection = 0;
			field_name = (Token){ 0 };

			if (token.type == TOKEN_SEMICOLON)
				break;
		}

		field_name = token;
	}

	return result;
}

void skip_block(Lexer *lexer) {
	while (lexer_match(lexer, TOKEN_RIGHT_BRACE, NULL))
		lexer_next(lexer);
}

void skip_statement(Lexer *lexer) {
	while (lexer_match(lexer, TOKEN_SEMICOLON, NULL))
		lexer_next(lexer);
}

uint32_t parse_structure(FILE *out_file, Lexer *lexer, bool type_defined) {
	Token identifier = { 0 };
	if (lexer_peek(lexer).type == TOKEN_IDENTIFIER)
		identifier = lexer_next(lexer);

	while (lexer_match(lexer, TOKEN_LEFT_BRACE, 0) == false)
		lexer_next(lexer);

	Lexer snapshot = *lexer;

	ArenaTemp scratch = arena_scratch_begin(NULL);

	uint32_t member_count = 0;
	while (lexer_peek(lexer).type != TOKEN_EOF && lexer_match(lexer, TOKEN_RIGHT_BRACE, NULL) == false) {
		Token token = lexer_next(lexer);

		// TODO: Anonymous struct
		if (token.type == TOKEN_TYPEDEF || token.type == TOKEN_STRUCT || token.type == TOKEN_UNION) {
			bool type_defined = false;
			if (token.type == TOKEN_TYPEDEF) {
				ASSERT_MESSAGE(
					lexer_peek(lexer).type == TOKEN_UNION || lexer_peek(lexer).type == TOKEN_STRUCT,
					"expected (struct | union) after typedef");
				lexer_next(lexer);
				type_defined = true;
			}

			member_count += parse_structure(out_file, lexer, type_defined);
			skip_statement(lexer);
			continue;
		}

		if (token.type == TOKEN_IDENTIFIER) {
			member_count++;
			while (true) {
				Token token = lexer_next(lexer);

				if (token.type == TOKEN_COMMA)
					member_count++;

				if (token.type == TOKEN_SEMICOLON || token.type == TOKEN_EOF)
					break;
			}
		}
		/* printf("%.*s (%s) - %u\n", SARG(token.string), token_type_names[token.type], token.line); */
	}

	if (type_defined)
		identifier = lexer_next(lexer);
	if (identifier.type == 0)
		return member_count;

	uint32_t indent_level = 2;
	String internals = string_format(scratch.arena,
		"%*s.type = TYPE_%.*s,\n"
		"%*s.name = \"%.*s\",\n"
		"%*s.alignment = alignof(%.*s),\n"
		"%*s.size = sizeof(%.*s),\n"
		"%*s.member_count = %u,\n"
		"%*s.members = (TypeMember[]) {\n",
		INDENT(indent_level),
		SARG(identifier.string),
		INDENT(indent_level),
		SARG(identifier.string),
		INDENT(indent_level),
		SARG(identifier.string),
		INDENT(indent_level),
		SARG(identifier.string),
		INDENT(indent_level),
		member_count,
		INDENT(indent_level));

	*lexer = snapshot;
	uint32_t index = 0;
	while (lexer_peek(lexer).type != TOKEN_EOF && lexer_match(lexer, TOKEN_RIGHT_BRACE, NULL) == false) {
		Token token = lexer_peek(lexer);

		// TODO: Handle anonymous structs properly
		if (token.type == TOKEN_STRUCT || token.type == TOKEN_UNION) {
			lexer_next(lexer);
			Token embedded_struct_name = lexer_peek(lexer).type == TOKEN_IDENTIFIER ? lexer_next(lexer) : (Token){ 0 };

			lexer_expect(lexer, TOKEN_LEFT_BRACE);

			Lexer snapshot = *lexer;
			while (lexer_match(lexer, TOKEN_RIGHT_BRACE, NULL) == false)
				lexer_next(lexer);

			Token field_name = lexer_expect(lexer, TOKEN_IDENTIFIER);
			*lexer = snapshot;
			while (lexer_match(lexer, TOKEN_RIGHT_BRACE, NULL) == false) {
				indent_level++;
				internals = emit_member(scratch.arena, internals, lexer, identifier, field_name.string, &index, indent_level);
				indent_level--;
			}

			if (lexer_peek(lexer).type == TOKEN_IDENTIFIER) {
				lexer_next(lexer); // skip the name
				lexer_expect(lexer, TOKEN_SEMICOLON); // skip semicolo
			}
			continue;
		} else if (token.type == TOKEN_CONST || token.type == TOKEN_IDENTIFIER) {
			indent_level++;
			internals = emit_member(scratch.arena, internals, lexer, identifier, S(""), &index, indent_level);
			indent_level--;
		} else
			lexer_next(lexer);

		/* printf("%.*s (%s) - %u\n", SARG(token.string), token_type_names[token.type], token.line); */
	}

	internals = string_concat(
		scratch.arena,
		internals,
		string_format(scratch.arena, "%*s},\n", INDENT(indent_level)));

	if (type_defined)
		identifier = lexer_next(lexer);

	if (identifier.type) {
		indent_level--;

		fprintf(out_file, "%*s[TYPE_%.*s] = {\n", INDENT(indent_level), SARG(identifier.string));
		fprintf(out_file, "%.*s", SARG(internals));

		fprintf(out_file, "%*s},\n", INDENT(indent_level));
	}

	arena_scratch_end(scratch);
	return member_count;
}

typedef struct meta_type MetaType;
struct meta_type {
	char *name;
	MetaType *next;
};

void enumerate_structure(ArenaTrie *trie, FILE *out_file, Lexer *lexer, bool type_defined) {
	Token identifier = { 0 };
	if (lexer_peek(lexer).type == TOKEN_IDENTIFIER)
		identifier = lexer_next(lexer);

	while (lexer_match(lexer, TOKEN_LEFT_BRACE, 0) == false)
		lexer_next(lexer);

	Token close_brace = { 0 };
	while (lexer_peek(lexer).type != TOKEN_EOF && lexer_match(lexer, TOKEN_RIGHT_BRACE, &close_brace) == false) {
		Token token = lexer_next(lexer);

		// another structure
		if (token.type == TOKEN_STRUCT || token.type == TOKEN_UNION) {
			// skip
			while (token.type != TOKEN_RIGHT_BRACE)
				token = lexer_next(lexer);
			lexer_next(lexer); // skip name
			lexer_next(lexer); // skip semicolon
		}

		if (token.type == TOKEN_IDENTIFIER) {
			if (arena_trieset_find(trie, buffer_wrap_string(token.string)) == 0) {
				fprintf(out_file, "    TYPE_%.*s,\n", SARG(token.string));
				arena_trieset_push(trie, buffer_wrap_string(token.string));
			}

			while (true) {
				Token token = lexer_next(lexer);
				if (token.type == TOKEN_SEMICOLON || token.type == TOKEN_EOF)
					break;
			}
		}
		/* printf("%.*s (%s) - %u\n", SARG(token.string), token_type_names[token.type], token.line); */
	}

	if (type_defined)
		identifier = lexer_next(lexer);

	if (identifier.type && arena_trieset_find(trie, buffer_wrap_string(identifier.string)) == 0) {
		fprintf(out_file, "    TYPE_%.*s,\n", SARG(identifier.string));
		arena_trieset_push(trie, buffer_wrap_string(identifier.string));
	}
}

int main(int32_t argc, char *argv[]) {
#if 0
	if (argc < 4) {
		fprintf(stderr, "Usage: %s <out.h> <out.c> [input_files...]\n", argv[0]);
		return -1;
	}

	ArenaTemp scratch = arena_scratch_begin(NULL);
	FILE *out_h = fopen(argv[1], "w");
	FILE *out_c = fopen(argv[2], "w");

	if (out_h == 0 || out_c == 0) {
		fprintf(stderr, "Failed to open output files.\n");
		return -1;
	}

	fprintf(out_h, "#pragma once\n\n");
	fprintf(out_h, "#include \"meta.h\"\n\n");
	fprintf(out_h, "enum {\n");

	fprintf(out_c, "#include \"meta_generated.h\"\n");
	fprintf(out_c, "#include \"components.h\"\n\n");
	fprintf(out_c, "Type type_introspection[TYPE_MAX] = {\n");

	ArenaTrie trie = arena_trie_make(scratch.arena);
	String *aliases = NULL;

	for (int32_t index = 3; index < argc; ++index) {
		const char *path = argv[index];
#else
	ArenaTemp scratch = arena_scratch_begin(NULL);
	FILE *out_h = fopen("src/meta_generated.h", "w");
	FILE *out_c = fopen("src/meta_generated.c", "w");

	if (out_h == 0 || out_c == 0) {
		fprintf(stderr, "Failed to open output files.\n");
		return -1;
	}

	fprintf(out_h, "#pragma once\n\n");
	fprintf(out_h, "#include \"meta.h\"\n\n");
	fprintf(out_h, "enum {\n");

	fprintf(out_c, "#include \"meta_generated.h\"\n");
	fprintf(out_c, "#include \"components.h\"\n\n");
	fprintf(out_c, "Type type_introspection[TYPE_MAX] = {\n");

	ArenaTrie trie = arena_trie_make(scratch.arena);
	String *aliases = NULL;

	const char *paths[] = {
		"../engine/src/common.h",
		"../engine/src/core/cmath.h",
		"src/components.h",
	};

	for (uint32_t index = 0; index < countof(paths); ++index) {
		const char *path = paths[index];
#endif
		FILE *file = fopen(path, "rb");
		if (file == NULL) {
			fprintf(stderr, "failed to open %s, skipping\n", path);
			continue;
		}

		fseek(file, 0, SEEK_END);
		uint64_t size = ftell(file);
		fseek(file, 0, SEEK_SET);

		char *buffer = (char *)arena_push_size(scratch.arena, size + 1);
		fread(buffer, sizeof(*buffer), size, file);
		buffer[size] = '\0';
		fclose(file);

		Lexer lexer = lexer_make(string_wrap(buffer));

		Token last = { 0 };
		Token token = { 0 };
		while ((token = lexer_next(&lexer)).type != TOKEN_EOF) {
			if (token.type == TOKEN_TYPEDEF) {
				if (lexer_peek(&lexer).type != TOKEN_STRUCT && lexer_peek(&lexer).type != TOKEN_UNION) {
					Token base_type = lexer_next(&lexer);
					Token alias_type = lexer_next(&lexer);

					if (arena_trieset_find(&trie, buffer_wrap_string(base_type.string)) == 0) {
						fprintf(out_h, "    TYPE_%.*s,\n", SARG(base_type.string));
						arena_trieset_push(&trie, buffer_wrap_string(base_type.string));
					}

					arena_darray_put(scratch.arena, aliases, String,
						string_format(scratch.arena, "#define TYPE_%.*s TYPE_%.*s\n", SARG(alias_type.string), SARG(base_type.string)));
					arena_trieset_push(&trie, buffer_wrap_string(alias_type.string));
				}
			}

			if (token.type == TOKEN_STRUCT || token.type == TOKEN_UNION)
				enumerate_structure(&trie, out_h, &lexer, last.type == TOKEN_TYPEDEF);
			last = token;
		}

		lexer = lexer_make(string_wrap(buffer));
		last = (Token){ 0 };
		token = (Token){ 0 };
		while ((token = lexer_next(&lexer)).type != TOKEN_EOF) {
			if (token.type == TOKEN_STRUCT || token.type == TOKEN_UNION)
				parse_structure(out_c, &lexer, last.type == TOKEN_TYPEDEF);

			last = token;
		}

		/* fclose(file); */
	}
	fprintf(out_h, "\n    TYPE_MAX,\n");
	fprintf(out_h, "};\n\n");

	for (uint32_t index = 0; index < arena_array_count(aliases); ++index)
		fprintf(out_h, "%.*s", SARG(aliases[index]));
	fprintf(out_h, "\n");

	fprintf(out_h, "extern Type type_introspection[TYPE_MAX];\n\n");
	fprintf(out_h, "#define type_info(T) type_introspection[TYPE_##T]");

	fprintf(out_c, "};\n");

	arena_scratch_end(scratch);

	return 0;
}
