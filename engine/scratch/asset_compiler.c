#include "core/arena.h"
#include "core/debug.h"
#include "core/strings.h"
#include "os.h"
#include <core.h>
#include <stdio.h>
#include <utils/lexer.h>

#include <gfx/gfx_types.h>

#define KEYWORD_LIST        \
	X(SHADER, "shader")     \
	X(PIPELINE, "pipeline") \
	X(SHARED, "shared")     \
	X(VERTEX, "vertex")     \
	X(FRAGMENT, "fragment") \
	X(COMPUTE, "compute")

typedef enum {
#define X(symbol, display) KEYWORD_##symbol,
	KEYWORD_LIST
#undef X

		KEYWORD_MAX,
} Keyword;
#define keyword_token(k) (TOKEN_KEYWORD_0 + (k))

static const string8 keyword_to_string[KEYWORD_MAX] = {
#define X(symbol, display) [KEYWORD_##symbol] = comp8(display),
	KEYWORD_LIST
#undef X
};

typedef enum {
	AST_BLOCK_KIND_SHARED,
	AST_BLOCK_KIND_FRAGMENT,
	AST_BLOCK_KIND_VERTEX,
	AST_BLOCK_KIND_COMPUTE,

	AST_BLOCK_KIND_MAX,
} AST_BlockKind;

static const string8 ast_block_kind_to_string[AST_BLOCK_KIND_MAX] = {
	[AST_BLOCK_KIND_SHARED] = comp8("AST_BLOCK_KIND_SHARED"),
	[AST_BLOCK_KIND_FRAGMENT] = comp8("AST_BLOCK_KIND_FRAGMENT"),
	[AST_BLOCK_KIND_VERTEX] = comp8("AST_BLOCK_KIND_VERTEX"),
	[AST_BLOCK_KIND_COMPUTE] = comp8("AST_BLOCK_KIND_COMPUTE"),
};

static const ShaderStage ast_block_kind_to_shader_stage[AST_BLOCK_KIND_MAX] = {
	[AST_BLOCK_KIND_FRAGMENT] = SHADER_STAGE_FRAGMENT,
	[AST_BLOCK_KIND_VERTEX] = SHADER_STAGE_VERTEX,
	[AST_BLOCK_KIND_COMPUTE] = SHADER_STAGE_COMPUTE,
};

static const string8 ast_block_kind_to_display_string[AST_BLOCK_KIND_MAX] = {
	[AST_BLOCK_KIND_SHARED] = comp8("shared"),
	[AST_BLOCK_KIND_FRAGMENT] = comp8("fragment"),
	[AST_BLOCK_KIND_VERTEX] = comp8("vertex"),
	[AST_BLOCK_KIND_COMPUTE] = comp8("compute"),
};

typedef enum {
	AST_NODE_PROGRAM,
	AST_NODE_SHADER,

	AST_NODE_SOURCE_DECL,
	AST_NODE_SOURCE_REF,
	AST_NODE_PIPELINE_DECL,

	AST_NODE_EXPR_ASSIGN,
	AST_NODE_EXPR_CALL,
	AST_NODE_EXPR_VARIABLE,

	AST_NODE_MAX,
} AST_NodeType;

typedef struct AST_Node AST_Node;
struct AST_Node {
	AST_NodeType type;

	// Tree
	AST_Node *parent;
	AST_Node *first_child, *last_child;
	AST_Node *next_sibling, *prev_sibling;

	AST_BlockKind block_kind;
	Token identifier;
	string8 glsl;
};

typedef struct AST_NodeList AST_NodeList;
struct AST_NodeList {
	AST_NodeList *next;
	AST_Node *node;
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

bool ast_pushback(AST_Node *parent, AST_Node *child) {
	return ast_push(parent, child, false);
}
bool ast_pushfront(AST_Node *parent, AST_Node *child) {
	return ast_push(parent, child, true);
}

AST_NodeList *shader_symbol_list = 0;

AST_Node *ast_parse_expr_primary(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;
	ArenaTemp scratch = arena_scratch_begin(arena);

	bool ok = arena && lexer;
	if (ok) {
		switch (lexer_peek(lexer).type) {
			case TOKEN_INTEGER:
			case TOKEN_IDENTIFIER: {
				result = ast_make(arena, AST_NODE_EXPR_VARIABLE);
				result->identifier = lexer_advance(lexer);
			} break;
			default: {
				Token tok = lexer_peek(lexer);
				string8 message = fmt8(
					scratch.arena,
					"Unexpected token '%.*s'",
					arg8(tok.lexeme));

				report(tok.line, tok.column, lexer_error_location_string(scratch.arena, tok), message);
				lexer_advance(lexer);
			} break;
		}
		if (lexer_peek(lexer).type == TOKEN_INTEGER) {
		} else if (lexer_peek(lexer).type == TOKEN_IDENTIFIER) {
			result = ast_make(arena, AST_NODE_EXPR_VARIABLE);
			result->identifier = lexer_advance(lexer);
		} else {
		}
	}

	arena_scratch_end(scratch);
	return result;
}

AST_Node *ast_parse_expr_assignment(Arena *arena, Lexer *lexer);

AST_Node *ast_parse_expr_call(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		result = ast_parse_expr_primary(arena, lexer);

		if (lexer_match(lexer, TOKEN_LPAREN, 0)) {
			AST_Node *call = ast_make(arena, AST_NODE_EXPR_CALL);
			call->identifier = result->identifier;

			if (lexer_peek(lexer).type != TOKEN_RPAREN)
				do {
					if (lexer_peek(lexer).type == TOKEN_COMMA) lexer_advance(lexer);
					ast_pushback(call, ast_parse_expr_assignment(arena, lexer));
				} while (lexer_match(lexer, TOKEN_COMMA, 0));

			lexer_consume(lexer, TOKEN_RPAREN, s("Expect ')' after arguments."));
			result = call;
		}
	}

