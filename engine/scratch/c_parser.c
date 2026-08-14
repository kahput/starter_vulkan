#include "common.h"
#include "core/arena.h"
#include "core/strings.h"
#include "os.h"
#include <core.h>

#include <stdio.h>
#include <utils/lexer.h>

#define KEYWORD_LIST        \
	X(STRUCT, "struct")     \
	X(UNION, "union")       \
	X(ENUM, "enum")         \
	X(TYPEDEF, "typedef")   \
	X(EXTERN, "extern")     \
	X(STATIC, "static")     \
	X(CONST, "const")       \
	X(RESTRICT, "restrict") \
	X(VOLATILE, "volatile") \
	X(VOID, "void")         \
	X(CHAR, "char")         \
	X(SHORT, "short")       \
	X(INT, "int")           \
	X(LONG, "long")         \
	X(FLOAT, "float")       \
	X(DOUBLE, "double")     \
	X(SIGNED, "signed")     \
	X(UNSIGNED, "unsigned") \
	X(BOOL, "_Bool")

typedef enum {
#define X(name, key) TOKEN_##name,
	KEYWORD_LIST
#undef X
		TOKEN_KEYWORD_MAX,
} TokenKeywordType;

static const TokenKeywordType storage_class[] = {
	TOKEN_TYPEDEF, TOKEN_EXTERN, TOKEN_STATIC
};

// clang-format off
static const TokenKeywordType type_specifier[] = {
    TOKEN_SHORT, TOKEN_LONG, TOKEN_UNSIGNED, TOKEN_SIGNED, 
    TOKEN_VOID, TOKEN_CHAR, TOKEN_INT, TOKEN_FLOAT, TOKEN_DOUBLE,
    TOKEN_BOOL, 
};
// clang-format on

static const TokenKeywordType type_qualifier[] = {
	TOKEN_CONST, TOKEN_VOLATILE, TOKEN_RESTRICT
};

String8 keyword_to_string[TOKEN_KEYWORD_MAX] = {
#define X(name, key) [TOKEN_##name] = str_comp(key),
	KEYWORD_LIST
#undef X
};

TokenType keyword_to_token[TOKEN_KEYWORD_MAX] = {
#define X(name, key) [TOKEN_##name] = TOKEN_KEYWORD_0 + TOKEN_##name,
	KEYWORD_LIST
#undef X
};

typedef struct AST_Node AST_Node;

#define AST_MAX_SYMBOLS 2048
static AST_Node *symbol_table[AST_MAX_SYMBOLS] = { 0 };
static uint32_t symbol_count = 0;

typedef enum {
	AST_NODE_PROGRAM,
	AST_NODE_DECLARATION,

	AST_NODE_BUILTIN,
	AST_NODE_TYPEDEF_NAME,
	AST_NODE_STRUCT,
	AST_NODE_UNION,
	AST_NODE_ENUM,

	AST_NODE_ENUMERATOR,
	AST_NODE_DECLARATOR,
	AST_NODE_POINTER,
	AST_NODE_ARRAY,
} AST_NodeType;

typedef enum C_BuiltinKind {
	C_BUILTIN_VOID,

	C_BUILTIN_BOOL,

	C_BUILTIN_CHAR,
	C_BUILTIN_UNSIGNED_CHAR,

	C_BUILTIN_SHORT,
	C_BUILTIN_UNSIGNED_SHORT,
	C_BUILTIN_INT,
	C_BUILTIN_UNSIGNED_INT,
	C_BUILTIN_LONG,
	C_BUILTIN_UNSIGNED_LONG,
	C_BUILTIN_LONG_LONG,
	C_BUILTIN_UNSIGNED_LONG_LONG,

	C_BUILTIN_FLOAT,
	C_BUILTIN_DOUBLE,
	C_BUILTIN_LONG_DOUBLE,

	C_BUILTIN_MAX,
} C_BuiltinKind;

