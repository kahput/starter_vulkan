#include "common.h"
#include "core/arena.h"
#include "core/strings.h"
#include <core/logger.h>
#include <core/lexer.h>

#include <platform/filesystem.h>
#include <stdio.h>
#include <stdlib.h>

void parse_structures(Lexer *lexer, bool type_defined) {
	Token identifier = { 0 };
	if (lexer_peek(lexer).type == TOKEN_IDENTIFIER)
		identifier = lexer_next(lexer);

	Lexer snapshot = *lexer;
	Token open_brace = lexer_expect(lexer, TOKEN_LEFT_BRACE);

	ArenaTemp scratch = arena_scratch_begin(NULL);

	Token close_brace = { 0 };
	uint32_t member_count = 0;
	while (lexer_peek(lexer).type != TOKEN_EOF && lexer_match(lexer, TOKEN_RIGHT_BRACE, &close_brace) == false) {
		Token token = lexer_next(lexer);

		// TODO: Anonymous struct
		if (token.type == TOKEN_STRUCT || token.type == TOKEN_UNION) {
			while (lexer_match(lexer, TOKEN_RIGHT_BRACE, 0) == false)
				lexer_next(lexer);

			continue;
		}

		if (token.type == TOKEN_IDENTIFIER) {
			member_count++;
			while (true) {
				Token token = lexer_next(lexer);
				if (token.type == TOKEN_SEMICOLON || token.type == TOKEN_EOF)
					break;
			}
		}
		/* printf("%.*s (%s) - %u\n", SARG(token.string), token_type_names[token.type], token.line); */
	}

	char indent_buffer[32];

	uint32_t indent = 2;
	memory_set(indent_buffer, ' ', sizeof(indent_buffer));
	indent_buffer[MIN(indent, 15) * 4] = '\0';

	if (type_defined)
		identifier = lexer_next(lexer);
	String internals = string_format(scratch.arena,
		"%s.type = METATYPE_%.*s,\n"
		"%s.name = \"%.*s\",\n"
		"%s.alignment = alignof(%.*s),\n"
		"%s.size = sizeof(%.*s),\n"
		"%s.member_count = %u,\n"
		"%s.members = (TypeMember[]) {\n",
		indent_buffer,
		SARG(identifier.string),
		indent_buffer,
		SARG(identifier.string),
		indent_buffer,
		SARG(identifier.string),
		indent_buffer,
		SARG(identifier.string),
		indent_buffer,
		member_count,
		indent_buffer);

	indent++;
	memory_set(indent_buffer, ' ', sizeof(indent_buffer));
	indent_buffer[MIN(indent, 15) * 4] = '\0';

	*lexer = snapshot;
	uint32_t index = 0;
	while (lexer_peek(lexer).type != TOKEN_EOF && lexer_match(lexer, TOKEN_RIGHT_BRACE, &close_brace) == false) {
		Token token = lexer_next(lexer);

		// TODO: Handle anonymous structs
		if (token.type == TOKEN_STRUCT || token.type == TOKEN_UNION) {
			while (lexer_match(lexer, TOKEN_RIGHT_BRACE, 0) == false)
				lexer_next(lexer);

			continue;
		}

		if (token.type == TOKEN_IDENTIFIER) {
			internals = string_concat(
				scratch.arena,
				internals,
				string_format(scratch.arena, "%s[%u] = {\n", indent_buffer, index, SARG(token.string)));

			indent++;
			memory_set(indent_buffer, ' ', sizeof(indent_buffer));
			indent_buffer[MIN(indent, 15) * 4] = '\0';

			// Type
			internals = string_concat(
				scratch.arena,
				internals,
				string_format(scratch.arena, "%s.type = METATYPE_%.*s,\n", indent_buffer, SARG(token.string)));

			Token name = { 0 };
			Token array_size = { 0 };
			while (true) {
				Token token = lexer_next(lexer);
				if (token.type == TOKEN_SEMICOLON || token.type == TOKEN_EOF)
					break;

				if (token.type == TOKEN_LEFT_BRACKET) { // array member
					array_size = lexer_next(lexer);
					lexer_expect(lexer, TOKEN_RIGHT_BRACKET);
					break;
				}
				name = token;
			}

			internals = string_concat(
				scratch.arena,
				internals,
				string_format(scratch.arena, "%s.field_name = \"%.*s\",\n", indent_buffer, SARG(name.string)));
			// offset & size
			internals = string_concat(
				scratch.arena,
				internals,
				string_format(scratch.arena, "%s.offset = offsetof(%.*s, %.*s),\n", indent_buffer, SARG(identifier.string), SARG(name.string)));
			internals = string_concat(
				scratch.arena,
				internals,
				string_format(scratch.arena, "%s.size = sizeof(((%.*s*)0)->%.*s),\n", indent_buffer, SARG(identifier.string), SARG(name.string)));

			internals = string_concat(
				scratch.arena,
				internals,
				string_format(scratch.arena, "%s.stride = sizeof(%.*s),\n", indent_buffer, SARG(token.string)));
			if (array_size.type) {
				internals = string_concat(
					scratch.arena,
					internals,
					string_format(scratch.arena, "%s.count = %.*s,\n", indent_buffer, SARG(array_size.string)));
			} else {
				internals = string_concat(
					scratch.arena,
					internals,
					string_format(scratch.arena, "%s.count = 1,\n", indent_buffer));
            }

			indent--;
			memory_set(indent_buffer, ' ', sizeof(indent_buffer));
			indent_buffer[MIN(indent, 15) * 4] = '\0';

			internals = string_concat(
				scratch.arena,
				internals,
				string_format(scratch.arena, "%s},\n", indent_buffer, index, SARG(token.string)));

			index++;
		}
		/* printf("%.*s (%s) - %u\n", SARG(token.string), token_type_names[token.type], token.line); */
	}

	indent--;
	memory_set(indent_buffer, ' ', sizeof(indent_buffer));
	indent_buffer[MIN(indent, 15) * 4] = '\0';

	internals = string_concat(
		scratch.arena,
		internals,
		string_format(scratch.arena, "%s},\n", indent_buffer));

	if (type_defined)
		identifier = lexer_next(lexer);

	if (identifier.type) {

		indent--;
		memory_set(indent_buffer, ' ', sizeof(indent_buffer));
		indent_buffer[MIN(indent, 15) * 4] = '\0';

		printf("%s[METATYPE_%.*s] = {\n", indent_buffer, SARG(identifier.string));
		printf("%.*s", SARG(internals));


		printf("%s},\n", indent_buffer);
	}

	arena_scratch_end(scratch);
}