	return result;
}

AST_Node *ast_parse_expr_assignment(Arena *arena, Lexer *lexer) {
	AST_Node *left = 0;

	bool ok = arena && lexer;
	if (ok) {
		Token s = lexer->current;

		left = ast_parse_expr_call(arena, lexer);

		if (lexer_match(lexer, TOKEN_EQUAL, 0)) {
			AST_Node *value = ast_parse_expr_assignment(arena, lexer);

			if (left->type == AST_NODE_EXPR_VARIABLE) {
				AST_Node *assign = ast_make(arena, AST_NODE_EXPR_ASSIGN);
				ast_pushback(assign, left);
				ast_pushback(assign, value);

				left = assign;
			} else {
				string8 where = lexer_error_location_string(arena, s);
				LOG_ERROR("#Invalid l-value for assignment\n%.*s", arg8(where));
			}
		}
	}

	return left;
}

AST_Node *ast_parse_expr(Arena *arena, Lexer *lexer) {
	return ast_parse_expr_assignment(arena, lexer);
}

AST_Node *ast_parse_pipeline_decl(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		result = ast_make(arena, AST_NODE_PIPELINE_DECL);
		if (lexer_peek(lexer).type == TOKEN_IDENTIFIER) result->identifier = lexer_consume(lexer, TOKEN_IDENTIFIER, s(""));

		lexer_consume(lexer, TOKEN_LPAREN, s("Expect '(' after pipeline declaration."));
		do {
            if (lexer_peek(lexer).type == TOKEN_RPAREN) break;

			ast_pushback(result, ast_parse_expr(arena, lexer));
		} while (lexer_match(lexer, TOKEN_COMMA, 0));

		lexer_consume(lexer, TOKEN_RPAREN, s("Expect ')' after pipeline definition."));
	}

	return result;
}

string8 ast_parse_block(Lexer *lexer) {
	lexer_consume(lexer, TOKEN_LBRACE, s("Expect '{' after glsl block declaration."));

	uint8_t *start = lexer->cursor, *end = lexer->cursor;
	int32_t lbrace = 1;
	while (lexer_at_end(lexer) == false) {
		Token peek = lexer_peek(lexer);

		if (peek.type == TOKEN_LBRACE) lbrace++;
		if (peek.type == TOKEN_RBRACE) lbrace--;

		if (lbrace == 0) break;

		lexer_advance(lexer);
		end = lexer->cursor;
	}
	lexer_consume(lexer, TOKEN_RBRACE, s("Expect '}' after glsl block declaration."));

	return str8_range((char *)start, (char *)end);
}

AST_Node *ast_block(Arena *arena, AST_BlockKind kind, string8 source) {
	AST_Node *result = 0;

	bool ok = arena;
	if (ok) {
		result = ast_make(arena, AST_NODE_SOURCE_DECL);
		result->block_kind = kind;
		result->glsl = source;
	}

	return result;
}

AST_Node *ast_parse_block_decl(Arena *arena, Lexer *lexer, AST_BlockKind kind) {
	ArenaTemp scratch = arena_scratch_begin(arena);

	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		Token tok = lexer_peek(lexer);

		if (tok.type == TOKEN_LBRACE) {
			result = ast_make(arena, AST_NODE_SOURCE_DECL);
			result->block_kind = kind;
			result->glsl = ast_parse_block(lexer);
		} else if (tok.type == TOKEN_IDENTIFIER) {
			Token identifier = lexer_consume(
				lexer,
				TOKEN_IDENTIFIER,
				s("Expect shader identifier."));

			lexer_consume(
				lexer,
				TOKEN_SEMICOLON,
				s("Expect ';' after shader reference."));

			result = ast_make(arena, AST_NODE_SOURCE_REF);
			result->block_kind = kind;
			result->identifier = identifier;
		} else {
			string8 message = fmt8(scratch.arena, "Expected block or shader reference, got '%.s'.", arg8(token_type_to_string[tok.type]));
			report(tok.line, tok.column, lexer_error_location_string(scratch.arena, tok), message);
			lexer_advance(lexer);
		}
	}

	return result;
}