String8 c_builtin_to_string[C_BUILTIN_MAX] = {
	[C_BUILTIN_VOID] = str_comp("void"),
	[C_BUILTIN_BOOL] = str_comp("_Bool"),
	[C_BUILTIN_CHAR] = str_comp("char"),
	[C_BUILTIN_UNSIGNED_CHAR] = str_comp("unsigned char"),
	[C_BUILTIN_SHORT] = str_comp("short"),
	[C_BUILTIN_UNSIGNED_SHORT] = str_comp("unsigned short"),
	[C_BUILTIN_INT] = str_comp("int"),
	[C_BUILTIN_UNSIGNED_INT] = str_comp("unsigned int"),
	[C_BUILTIN_LONG] = str_comp("long"),
	[C_BUILTIN_UNSIGNED_LONG] = str_comp("unsigned long"),
	[C_BUILTIN_LONG_LONG] = str_comp("long long"),
	[C_BUILTIN_UNSIGNED_LONG_LONG] = str_comp("unsigned long long"),
	[C_BUILTIN_FLOAT] = str_comp("float"),
	[C_BUILTIN_DOUBLE] = str_comp("double"),
	[C_BUILTIN_LONG_DOUBLE] = str_comp("long double"),
};

enum AST_QualifierBits {
	AST_QUAL_CONST = BIT(0),
	AST_QUAL_RESTRICT = BIT(1),
	AST_QUAL_VOLATILE = BIT(2),
};
typedef uint8_t AST_QualifierSet;

typedef enum {
	AST_STORAGE_NONE,

	AST_STORAGE_TYPEDEF,
	AST_STORAGE_EXTERN,
	AST_STORAGE_STATIC,
} AST_StorageQualifier;

static const String8 storage_class_to_string[] = {
	[AST_STORAGE_NONE] = str_comp("None"),

	[AST_STORAGE_TYPEDEF] = str_comp("typedef"),
	[AST_STORAGE_EXTERN] = str_comp("extern"),
	[AST_STORAGE_STATIC] = str_comp("static"),
};

ENSURE_INLINE AST_StorageQualifier
keyword_to_storage_class(TokenType type) {
	AST_StorageQualifier result = AST_STORAGE_NONE;

	if ((TokenKeywordType)(type - TOKEN_KEYWORD_0) == TOKEN_TYPEDEF) result = AST_STORAGE_TYPEDEF;
	if ((TokenKeywordType)(type - TOKEN_KEYWORD_0) == TOKEN_STATIC) result = AST_STORAGE_STATIC;
	if ((TokenKeywordType)(type - TOKEN_KEYWORD_0) == TOKEN_EXTERN) result = AST_STORAGE_EXTERN;

	return result;
}

typedef struct {
	AST_StorageQualifier storage;
	AST_QualifierSet qualifiers;

	bool is_signed;
	bool is_bool;
	bool is_float;
	bool is_double;
	bool is_unsigned;
	bool is_char;
	bool is_short;
	bool is_int;
	bool is_void;
	uint32_t long_count;
} AST_DeclSpecifiers;

struct AST_Node {
	AST_NodeType type;

	// Tree
	AST_Node *parent;
	AST_Node *first_child, *last_child;
	AST_Node *next_sibling, *prev_sibling;

	Token name;
	String8 string;
	C_BuiltinKind builtin;
	AST_QualifierSet qualifiers;
	AST_StorageQualifier storage;
};

AST_Node *ast_make(Arena *arena, AST_NodeType type) {
	AST_Node *result = arena_push_count(arena, AST_Node, 1);
	result->type = type;

	return result;
}