void enummerate_structures(Lexer *lexer, bool type_defined) {
	Token identifier = { 0 };
	if (lexer_peek(lexer).type == TOKEN_IDENTIFIER)
		identifier = lexer_next(lexer);

	Token open_brace = lexer_expect(lexer, TOKEN_LEFT_BRACE);

	Token close_brace = { 0 };
	while (lexer_peek(lexer).type != TOKEN_EOF && lexer_match(lexer, TOKEN_RIGHT_BRACE, &close_brace) == false) {
		Token token = lexer_next(lexer);

		// another structure
		if (token.type == TOKEN_STRUCT || token.type == TOKEN_UNION)
			enummerate_structures(lexer, false);

		if (token.type == TOKEN_IDENTIFIER) {
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

	if (identifier.type)
		printf("    METATYPE_%.*s,\n", SARG(identifier.string));
}

int main(int32_t carg, char *argv[]) {
	ArenaTemp scratch = arena_scratch_begin(NULL);

	FILE *file = fopen("src/components.h", "r");
	fseek(file, 0, SEEK_END);
	uint64_t size = ftell(file);

	char *buffer = malloc(size + 1);

	rewind(file);
	fread(buffer, sizeof(*buffer), size, file);
	fclose(file);

	/* printf("%.*s\n", file_buffer.size, file_buffer.memory); */
	Lexer lexer = lexer_make(string_wrap(buffer));

	bool parsing = true;
	Token last = { 0 };
	/* printf("typedef enum {\n"); */

	printf("#include \"meta.h\"\n\n");

	printf("typedef enum {\n");
	while (parsing) {
		Token token = lexer_next(&lexer);
		switch (token.type) {
			case TOKEN_UNION:
			case TOKEN_STRUCT: {
				if (token.type == TOKEN_UNION) {
					uint32_t y = 0;
				}
				enummerate_structures(&lexer, last.type == TOKEN_TYPEDEF);
			} break;
			case TOKEN_LEFT_PAREN:
			case TOKEN_RIGHT_PAREN:
			case TOKEN_LEFT_BRACE:
			case TOKEN_RIGHT_BRACE:
			case TOKEN_LEFT_BRACKET:
			case TOKEN_RIGHT_BRACKET:
			case TOKEN_COMMA:
			case TOKEN_DOT:
			case TOKEN_MINUS:
			case TOKEN_PLUS:
			case TOKEN_SEMICOLON:
			case TOKEN_COLON:
			case TOKEN_SLASH:
			case TOKEN_STAR:
			case TOKEN_PERCENT:
			case TOKEN_BANG:
			case TOKEN_BANG_EQUAL:
			case TOKEN_EQUAL:
			case TOKEN_EQUAL_EQUAL:
			case TOKEN_GREATER:
			case TOKEN_GREATER_EQUAL:
			case TOKEN_LESS:
			case TOKEN_LESS_EQUAL:
			case TOKEN_AMP:
			case TOKEN_AMP_AMP:
			case TOKEN_PIPE:
			case TOKEN_PIPE_PIPE:
			case TOKEN_IDENTIFIER:
			case TOKEN_STRING:
			case TOKEN_FLOAT:
			case TOKEN_INTEGER:
			case TOKEN_TRUE:
			case TOKEN_FALSE:
			case TOKEN_NULL:
			case TOKEN_TYPEDEF:
				break;

			case TOKEN_UNKNOWN:
			case TOKEN_MAX: {
				continue;
			} break;
			case TOKEN_EOF: {
				parsing = false;
			} break;
		}

		last = token;
	}
    printf("\n    META_TYPE_MAX,\n");
	printf("} TypeIdentifier;\n\n");

	lexer = lexer_make(string_wrap(buffer));
	printf("static Type types[METATYPE_MAX] = {\n");
	parsing = true;
	while (parsing) {
		Token token = lexer_next(&lexer);
		switch (token.type) {
			case TOKEN_UNION:
			case TOKEN_STRUCT: {
				if (token.type == TOKEN_UNION) {
					uint32_t y = 0;
				}
				parse_structures(&lexer, last.type == TOKEN_TYPEDEF);
			} break;
			case TOKEN_LEFT_PAREN:
			case TOKEN_RIGHT_PAREN:
			case TOKEN_LEFT_BRACE:
			case TOKEN_RIGHT_BRACE:
			case TOKEN_LEFT_BRACKET:
			case TOKEN_RIGHT_BRACKET:
			case TOKEN_COMMA:
			case TOKEN_DOT:
			case TOKEN_MINUS:
			case TOKEN_PLUS:
			case TOKEN_SEMICOLON:
			case TOKEN_COLON:
			case TOKEN_SLASH:
			case TOKEN_STAR:
			case TOKEN_PERCENT:
			case TOKEN_BANG:
			case TOKEN_BANG_EQUAL:
			case TOKEN_EQUAL:
			case TOKEN_EQUAL_EQUAL:
			case TOKEN_GREATER:
			case TOKEN_GREATER_EQUAL:
			case TOKEN_LESS:
			case TOKEN_LESS_EQUAL:
			case TOKEN_AMP:
			case TOKEN_AMP_AMP:
			case TOKEN_PIPE:
			case TOKEN_PIPE_PIPE:
			case TOKEN_IDENTIFIER:
			case TOKEN_STRING:
			case TOKEN_FLOAT:
			case TOKEN_INTEGER:
			case TOKEN_TRUE:
			case TOKEN_FALSE:
			case TOKEN_NULL:
			case TOKEN_TYPEDEF:
				break;

			case TOKEN_UNKNOWN:
			case TOKEN_MAX: {
				continue;
			} break;
			case TOKEN_EOF: {
				parsing = false;
			} break;
		}

		last = token;
	}
	printf("};\n");

	arena_scratch_end(scratch);

	return 0;
}