AST_Node *ast_parse_shader_decl(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		result = ast_make(arena, AST_NODE_SHADER);
		result->identifier = lexer_consume(lexer, TOKEN_IDENTIFIER, s("Expect identifier after shader declaration."));

		AST_NodeList *symbol = arena_push_count(arena, AST_NodeList, 1);

		symbol->node = result;
		symbol->next = shader_symbol_list;
		shader_symbol_list = symbol;

		lexer_consume(lexer, TOKEN_LBRACE, s("Expect '{' after shader declaration."));
		while (lexer_at_end(lexer) == false && lexer_match(lexer, TOKEN_RBRACE, 0) == false) {
			if (lexer_match(lexer, keyword_token(KEYWORD_PIPELINE), 0)) {
				ast_pushback(result, ast_parse_pipeline_decl(arena, lexer));
			} else if (lexer_match(lexer, keyword_token(KEYWORD_SHARED), 0)) {
				ast_pushback(result, ast_parse_block_decl(arena, lexer, AST_BLOCK_KIND_SHARED));
			} else if (lexer_match(lexer, keyword_token(KEYWORD_VERTEX), 0))
				ast_pushback(result, ast_parse_block_decl(arena, lexer, AST_BLOCK_KIND_VERTEX));
			else if (lexer_match(lexer, keyword_token(KEYWORD_FRAGMENT), 0))
				ast_pushback(result, ast_parse_block_decl(arena, lexer, AST_BLOCK_KIND_FRAGMENT));
			else if (lexer_match(lexer, keyword_token(KEYWORD_COMPUTE), 0))
				ast_pushback(result, ast_parse_block_decl(arena, lexer, AST_BLOCK_KIND_COMPUTE));
			else {
				LOG_ERROR("#Unexpected token '%.*s'.\n%.*s", arg8(lexer_peek(lexer).lexeme), arg8(lexer_error_location_string(arena, lexer_peek(lexer))));
				lexer_advance(lexer);
			}
		}
	}

	return result;
}

void ast_visit(AST_Node *node, uint32_t indent_level) {
	for (uint32_t index = 0; index < indent_level; ++index)
		printf("  ");

	if (node)
		switch (node->type) {
			case AST_NODE_PROGRAM: {
				AST_Node *shader = node->first_child;
				if (shader) do {
						ast_visit(shader, indent_level);
						shader = shader->next_sibling;
					} while (shader != node->first_child);
			} break;
			case AST_NODE_SHADER: {
				printf("SHADER (%.*s)\n", arg8(node->identifier.lexeme));
				AST_Node *decl = node->first_child;
				if (decl) do {
						ast_visit(decl, indent_level + 1);
						decl = decl->next_sibling;
					} while (decl != node->first_child);
			} break;
			case AST_NODE_SOURCE_REF:
			case AST_NODE_SOURCE_DECL: {
				string8 table[AST_BLOCK_KIND_MAX] = {
					[AST_BLOCK_KIND_SHARED] = s("SHARED_DECL"),
					[AST_BLOCK_KIND_VERTEX] = s("VERTEX_DECL"),
					[AST_BLOCK_KIND_FRAGMENT] = s("FRAGMENT_DECL"),
					[AST_BLOCK_KIND_COMPUTE] = s("COMPUTE_DECL"),
				};
				bool is_ref = node->type == AST_NODE_SOURCE_REF;
				string8 ref = is_ref ? node->identifier.lexeme : s("");
				string8 open = is_ref ? s("(") : s("");
				string8 close = is_ref ? s(")") : s("");

				printf("%.*s%.*s%.*s%.*s\n", arg8(table[node->block_kind]), arg8(open), arg8(ref), arg8(close));
			} break;
			case AST_NODE_PIPELINE_DECL:
				printf("PIPELINE_DECL(%.*s)\n", arg8(node->identifier.lexeme));
				AST_Node *stmt = node->first_child;
				if (stmt) do {
						ast_visit(stmt, indent_level + 1);
						stmt = stmt->next_sibling;
					} while (stmt != node->first_child);
				break;
			case AST_NODE_EXPR_ASSIGN:
				printf("ASSIGN\n");
				ast_visit(node->first_child, indent_level + 1);
				ast_visit(node->last_child, indent_level + 1);
				break;
			case AST_NODE_EXPR_CALL:
				printf("CALL(%.*s)\n", arg8(node->identifier.lexeme));
				ast_visit(node->first_child, indent_level + 1);
				AST_Node *args = node->first_child->next_sibling;
				while (args != node->first_child) {
					ast_visit(args, indent_level + 1);
					args = args->next_sibling;
				}
				break;
			case AST_NODE_EXPR_VARIABLE:
				printf("VARIABLE(%.*s)\n", arg8(node->identifier.lexeme));
				break;
			default:
				break;
		}
}

// clang-format off

static const string8 blend_op_table[] = {
	[BLEND_OP_ADD]         = comp8("add"),
	[BLEND_OP_SUB]         = comp8("sub"),
	[BLEND_OP_REVERSE_SUB] = comp8("rsub"),
	[BLEND_OP_MIN]         = comp8("min"),
	[BLEND_OP_MAX]         = comp8("max"),
};

static const string8 blend_factor_table[BLEND_FACTOR_MAX] = {
	[BLEND_FACTOR_ZERO]                     = comp8("zero"),
	[BLEND_FACTOR_ONE]                      = comp8("one"),
	[BLEND_FACTOR_SRC_COLOR]                = comp8("src_color"),
	[BLEND_FACTOR_ONE_MINUS_SRC_COLOR]      = comp8("one_minus_src_color"),
	[BLEND_FACTOR_DST_COLOR]                = comp8("dst_color"),
	[BLEND_FACTOR_ONE_MINUS_DST_COLOR]      = comp8("one_minus_dst_color"),
	[BLEND_FACTOR_SRC_ALPHA]                = comp8("src_alpha"),
	[BLEND_FACTOR_ONE_MINUS_SRC_ALPHA]      = comp8("one_minus_src_alpha"),
	[BLEND_FACTOR_DST_ALPHA]                = comp8("dst_alpha"),
	[BLEND_FACTOR_ONE_MINUS_DST_ALPHA]      = comp8("one_minus_dst_alpha"),
};