bool ast_unparent(AST_Node *child) {
	bool ok = child && child->parent;
	if (ok) {
		AST_Node *parent = child->parent;
		if (parent->first_child == parent->last_child) {
			parent->first_child = parent->last_child = 0;
		} else {
			child->prev_sibling->next_sibling = child->next_sibling;
			child->next_sibling->prev_sibling = child->prev_sibling;

			if (parent->first_child == child)
				parent->first_child = child->next_sibling;
			if (parent->last_child == child)
				parent->last_child = child->prev_sibling;
		}

		child->parent = 0;
		child->next_sibling = child->prev_sibling = 0;
	}

	return ok;
}
bool ast_push(AST_Node *parent, AST_Node *child, bool front) {
	bool ok = parent && child && child != parent;
	if (ok) {
		ast_unparent(child);

		if (parent->first_child == 0) {
			parent->first_child = parent->last_child = child;
			child->next_sibling = child->prev_sibling = child;
		} else {
			child->prev_sibling = parent->last_child;
			child->next_sibling = parent->first_child;

			parent->last_child->next_sibling = child;
			parent->first_child->prev_sibling = child;

			if (front)
				parent->first_child = child;
			else
				parent->last_child = child;
		}

		child->parent = parent;
	}

	return ok;
}

bool ast_pushback(AST_Node *parent, AST_Node *child) { return ast_push(parent, child, false); }
bool ast_pushfront(AST_Node *parent, AST_Node *child) { return ast_push(parent, child, true); }

bool match_impl(Lexer *lexer, TokenType *token_types, uint32_t token_count) {
	bool ok = lexer && token_types && token_count;
	if (ok) {
		Token peek = lexer_peek(lexer);

		ok = false;
		for (uint32_t index = 0; index < token_count; ++index) {
			if (peek.type == token_types[index]) {
				ok = true;
				lexer_advance(lexer); // consume token
				break;
			}
		}
	}

	return ok;
}

bool match_keyword_impl(Lexer *lexer, const TokenKeywordType *in_keywords, uint32_t keyword_count) {
	bool ok = lexer && in_keywords;
	if (ok) {
		ArenaTemp scratch = arena_scratch_begin(0);
		TokenKeywordType *keywords = arena_push_count(scratch.arena, TokenKeywordType, keyword_count);
		memory_copy_count(keywords, in_keywords, keyword_count);

		for (uint32_t index = 0; index < keyword_count; ++index)
			keywords[index] += TOKEN_KEYWORD_0;

		ok = match_impl(lexer, (TokenType *)keywords, keyword_count);
		arena_scratch_end(scratch);
	}

	return ok;
}

AST_Node *match_typedef_symbol(Lexer *lexer) {
	AST_Node *result = false;

	bool ok = lexer && lexer_peek(lexer).type == TOKEN_IDENTIFIER;
	if (ok) {
		String8 name = lexer_peek(lexer).lexeme;

		for (uint32_t index = 0; index < symbol_count; ++index) {
			if (str8_equals(name, symbol_table[index]->name.lexeme)) {
				lexer_advance(lexer); // consume identifier
				result = symbol_table[index];
				break;
			}
		}
	}

	return result;
}

#define match(...) match_impl(lexer, array_arg(TokenType, __VA_ARGS__))
#define match_keyword(...) match_keyword_impl(lexer, array_arg(TokenKeywordType, __VA_ARGS__))

AST_Node *ast_parse_decl(Arena *arena, Lexer *lexer);
AST_Node *ast_parse_declarator(Arena *arena, Lexer *lexer);

AST_Node *ast_parse_pointer(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		result = ast_make(arena, AST_NODE_POINTER);

		while (match_keyword_impl(lexer, type_qualifier, countof(type_qualifier))) {
			Token t = lexer->current;

			// clang-format off
			switch (t.type - TOKEN_KEYWORD_0) {
				case TOKEN_CONST: result->qualifiers |= AST_QUAL_CONST; break;
				case TOKEN_RESTRICT: result->qualifiers |= AST_QUAL_RESTRICT; break;
				case TOKEN_VOLATILE: result->qualifiers |= AST_QUAL_VOLATILE; break;

				default:
					break;
			}
			// clang-format on
		}

		if (match(TOKEN_STAR)) {
			AST_Node *ptr = ast_parse_pointer(arena, lexer);
			ast_pushback(ptr, result);
			result = ptr;
		}
	}

	return result;
}