static const string8 cull_mode_table[CULL_MODE_MAX] = {
	[CULL_MODE_NONE]           = comp8("none"),
	[CULL_MODE_FRONT]          = comp8("front"),
	[CULL_MODE_BACK]           = comp8("back"),
	[CULL_MODE_FRONT_AND_BACK] = comp8("front_and_back"),
};
// clang-format on

int32_t eval_pipeline_state(const string8 *state_table, uint32_t table_count, string8 needle, const char *fmt, ...) {
	int32_t result = -1;

	bool ok = state_table && table_count;
	if (ok) {
		for (uint32_t index = 0; index < table_count; ++index) {
			if (eq8(state_table[index], needle)) {
				result = index;
				break;
			}
		}

		ok = result != -1;
	}

	if (ok == false) {
		ArenaTemp scratch = arena_scratch_begin(0);
		va_list args;
		va_start(args, fmt);
		string8 err = fmtv8(scratch.arena, fmt, args);
		va_end(args);

		LOG_ERROR("%.*s", arg8(err));
		arena_scratch_end(scratch);
	}

	return result;
}

bool ast_pipeline_eval(AST_Node *pipeline, PipelineOptions *opts) {
	ArenaTemp scratch = arena_scratch_begin(0);

	bool ok = pipeline && opts;

	if (ok) {
		string8 name = pipeline->identifier.lexeme.length
			? pipeline->identifier.lexeme
			: s("unnamed");

		AST_Node *assign = pipeline->first_child;
		do {
			if (assign == 0) break;

			ok = assign->type == AST_NODE_EXPR_ASSIGN;
			if (ok == false) {
				Token err = assign->identifier;
				report(err.line, err.column, lexer_error_location_string(scratch.arena, err), s("Expected '=' after pipeline property."));
				break;
			}

			Token tok = assign->first_child->identifier;
			string8 key = tok.lexeme;

			bool is_blend = eq8(key, s("blend"));
			bool is_color = is_blend || eq8(key, s("blend_color"));
			bool is_alpha = is_blend || eq8(key, s("blend_alpha"));

			if (is_color || is_alpha) {
				AST_Node *call = assign->last_child;
				ok = call && call->type == AST_NODE_EXPR_CALL;
				if (ok == false) {
					string8 message = fmt8(scratch.arena,
						"Expect function call value for '%.*s'",
						arg8(key));
					report(tok.line, tok.column, lexer_error_location_string(scratch.arena, tok), message);
					break;
				}

				int32_t op = eval_pipeline_state(blend_op_table, countof(blend_op_table), call->identifier.lexeme,
					"Pipeline '%.*s': unknown blend operation '%.*s'", arg8(name), arg8(call->identifier.lexeme));
				ok &= op != -1;

				AST_Node *src_node = call->first_child;
				AST_Node *dst_node = src_node ? src_node->next_sibling : 0;

				ok &= src_node && dst_node && src_node != dst_node;
				if (ok == false) {
					string8 message = fmt8(
						scratch.arena,
						"Expect two arguments for blend operation '%.*s'.",
						arg8(key));
					report(tok.line, tok.column, lexer_error_location_string(scratch.arena, tok), message);
					break;
				}

				int32_t src = eval_pipeline_state(blend_factor_table, countof(blend_factor_table), src_node->identifier.lexeme,
					"Pipeline '%.*s': unknown source blend factor '%.*s'", arg8(name), arg8(src_node->identifier.lexeme));

				int32_t dst = eval_pipeline_state(blend_factor_table, countof(blend_factor_table), dst_node->identifier.lexeme,
					"Pipeline '%.*s': unknown destination blend factor '%.*s'", arg8(name), arg8(dst_node->identifier.lexeme));

				ok &= src != -1;
				ok &= dst != -1;

				if (ok) {
					opts->blend_enable = true;

					if (is_blend || is_color) {
						opts->color_op = (BlendOp)op;
						opts->src_color_factor = (BlendFactor)src;
						opts->dst_color_factor = (BlendFactor)dst;
					}

					if (is_blend || is_alpha) {
						opts->alpha_op = (BlendOp)op;
						opts->src_alpha_factor = (BlendFactor)src;
						opts->dst_alpha_factor = (BlendFactor)dst;
					}
				}
			} else if (eq8(key, s("cull")) || eq8(key, s("cull_mode"))) {
				AST_Node *value_node = assign->last_child;

				ok = value_node;
				if (ok == false) {
					LOG_ERROR("Pipeline '%.*s': cull requires a value", arg8(name));
					break;
				}

				CullMode value = CULL_MODE_MAX;

				for (uint32_t index = 0; index < countof(cull_mode_table); ++index) {
					if (eq8(cull_mode_table[index], value_node->identifier.lexeme)) {
						value = (CullMode)index;
						break;
					}
				}

				ok = value != CULL_MODE_MAX;
				if (ok == false) {
					LOG_ERROR("Pipeline '%.*s': unknown cull mode '%.*s'", arg8(name), arg8(value_node->identifier.lexeme));
					break;
				}

				opts->cull_mode = value;
			} else if (eq8(key, s("depth_test")) || eq8(key, s("depth_write"))) {
				AST_Node *value_node = assign->last_child;

				ok = value_node;
				if (ok == false) {
					LOG_ERROR("Pipeline '%.*s': depth_test requires a value", arg8(name));
					break;
				}

				struct {
					string8 key;
					bool value;
				} depth_table[] = {
					{ s("true"), true },
					{ s("false"), false },
					{ s("1"), true },
					{ s("0"), false }
				};

				int32_t found = -1;
				for (uint32_t index = 0; index < countof(depth_table); ++index) {
					if (eq8(depth_table[index].key, value_node->identifier.lexeme)) {
						found = index;
						break;
					}
				}

				ok = found != -1;
				if (ok == false) {
					string8 message = fmt8(
						scratch.arena,
						"Expect boolean for '%.*s', got '%.*s'",
						arg8(key),
						arg8(value_node->identifier.lexeme));

					tok = value_node->identifier;
					report(tok.line, tok.column, lexer_error_location_string(scratch.arena, tok), message);
					break;
				}

				if (eq8(key, s("depth_test")))
					opts->disable_depth_test = !depth_table[found].value;
				else
					opts->disable_depth_write = !depth_table[found].value;
			} else if (eq8(key, s("polygon")) || eq8(key, s("polygon_mode"))) {
				AST_Node *value_node = assign->last_child;

				ok = value_node;
				if (ok == false) {
					LOG_ERROR("Pipeline '%.*s': polygon requires a value", arg8(name));
					break;
				}

				PolygonMode value = POLYGON_MODE_MAX;

				for (uint32_t index = 0; index < countof(cull_mode_table); ++index) {
					if (eq8(cull_mode_table[index], value_node->identifier.lexeme)) {
						value = (PolygonMode)index;
						break;
					}
				}

				ok = value != POLYGON_MODE_MAX;
				if (ok == false) {
					LOG_ERROR("Pipeline '%.*s': unknown polygon mode '%.*s'", arg8(name), arg8(value_node->identifier.lexeme));
					break;
				}

				opts->polygon_mode = value;
			} else {
				string8 message = fmt8(
					scratch.arena,
					"Unknown pipeline property '%.*s'.",
					arg8(tok.lexeme));
				report(tok.line, tok.column, lexer_error_location_string(scratch.arena, tok), message);

				ok = false;
				break;
			}

			assign = assign->next_sibling;
		} while (assign != pipeline->first_child);
	}

	arena_scratch_end(scratch);
	return ok;
}