AST_Node *ast_parse_declarator(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		AST_Node *leading_pointers = 0;
		if (match(TOKEN_STAR))
			leading_pointers = ast_parse_pointer(arena, lexer);

		if (match(TOKEN_LPAREN)) {
			result = ast_parse_declarator(arena, lexer);
			lexer_consume(lexer, TOKEN_RPAREN, s("Expect ')' after enclosed declarator."));
		} else {
			result = ast_make(arena, AST_NODE_DECLARATOR);
			Token name = lexer_consume(lexer, TOKEN_IDENTIFIER, s("Expect declarator name."));
			result->name = name;
		}

		AST_Node *arrays_head = 0, *arrays_tail = 0;
		while (match(TOKEN_LBRACKET)) { // array
			AST_Node *arr = ast_make(arena, AST_NODE_ARRAY);

			arr->string.text = lexer_peek(lexer).lexeme.text;
			while (lexer_at_end(lexer) == false && match(TOKEN_RBRACKET) == false)
				lexer_advance(lexer);
			arr->string = str8_range((char *)arr->string.text, (char *)lexer->current.lexeme.text);
			ast_pushback(arr, arr);

			if (arrays_head == 0) arrays_head = arr;
			if (arrays_tail) ast_pushback(arrays_tail, arr);
			arrays_tail = arr;
		}

		if (arrays_head) {
			AST_Node *tail = result;
			while (tail->first_child != 0)
				tail = tail->first_child;

			ast_pushback(tail, arrays_head);
		}
		if (leading_pointers)
			ast_pushback(arrays_tail ? arrays_tail : result, leading_pointers);

		if (match(TOKEN_LPAREN)) { // TODO: Handle functions
			uint32_t depth = 1;
			while (lexer_at_end(lexer) == false && depth) {
				if (lexer_peek(lexer).type == TOKEN_LPAREN) depth++;
				if (lexer_peek(lexer).type == TOKEN_RPAREN) depth--;
				lexer_advance(lexer);
			}
		}

		if (match(TOKEN_EQUAL)) // skip assignment
			while (lexer_peek(lexer).type != TOKEN_SEMICOLON)
				lexer_advance(lexer);
	}

	return result;
}

AST_Node *ast_parse_struct(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		result = ast_make(arena, (lexer->current.type - TOKEN_KEYWORD_0) == TOKEN_STRUCT ? AST_NODE_STRUCT : AST_NODE_UNION);

		if (match(TOKEN_IDENTIFIER)) {
			if (lexer_peek(lexer).type == TOKEN_LPAREN) // ignore alignas
				while (match(TOKEN_RPAREN) == false)
					lexer_advance(lexer);
			else
				result->name = lexer->current;
		}

		if (match(TOKEN_LBRACE)) {
			while (match(TOKEN_RBRACE) == false)
				ast_pushback(result, ast_parse_decl(arena, lexer));
		}
	}

	return result;
}

AST_Node *ast_parse_enum(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		result = ast_make(arena, AST_NODE_ENUM);
		if (match(TOKEN_IDENTIFIER))
			result->name = lexer->current;

		if (match(TOKEN_LBRACE)) do {
				AST_Node *enumerator = ast_make(arena, AST_NODE_ENUMERATOR);
				enumerator->name = lexer_consume(lexer, TOKEN_IDENTIFIER, s("Expect enumerator."));
				ast_pushback(result, enumerator);

				if (match(TOKEN_EQUAL)) {
					enumerator->string.text = lexer_peek(lexer).lexeme.text;
					while ((lexer_peek(lexer).type == TOKEN_COMMA || lexer_peek(lexer).type == TOKEN_RBRACE) == false)
						lexer_advance(lexer);

					enumerator->string = str8_range((char *)enumerator->string.text, (char *)lexer_peek(lexer).lexeme.text);
					ast_pushback(enumerator, enumerator);
				}

				if (lexer_peek(lexer).type == TOKEN_COMMA)
					lexer_advance(lexer);
			} while (match(TOKEN_RBRACE) == false);
	}

	return result;
}

void ast_resolve_decl_specifier(AST_DeclSpecifiers *specs, AST_Node *result) {
	// clang-format off
	if (specs->is_bool) result->builtin = C_BUILTIN_BOOL;
	else if (specs->is_char) result->builtin = specs->is_unsigned ? C_BUILTIN_UNSIGNED_CHAR : C_BUILTIN_CHAR;
	else if (specs->is_short) result->builtin = specs->is_unsigned ? C_BUILTIN_UNSIGNED_SHORT : C_BUILTIN_SHORT;
	else if (specs->is_float) result->builtin = C_BUILTIN_FLOAT;
	else if (specs->is_double) result->builtin = specs->long_count == 1 ? C_BUILTIN_LONG_DOUBLE : C_BUILTIN_DOUBLE;
	else if (specs->long_count == 1) result->builtin = specs->is_unsigned ? C_BUILTIN_UNSIGNED_LONG : C_BUILTIN_LONG;
	else if (specs->long_count == 2) result->builtin = specs->is_unsigned ? C_BUILTIN_UNSIGNED_LONG_LONG : C_BUILTIN_LONG_LONG;
	else if (specs->is_int || specs->is_signed || specs->is_unsigned) result->builtin = specs->is_unsigned ? C_BUILTIN_UNSIGNED_INT : C_BUILTIN_INT;
	// clang-format on
}
bool ast_decl_specifiers_empty(AST_DeclSpecifiers *specs) {
	bool result = false;

	ArenaTemp scratch = arena_scratch_begin(0);
	AST_Node *temp = ast_make(scratch.arena, AST_NODE_BUILTIN);
	ast_resolve_decl_specifier(specs, temp);
	if (temp->builtin == C_BUILTIN_VOID && specs->is_void == false) result = true;
	arena_scratch_end(scratch);

	return result;
}