static AST_Node *ast_lookup_shader(string8 name) {
	AST_Node *result = 0;

	for (AST_NodeList *it = shader_symbol_list; it; it = it->next) {
		AST_Node *shader = it->node;

		if (eq8(shader->identifier.lexeme, name)) {
			result = shader;
			break;
		}
	}

	return result;
}

static bool ast_in_list(AST_NodeList *list, AST_Node *node) {
	bool result = false;
	for (AST_NodeList *it = list; it; it = it->next) {
		if (node == it->node) {
			result = true;
			break;
		}
	}
	return result;
}

static AST_Node *ast_find_stage_in_shader(AST_Node *shader, AST_BlockKind kind) {
	AST_Node *result = 0;

	bool ok = shader && shader->first_child;
	if (ok) {
		AST_Node *it = shader->first_child;
		do {
			if ((it->type == AST_NODE_SOURCE_DECL || it->type == AST_NODE_SOURCE_REF) &&
				it->block_kind == kind) {
				result = it;
				break;
			}

			it = it->next_sibling;
		} while (it != shader->first_child);
	}

	return result;
}

bool ast_resolve_ref(AST_Node *node, AST_NodeList *explored) {
	ArenaTemp scratch = arena_scratch_begin(0);

	bool ok = node;
	if (ok) {
		switch (node->type) {
			case AST_NODE_PROGRAM:
			case AST_NODE_SHADER: {
				AST_Node *it = node->first_child;
				if (it) {
					do {
						ok &= ast_resolve_ref(it, 0);
						it = it->next_sibling;
					} while (it != node->first_child);
				}
			} break;

			case AST_NODE_SOURCE_REF: {
				Token tok = node->identifier;
				string8 ref_name = node->identifier.lexeme;

				ok = ast_in_list(explored, node) == false;
				if (ok == false) {
					string8 cycle = { 0 };
					for (AST_NodeList *it = explored; it; it = it->next) {
						AST_Node *shader = it->node->parent;
						string8 shader_name = shader->identifier.lexeme;

						if (cycle.length)
							cycle = fmt8(scratch.arena, "%.*s -> %.*s", arg8(cycle), arg8(shader_name));
						else
							cycle = shader_name;
					}

					string8 message = fmt8(
						scratch.arena,
						"Cyclic dependency detected in stage %.*s for shader '%.*s'. %.*s",
						arg8(ast_block_kind_to_display_string[node->block_kind]),
						arg8(ref_name),
						arg8(cycle));
					report(tok.line, tok.column, lexer_error_location_string(scratch.arena, tok), message);
					break;
				}

				AST_Node *target_shader = ast_lookup_shader(ref_name);
				ok = target_shader != 0;
				if (ok == false) {
					string8 message = fmt8(scratch.arena, "Referenced shader '%.*s' was not declared.", arg8(ref_name));
					report(tok.line, tok.column, lexer_error_location_string(scratch.arena, tok), message);
					break;
				}

				AST_Node *target_stage = ast_find_stage_in_shader(target_shader, node->block_kind);
				ok = target_stage != 0;
				if (ok == false) {
					string8 message = fmt8(
						scratch.arena,
						"Shader '%.*s' does not define referenced stage %.*s.\n",
						arg8(ref_name),
						arg8(ast_block_kind_to_display_string[node->block_kind]));
					report(tok.line, tok.column, lexer_error_location_string(scratch.arena, tok), message);
					break;
				}

				AST_NodeList *cur = arena_push_count(scratch.arena, AST_NodeList, 1);

				cur->node = node;
				cur->next = explored;
				explored = cur;

				ok = ast_resolve_ref(target_stage, explored);
				if (ok == false) {
					break;
				}

				node->type = AST_NODE_SOURCE_DECL;
				node->glsl = target_stage->glsl;
			} break;

			default:
				break;
		}
	}

	arena_scratch_end(scratch);
	return ok;
}