AST_Node *ast_parse_decl(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		result = ast_make(arena, AST_NODE_DECLARATION);

		AST_DeclSpecifiers specifiers = { 0 };
		AST_Node *type = 0, *symbol = 0;
		AST_QualifierSet type_qualifiers = 0;

		while (
			match_keyword_impl(lexer, type_specifier, countof(type_specifier)) ||
			match_keyword_impl(lexer, type_qualifier, countof(type_qualifier)) || //
			match_keyword_impl(lexer, storage_class, countof(storage_class)) || //
			match_keyword(TOKEN_STRUCT, TOKEN_UNION, TOKEN_ENUM) ||
			((symbol = match_typedef_symbol(lexer))) //
		) {
			Token t = lexer->current;
			if (symbol) {
				type = ast_make(arena, AST_NODE_TYPEDEF_NAME);
				type->name = symbol->name;
				symbol = 0;
				continue;
			}

			// clang-format off
			switch ((TokenKeywordType)t.type - TOKEN_KEYWORD_0) {
				case TOKEN_TYPEDEF:
				case TOKEN_EXTERN:
				case TOKEN_STATIC:
					if (result->storage != AST_STORAGE_NONE)
						report(t.line, t.column, lexer_error_location_string(arena, t), s("Multiple storage classes in declaration."));
					result->storage = keyword_to_storage_class(t.type);
					break;

                case TOKEN_CONST:    type_qualifiers |= AST_QUAL_CONST; break;
                case TOKEN_RESTRICT: type_qualifiers |= AST_QUAL_RESTRICT; break;
                case TOKEN_VOLATILE: type_qualifiers |= AST_QUAL_VOLATILE; break;

				case TOKEN_VOID: specifiers.is_void = true; break;
                case TOKEN_BOOL: specifiers.is_bool = true; break;
				case TOKEN_CHAR: specifiers.is_char = true; break;
				case TOKEN_SHORT: specifiers.is_short = true; break;
				case TOKEN_INT: specifiers.is_int = true; break;
				case TOKEN_LONG: specifiers.long_count += 1; break;
				case TOKEN_SIGNED: specifiers.is_signed = true; break;
				case TOKEN_UNSIGNED: specifiers.is_unsigned = true; break;
				case TOKEN_FLOAT: specifiers.is_float = true; break;
				case TOKEN_DOUBLE: specifiers.is_double = true; break;

				case TOKEN_STRUCT: type = ast_parse_struct(arena, lexer); break;
				case TOKEN_UNION: type = ast_parse_struct(arena, lexer); break;
				case TOKEN_ENUM: type = ast_parse_enum(arena, lexer); break;
			}
			// clang-format on
		}

		if (type == 0 && symbol == 0 && ast_decl_specifiers_empty(&specifiers) && lexer_peek(lexer).type == TOKEN_IDENTIFIER) {
			type = ast_make(arena, AST_NODE_TYPEDEF_NAME);
			type->name = lexer_advance(lexer);
		}

		if (type == 0) {
			type = ast_make(arena, AST_NODE_BUILTIN);
			ast_resolve_decl_specifier(&specifiers, type);
		}
		type->qualifiers = type_qualifiers;
		ast_pushback(result, type);

		if (lexer_peek(lexer).type != TOKEN_SEMICOLON) do {
				ast_pushback(result, ast_parse_declarator(arena, lexer));
				if (result->storage == AST_STORAGE_TYPEDEF)
					symbol_table[symbol_count++] = result->last_child;
			} while (match(TOKEN_COMMA));

		lexer_consume(lexer, TOKEN_SEMICOLON, s("Expect ';' after declaration"));
	}

	return result;
}
static const String8 synthetic_types_header = str_comp(
	"typedef unsigned char      uint8_t;\n"
	"typedef unsigned short     uint16_t;\n"
	"typedef unsigned int       uint32_t;\n"
	"typedef unsigned long long uint64_t;\n"
	"\n"
	"typedef signed char        int8_t;\n"
	"typedef signed short       int16_t;\n"
	"typedef signed int         int32_t;\n"
	"typedef signed long long   int64_t;\n"
	"\n"
	"typedef unsigned long long size_t;\n"
	"typedef signed long long   ptrdiff_t;\n"
	"typedef unsigned long long uintptr_t;\n"
	"typedef signed long long   intptr_t;\n"
	"typedef _Bool              bool;\n");

void ast_visit(AST_Node *node, uint32_t indent_level) {
	for (uint32_t i = 0; i < indent_level; ++i) {
		printf("  ");
	}

	switch (node->type) {
		case AST_NODE_PROGRAM: {
			AST_Node *decl = node->first_child;
			do {
				ast_visit(decl, indent_level);
				decl = decl->next_sibling;
			} while (decl && decl != node->first_child);
		} break;
		case AST_NODE_DECLARATION: {
			printf("DECLARATION");
			if (node->storage != 0)
				printf("(%.*s)", str_spread(storage_class_to_string[node->storage]));
			printf("\n");
			ast_visit(node->first_child, indent_level + 1);

			AST_Node *declarator = node->first_child->next_sibling;
			while (declarator && declarator != node->first_child) {
				ast_visit(declarator, indent_level + 1);
				declarator = declarator->next_sibling;
			}
		} break;
		case AST_NODE_BUILTIN: {
			printf("BUILTIN ");
			printf("(%.*s", str_spread(c_builtin_to_string[node->builtin]));
			if (has_flag(node->qualifiers, AST_QUAL_CONST))
				printf("%sCONST", ", ");
			if (has_flag(node->qualifiers, AST_QUAL_RESTRICT))
				printf("%sRESTRICT", ", ");
			if (has_flag(node->qualifiers, AST_QUAL_VOLATILE))
				printf("%sVOLATILE", ", ");
			printf(")\n");
		} break;
		case AST_NODE_TYPEDEF_NAME: {
			printf("TYPEDEF_NAME");
			printf("(%.*s)\n", str_spread(node->name.lexeme));
		} break;
		case AST_NODE_UNION:
		case AST_NODE_STRUCT: {
			printf(node->type == AST_NODE_STRUCT ? "STRUCT" : "UNION");
			bool prev = false;
			if (node->name.lexeme.length || node->qualifiers)
				printf("(");
			if (node->name.lexeme.length) {
				printf("%.*s", str_spread(node->name.lexeme));
				prev = true;
			}
			if (has_flag(node->qualifiers, AST_QUAL_CONST)) {
				printf("%sCONST", prev ? ", " : "");
				prev = true;
			}
			if (has_flag(node->qualifiers, AST_QUAL_RESTRICT)) {
				printf("%sRESTRICT", prev ? ", " : "");
				prev = true;
			}
			if (has_flag(node->qualifiers, AST_QUAL_VOLATILE)) {
				printf("%sVOLATILE", prev ? ", " : "");
				prev = true;
			}
			if (node->name.lexeme.length || node->qualifiers)
				printf(")");
			printf("\n");

			AST_Node *member = node->first_child;
			if (member) do {
					ast_visit(member, indent_level + 1);
					member = member->next_sibling;
				} while (member && member != node->first_child);
		} break;
		case AST_NODE_ENUM: {
			printf("ENUM");
			bool prev = false;
			if (node->name.lexeme.length || node->qualifiers)
				printf("(");
			if (node->name.lexeme.length) {
				printf("%.*s", str_spread(node->name.lexeme));
				prev = true;
			}
			if (has_flag(node->qualifiers, AST_QUAL_CONST)) {
				printf("%sCONST", prev ? ", " : "");
				prev = true;
			}
			if (has_flag(node->qualifiers, AST_QUAL_RESTRICT)) {
				printf("%sRESTRICT", prev ? ", " : "");
				prev = true;
			}
			if (has_flag(node->qualifiers, AST_QUAL_VOLATILE)) {
				printf("%sVOLATILE", prev ? ", " : "");
				prev = true;
			}
			if (node->name.lexeme.length || node->qualifiers)
				printf(")");
			printf("\n");

			AST_Node *enumerator = node->first_child;
			if (enumerator)
				do {
					ast_visit(enumerator, indent_level + 1);
					enumerator = enumerator->next_sibling;
				} while (enumerator && enumerator != node->first_child);
		} break;
		case AST_NODE_ENUMERATOR: {
			printf("ENUMERATOR ");
			printf("name=%.*s", str_spread(node->name.lexeme));
			if (node->string.length)
				printf(" tokens=%.*s", str_spread(node->string));
			printf("\n");

			AST_Node *expr = node->first_child;
			if (expr) ast_visit(expr, indent_level + 1);
		} break;
		case AST_NODE_DECLARATOR: {
			String8 name = node->name.lexeme;
			printf("DECLARATOR");
			printf("(%.*s)\n", str_spread(node->name.lexeme));

			if (node->first_child)
				ast_visit(node->first_child, indent_level + 1);
		} break;
		case AST_NODE_POINTER: {
			printf("POINTER");
			if (node->qualifiers != 0)
				printf("(");
			bool prev = false;
			if (has_flag(node->qualifiers, AST_QUAL_CONST)) {
				printf("CONST");
				prev = true;
			}
			if (has_flag(node->qualifiers, AST_QUAL_RESTRICT)) {
				printf("%sRESTRICT", prev ? " | " : "");
				prev = true;
			}
			if (has_flag(node->qualifiers, AST_QUAL_VOLATILE)) {
				printf("%sVOLATILE", prev ? " | " : "");
				prev = true;
			}
			if (node->qualifiers)
				printf(")");
			printf("\n");

			if (node->first_child)
				ast_visit(node->first_child, indent_level + 1);
			if (node->first_child != node->last_child)
				ast_visit(node->last_child, indent_level + 1);
		} break;
		case AST_NODE_ARRAY: {
			printf("ARRAY");
			if (node->string.length)
				printf("(%.*s)", str_spread(node->string));
			printf("\n");

			if (node->first_child)
				ast_visit(node->first_child, indent_level + 1);
			if (node->last_child != node->first_child)
				ast_visit(node->last_child, indent_level + 1);
		} break;
	}
}