#define stage_index(k) ast_block_kind_to_shader_stage[(k)]
int main(int32_t argc, char **argv) {
	Arena arena[] = { arena_make(MiB(32)) };

	string8 ref_directory = argc > 1 ? str8z(argv[1]) : s("./");
	string8 shader_directory = pathjoin8(arena, ref_directory, s("assets/shaders/"));

	uint32_t file_count;
	string8 *files = os_directory_files(arena, shader_directory, &file_count);

	bool had_error = false;

	AST_Node *program = ast_make(arena, AST_NODE_PROGRAM);
	for (uint32_t index = 0; index < file_count; ++index) {
		if (eq8(ext8(files[index]), s("shader")) == false) continue;
		string8 file = concat8(arena, shader_directory, files[index]);

		Lexer lexer[] = { lexer_make(os_file_read(arena, file), keyword_to_string, countof(keyword_to_string)) };
		while (lexer_at_end(lexer) == false) {
			if (lexer_match(lexer, TOKEN_KEYWORD_0 + KEYWORD_SHADER, 0))
				ast_pushback(program, ast_parse_shader_decl(arena, lexer));
			else
				lexer_advance(lexer);
		}

		if (lexer->had_error == true) return -1;
	}

	if (had_error) return -1;
	if (ast_resolve_ref(program, 0) == false) had_error = true;

	// TODO: Explict order
	/*
	 * parse();
	 * collect_and_validate();
	 *
	 * emit_glsl();
	 * compile_glsl();
	 *
	 * emit_header();
	 * emit_source();
	 */

	string8 code_output_directory = pathjoin8(arena, ref_directory, s("game/src/generated"));
	if (os_directory_exists(code_output_directory) == false)
		os_directory_make(code_output_directory);
	FILE *header = fopen((char *)pathjoin8(arena, code_output_directory, s("assets_generated.h")).bytes, "w");
	FILE *source = fopen((char *)pathjoin8(arena, code_output_directory, s("assets_generated.c")).bytes, "w");

	if (header == 0 || source == 0) return -1;

	string8 output_directory = pathjoin8(arena, ref_directory, s("assets/shaders/generated"));
	if (os_directory_exists(output_directory) == false)
		if (os_directory_make(output_directory) == false) return -1;

	fprintf(header, "#pragma once\n");
	fprintf(header, "#include \"core/strings.h\"\n");
	fprintf(header, "#include \"gfx/gfx_types.h\"\n\n");

	fprintf(header, "typedef struct {\n");
	fprintf(header, "    string8 name;\n");
	fprintf(header, "    string8 filepaths[SHADER_STAGE_MAX];\n");
	fprintf(header, "    PipelineOptions pipelines[8];\n");
	fprintf(header, "    uint32_t pipeline_count;\n");
	fprintf(header, "} ShaderMetadata;\n\n");

	fprintf(header, "typedef enum {\n");
	AST_Node *shader = program->first_child;
	if (shader) do {
			string8 name = shader->identifier.lexeme;
			string8 name_upper = upper8(arena, name);
			fprintf(header, "    RES_SHADER_%.*s,\n", arg8(name_upper));

			string8 glsl_paths[SHADER_STAGE_MAX] = {
				[SHADER_STAGE_VERTEX] = fmt8(arena, "%.*s/v_%.*s.glsl", arg8(output_directory), arg8(name)),
				[SHADER_STAGE_FRAGMENT] = fmt8(arena, "%.*s/f_%.*s.glsl", arg8(output_directory), arg8(name)),
				[SHADER_STAGE_COMPUTE] = fmt8(arena, "%.*s/c_%.*s.glsl", arg8(output_directory), arg8(name)),
			};

			string8 spv_paths[SHADER_STAGE_MAX] = {
				[SHADER_STAGE_VERTEX] = fmt8(arena, "%.*s/v_%.*s.spv", arg8(output_directory), arg8(name)),
				[SHADER_STAGE_FRAGMENT] = fmt8(arena, "%.*s/f_%.*s.spv", arg8(output_directory), arg8(name)),
				[SHADER_STAGE_COMPUTE] = fmt8(arena, "%.*s/c_%.*s.spv", arg8(output_directory), arg8(name)),
			};

			string8 blocks[AST_BLOCK_KIND_MAX] = { 0 };

			uint32_t pipeline_count = 0;

			AST_Node *decl = shader->first_child;
			if (decl) do {
					if (decl->type == AST_NODE_SOURCE_DECL)
						blocks[decl->block_kind] = concat8(arena, blocks[decl->block_kind], decl->glsl);
					else if (decl->type == AST_NODE_PIPELINE_DECL) {
						string8 pipeline_name = decl->identifier.lexeme;
						string8 pipeline_name_upper = upper8(arena, pipeline_name);
						fprintf(header, "#define PIPELINE_%.*s_%.*s %u\n", arg8(name_upper), arg8(pipeline_name_upper), pipeline_count++);
					}

					decl = decl->next_sibling;
				} while (decl != shader->first_child);

			if (pipeline_count == 0)
				fprintf(header, "#define PIPELINE_%.*s_%s %u\n", arg8(name_upper), "DEFAULT", pipeline_count++);

			string8 version_header = s("#version 450 core\n");
			string8 stage_info[SHADER_STAGE_MAX] = {
				[SHADER_STAGE_VERTEX] = s(
					"#pragma shader_stage(vertex)\n\n"
					"#define INOUT out\n"),
				[SHADER_STAGE_FRAGMENT] = s(
					"#pragma shader_stage(fragment)\n\n"
					"#define INOUT in\n"),
				[SHADER_STAGE_COMPUTE] = s("#pragma shader_stage(compute)"),
			};
			string8 include_dir = s("assets/shaders/");
			string8 cleaned_shared = dedent8(arena, blocks[AST_BLOCK_KIND_SHARED]);

			for (AST_BlockKind block_index = 0; block_index < AST_BLOCK_KIND_MAX; ++block_index) {
				if (block_index == AST_BLOCK_KIND_SHARED) continue;
				if (blocks[block_index].length == 0) continue;
				string8 cleaned_source = dedent8(arena, blocks[block_index]);

				string8 glsl_path = glsl_paths[stage_index(block_index)];
				string8 spv_path = spv_paths[stage_index(block_index)];

				string8 glsl = fmt8(arena,
					"%.*s%.*s\n// --- shared_start ---\n%.*s\n\n// --- shared_end ---\n\n// --- source_start ---\n%.*s\n\n// --- source_end ---",
					arg8(version_header),
					arg8(stage_info[stage_index(block_index)]),
					arg8(cleaned_shared),
					arg8(cleaned_source) //
				);
				bool glsl_modified = true;

				if (os_file_exists(glsl_path)) {
					string8 existing = os_file_read(arena, glsl_path);

					if (eq8(existing, glsl))
						glsl_modified = false;
				}

				if (glsl_modified || os_file_exists(spv_path) == false) {
					string8 tmp_path = fmt8(arena, "%.*s.tmp.vert", arg8(glsl_path));
					os_file_write(tmp_path, glsl.bytes, glsl.length);

					string8 cmd = fmt8(
						arena,
						"glslc -I %.*s %.*s -o %.*s",
						arg8(include_dir),
						arg8(tmp_path),
						arg8(spv_path));

					LOG_INFO("#Generating %.*s", arg8(spv_path));
					if (os_execute_command(cmd) == 0)
						os_file_write(glsl_path, glsl.bytes, glsl.length);

					os_file_delete(tmp_path);
				}
			}

			shader = shader->next_sibling;
		} while (shader != program->first_child);
	fprintf(header, "\n    RES_SHADER_MAX\n");
	fprintf(header, "} RES_ShaderID;\n\n");

	fprintf(header, "extern ShaderMetadata res_shaderid_to_metadata[RES_SHADER_MAX];\n");

	fprintf(source, "#include \"assets_generated.h\"\n\n");
	fprintf(source, "ShaderMetadata res_shaderid_to_metadata[RES_SHADER_MAX] = {\n");
	if (shader) do {
			string8 name = shader->identifier.lexeme;
			string8 upper = upper8(arena, name);

			string8 filepaths[SHADER_STAGE_MAX] = {
				[SHADER_STAGE_VERTEX] = fmt8(arena, "%.*s/v_%.*s.spv", arg8(output_directory), arg8(name)),
				[SHADER_STAGE_FRAGMENT] = fmt8(arena, "%.*s/f_%.*s.spv", arg8(output_directory), arg8(name)),
				[SHADER_STAGE_COMPUTE] = fmt8(arena, "%.*s/c_%.*s.spv", arg8(output_directory), arg8(name)),
			};

			bool is_compute = false;

			uint32_t pipeline_count = 0;
			AST_Node *decl = shader->first_child;
			if (decl) do {
					if (decl->type == AST_NODE_SOURCE_DECL && decl->block_kind == AST_BLOCK_KIND_COMPUTE)
						is_compute = true;
					if (decl->type == AST_NODE_PIPELINE_DECL) pipeline_count++;

					decl = decl->next_sibling;
				} while (decl != shader->first_child);

			ASSERT_FORMAT((is_compute && pipeline_count) == false && pipeline_count < 8, "Shader '%.*s': compute and graphics pipeline defined together.", arg8(shader->identifier.lexeme));

			fprintf(source, "    [RES_SHADER_%.*s] = {\n", arg8(upper));
			fprintf(source, "      .name = comp8(\"%.*s\"),\n", arg8(name));
			fprintf(source, "      .filepaths = {\n");
			if (is_compute)
				fprintf(source, "          [SHADER_STAGE_COMPUTE] = comp8(\"%.*s\"),\n", arg8(filepaths[SHADER_STAGE_COMPUTE]));
			else {
				fprintf(source, "          [SHADER_STAGE_VERTEX] = comp8(\"%.*s\"),\n", arg8(filepaths[SHADER_STAGE_VERTEX]));
				fprintf(source, "          [SHADER_STAGE_FRAGMENT] = comp8(\"%.*s\"),\n", arg8(filepaths[SHADER_STAGE_FRAGMENT]));
			}
			fprintf(source, "      },\n");

			if (is_compute == false) {
				fprintf(source, "      .pipelines = {\n");

				if (decl && pipeline_count) do {
						PipelineOptions options = { 0 };
						options.src_color_factor = BLEND_FACTOR_ONE;
						options.src_alpha_factor = BLEND_FACTOR_ONE;

						if (decl->type == AST_NODE_PIPELINE_DECL) {
							ast_pipeline_eval(decl, &options);

							string8 pipeline_name = decl->identifier.lexeme;
							string8 pipeline_upper = upper8(arena, pipeline_name);

							fprintf(source, "          [PIPELINE_%.*s_%.*s] = {\n", arg8(upper), arg8(pipeline_upper)); // TODO: PipelineID
							fprintf(source, "              .cull_mode = %.*s,\n", arg8(cull_mode_to_string[options.cull_mode]));
							fprintf(source, "              .polygon_mode = %.*s,\n", arg8(polygon_mode_to_string[options.polygon_mode]));
							fprintf(source, "              .disable_depth_test = %s,\n", options.disable_depth_test ? "true" : "false");
							fprintf(source, "              .disable_depth_write = %s,\n", options.disable_depth_write ? "true" : "false");
							fprintf(source, "              .blend_enable = %s,\n", options.blend_enable ? "true" : "false");
							fprintf(source, "              .color_op = %.*s,\n", arg8(blend_op_to_string[options.color_op]));
							fprintf(source, "              .alpha_op = %.*s,\n", arg8(blend_op_to_string[options.alpha_op]));
							fprintf(source, "              .src_color_factor = %.*s,\n", arg8(blend_factor_to_string[options.src_color_factor]));
							fprintf(source, "              .dst_color_factor = %.*s,\n", arg8(blend_factor_to_string[options.dst_color_factor]));
							fprintf(source, "              .src_alpha_factor = %.*s,\n", arg8(blend_factor_to_string[options.src_alpha_factor]));
							fprintf(source, "              .dst_alpha_factor = %.*s,\n", arg8(blend_factor_to_string[options.dst_alpha_factor]));
							fprintf(source, "          },\n");
						}

						decl = decl->next_sibling;
					} while (decl != shader->first_child);

				if (pipeline_count == 0) {
					pipeline_count = 1;
					PipelineOptions options = { 0 };
					options.src_color_factor = BLEND_FACTOR_ONE;
					options.src_alpha_factor = BLEND_FACTOR_ONE;

					fprintf(source, "          [PIPELINE_%.*s_%s] = {\n", arg8(upper), "DEFAULT");
					fprintf(source, "              .cull_mode = %.*s,\n", arg8(cull_mode_to_string[options.cull_mode]));
					fprintf(source, "              .polygon_mode = %.*s,\n", arg8(polygon_mode_to_string[options.polygon_mode]));
					fprintf(source, "              .disable_depth_test = %s,\n", options.disable_depth_test ? "true" : "false");
					fprintf(source, "              .disable_depth_write = %s,\n", options.disable_depth_write ? "true" : "false");
					fprintf(source, "              .blend_enable = %s,\n", options.blend_enable ? "true" : "false");
					fprintf(source, "              .color_op = %.*s,\n", arg8(blend_op_to_string[options.color_op]));
					fprintf(source, "              .alpha_op = %.*s,\n", arg8(blend_op_to_string[options.alpha_op]));
					fprintf(source, "              .src_color_factor = %.*s,\n", arg8(blend_factor_to_string[options.src_color_factor]));
					fprintf(source, "              .dst_color_factor = %.*s,\n", arg8(blend_factor_to_string[options.dst_color_factor]));
					fprintf(source, "              .src_alpha_factor = %.*s,\n", arg8(blend_factor_to_string[options.src_alpha_factor]));
					fprintf(source, "              .dst_alpha_factor = %.*s,\n", arg8(blend_factor_to_string[options.dst_alpha_factor]));
					fprintf(source, "          },\n");
				}

				fprintf(source, "      },\n");
				fprintf(source, "      .pipeline_count = %u,\n", pipeline_count);
			}

			fprintf(source, "    },\n");

			shader = shader->next_sibling;
		} while (shader != program->first_child);
	fprintf(source, "};\n\n");

	fclose(header);
	fclose(source);
}