void ast_print_typeid(AST_Node *node) {
	switch (node->type) {
		case AST_NODE_PROGRAM: {
			AST_Node *decl = node->first_child;
			do {
				ast_print_typeid(decl);
				decl = decl->next_sibling;
			} while (decl && decl != node->first_child);
		} break;
		case AST_NODE_DECLARATION: {
			if (node->storage == AST_STORAGE_TYPEDEF) {
				AST_Node *declarator = node->first_child->next_sibling;
				while (declarator && declarator != node->first_child) {
					if (node->first_child->type != AST_NODE_STRUCT && node->first_child->type != AST_NODE_UNION && node->first_child->type != AST_NODE_ENUM) {
						String8 type = node->first_child->type == AST_NODE_BUILTIN ? c_builtin_to_string[node->first_child->builtin] : node->first_child->name.lexeme;
						fprintf(stderr, "#define TYPE_%.*s TYPE_%.*s\n", str_spread(declarator->name.lexeme), str_spread(type));
					} else {
						fprintf(stdout, "    TYPE_%.*s,\n", str_spread(declarator->name.lexeme));
					}
					declarator = declarator->next_sibling;
				}
			}
		} break;
		case AST_NODE_BUILTIN:
		case AST_NODE_TYPEDEF_NAME:
		case AST_NODE_STRUCT:
		case AST_NODE_UNION:
		case AST_NODE_ENUM:
		case AST_NODE_ENUMERATOR:
		case AST_NODE_DECLARATOR:
		case AST_NODE_POINTER:
		case AST_NODE_ARRAY:
			break;
	}
}

int main(void) {
	Arena arena[] = { arena_make(MiB(8)) };

	String8 headers[] = {
		synthetic_types_header,
		os_file_read_entire(arena, s("engine/src/common.h")),
		os_file_read_entire(arena, s("engine/src/core/cmath.h")),
		os_file_read_entire(arena, s("engine/src/core/arena.h")),
		os_file_read_entire(arena, s("engine/src/draw.h")),
		os_file_read_entire(arena, s("engine/src/core/geom_types.h")),
		os_file_read_entire(arena, s("engine/src/utils/anim.h")),
		os_file_read_entire(arena, s("engine/scratch/skinning.c")),
		os_file_read_entire(arena, s("engine/src/gfx/gfx_types.h")),
		os_file_read_entire(arena, s("engine/src/meta.h")),
		os_file_read_entire(arena, s("engine/scratch/c_parser.c")),
	};

	AST_Node *program = ast_make(arena, AST_NODE_PROGRAM);
	for (uint32_t index = 0; index < countof(headers); ++index) {
		Lexer lexer[] = { lexer_make(headers[index], keyword_to_string, countof(keyword_to_string)) };
		while (lexer_at_end(lexer) == false) {
			Token peek = lexer_peek(lexer);

			switch (peek.type) {
				case TOKEN_STRUCT + TOKEN_KEYWORD_0:
				case TOKEN_UNION + TOKEN_KEYWORD_0:
				case TOKEN_ENUM + TOKEN_KEYWORD_0:
				case TOKEN_TYPEDEF + TOKEN_KEYWORD_0:
					ast_pushback(program, ast_parse_decl(arena, lexer));
					break;

				default:
					lexer_advance(lexer);
					break;
			}
		}

		/* ast_print_typeid(program); */
	}
	ast_visit(program, 0);

	arena_destroy(arena);
	return 0;
}
