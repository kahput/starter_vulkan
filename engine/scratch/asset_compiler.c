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

static const String8 keyword_to_string[KEYWORD_MAX] = {
#define X(symbol, display) [KEYWORD_##symbol] = scomp(display),
	KEYWORD_LIST
#undef X
};

typedef enum {
	AST_NODE_PROGRAM,
	AST_NODE_SHADER,

	AST_NODE_SHARED_DECL,
	AST_NODE_VERTEX_DECL,
	AST_NODE_FRAGMENT_DECL,
	AST_NODE_COMPUTE_DECL,
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

	Token identifier;
	String8 code;
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

String8 ast_parse_glsl_block(Lexer *lexer) {
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

	return str8_from_ends((char *)start, (char *)end);
}

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
				String8 message = str8_pushf(
					scratch.arena,
					s("Unexpected token '%.*s'"),
					sspread(tok.lexeme));

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
				String8 where = lexer_error_location_string(arena, s);
				LOG_ERROR("#Invalid l-value for assignment\n%.*s", sspread(where));
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

		lexer_consume(lexer, TOKEN_LBRACE, s("Expect '{' after pipeline declaration."));
		while (lexer_peek(lexer).type != TOKEN_RBRACE) {
			ast_pushback(result, ast_parse_expr(arena, lexer));
			lexer_consume(lexer, TOKEN_SEMICOLON, s("Expect ';' after statement"));
		}
		lexer_consume(lexer, TOKEN_RBRACE, s("Expect '}' after pipeline definition."));
	}

	return result;
}

AST_Node *ast_parse_shared_decl(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		result = ast_make(arena, AST_NODE_SHARED_DECL);

		lexer_consume(lexer, TOKEN_LBRACE, s("Expect '{' after shared declaration."));
		result->code = ast_parse_glsl_block(lexer);
		lexer_consume(lexer, TOKEN_RBRACE, s("Expect '}' after shared glsl block."));
	}

	return result;
}

AST_Node *ast_parse_vertex_decl(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		result = ast_make(arena, AST_NODE_VERTEX_DECL);

		lexer_consume(lexer, TOKEN_LBRACE, s("Expect '{' after vertex declaration."));
		result->code = ast_parse_glsl_block(lexer);
		lexer_consume(lexer, TOKEN_RBRACE, s("Expect '}' after vertex glsl block."));
	}

	return result;
}

AST_Node *ast_parse_fragment_decl(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		result = ast_make(arena, AST_NODE_FRAGMENT_DECL);

		lexer_consume(lexer, TOKEN_LBRACE, s("Expect '{' after fragment declaration."));
		result->code = ast_parse_glsl_block(lexer);
		lexer_consume(lexer, TOKEN_RBRACE, s("Expect '}' after fragment glsl block."));
	}

	return result;
}

AST_Node *ast_parse_compute_decl(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		result = ast_make(arena, AST_NODE_COMPUTE_DECL);

		lexer_consume(lexer, TOKEN_LBRACE, s("Expect '{' after compute declaration."));
		result->code = ast_parse_glsl_block(lexer);
		lexer_consume(lexer, TOKEN_RBRACE, s("Expect '}' after compute glsl block."));
	}

	return result;
}

AST_Node *ast_parse_shader_decl(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		result = ast_make(arena, AST_NODE_SHADER);
		result->identifier = lexer_consume(lexer, TOKEN_IDENTIFIER, s("Expect identifier after shader declaration."));

		lexer_consume(lexer, TOKEN_LBRACE, s("Expect '{' after shader declaration."));
		while (lexer_at_end(lexer) == false && lexer_match(lexer, TOKEN_RBRACE, 0) == false) {
			if (lexer_match(lexer, keyword_token(KEYWORD_PIPELINE), 0))
				ast_pushback(result, ast_parse_pipeline_decl(arena, lexer));
			else if (lexer_match(lexer, keyword_token(KEYWORD_SHARED), 0))
				ast_pushback(result, ast_parse_shared_decl(arena, lexer));
			else if (lexer_match(lexer, keyword_token(KEYWORD_VERTEX), 0))
				ast_pushback(result, ast_parse_vertex_decl(arena, lexer));
			else if (lexer_match(lexer, keyword_token(KEYWORD_FRAGMENT), 0))
				ast_pushback(result, ast_parse_fragment_decl(arena, lexer));
			else if (lexer_match(lexer, keyword_token(KEYWORD_COMPUTE), 0))
				ast_pushback(result, ast_parse_compute_decl(arena, lexer));
			else {
				LOG_ERROR("#Unexpected token '%.*s'.\n%.*s", sspread(lexer_peek(lexer).lexeme), sspread(lexer_error_location_string(arena, lexer_peek(lexer))));
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
				printf("SHADER (%.*s)\n", sspread(node->identifier.lexeme));
				AST_Node *decl = node->first_child;
				if (decl) do {
						ast_visit(decl, indent_level + 1);
						decl = decl->next_sibling;
					} while (decl != node->first_child);
			} break;
			case AST_NODE_SHARED_DECL:
				printf("SHARED_DECL\n");
				break;
			case AST_NODE_VERTEX_DECL:
				printf("VERTEX_DECL\n");
				break;
			case AST_NODE_FRAGMENT_DECL:
				printf("FRAGMENT_DECL\n");
				break;
			case AST_NODE_COMPUTE_DECL:
				printf("COMPUTE_DECL(%luc)\n", node->code.length);
				break;
			case AST_NODE_PIPELINE_DECL:
				printf("PIPELINE_DECL(%.*s)\n", sspread(node->identifier.lexeme));
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
				printf("CALL(%.*s)\n", sspread(node->identifier.lexeme));
				ast_visit(node->first_child, indent_level + 1);
				AST_Node *args = node->first_child->next_sibling;
				while (args != node->first_child) {
					ast_visit(args, indent_level + 1);
					args = args->next_sibling;
				}
				break;
			case AST_NODE_EXPR_VARIABLE:
				printf("VARIABLE(%.*s)\n", sspread(node->identifier.lexeme));
				break;
			default:
				break;
		}
}

// clang-format off

static const String8 blend_op_table[] = {
	[BLEND_OP_ADD]         = scomp("add"),
	[BLEND_OP_SUB]         = scomp("sub"),
	[BLEND_OP_REVERSE_SUB] = scomp("rsub"),
	[BLEND_OP_MIN]         = scomp("min"),
	[BLEND_OP_MAX]         = scomp("max"),
};

static const String8 blend_factor_table[BLEND_FACTOR_MAX] = {
	[BLEND_FACTOR_ZERO]                     = scomp("zero"),
	[BLEND_FACTOR_ONE]                      = scomp("one"),
	[BLEND_FACTOR_SRC_COLOR]                = scomp("src_color"),
	[BLEND_FACTOR_ONE_MINUS_SRC_COLOR]      = scomp("one_minus_src_color"),
	[BLEND_FACTOR_DST_COLOR]                = scomp("dst_color"),
	[BLEND_FACTOR_ONE_MINUS_DST_COLOR]      = scomp("one_minus_dst_color"),
	[BLEND_FACTOR_SRC_ALPHA]                = scomp("src_alpha"),
	[BLEND_FACTOR_ONE_MINUS_SRC_ALPHA]      = scomp("one_minus_src_alpha"),
	[BLEND_FACTOR_DST_ALPHA]                = scomp("dst_alpha"),
	[BLEND_FACTOR_ONE_MINUS_DST_ALPHA]      = scomp("one_minus_dst_alpha"),
};

static const String8 cull_mode_table[CULL_MODE_MAX] = {
	[CULL_MODE_NONE]           = scomp("none"),
	[CULL_MODE_FRONT]          = scomp("front"),
	[CULL_MODE_BACK]           = scomp("back"),
	[CULL_MODE_FRONT_AND_BACK] = scomp("front_and_back"),
};
// clang-format on

int32_t eval_pipeline_state(const String8 *state_table, uint32_t table_count, String8 needle, String8 fmt, ...) {
	int32_t result = -1;

	bool ok = state_table && table_count;
	if (ok) {
		for (uint32_t index = 0; index < table_count; ++index) {
			if (str8_equals(state_table[index], needle)) {
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
		String8 err = str8_push_format_list(scratch.arena, fmt, args);
		va_end(args);

		LOG_ERROR("%.*s", sspread(err));
		arena_scratch_end(scratch);
	}

	return result;
}

String8 pipeline_state_keys[] = {
	scomp("cull_mode"),
	scomp("polygon_mode"),

	scomp("blend"),
	scomp("blend_color"),
	scomp("blend_alpha"),

	scomp("depth_write"),
	scomp("depth_test"),
};

bool ast_pipeline_eval(AST_Node *pipeline, PipelineOptions *opts) {
	ArenaTemp scratch = arena_scratch_begin(0);

	bool ok = pipeline && opts;

	if (ok) {
		String8 name = pipeline->identifier.lexeme.length
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
			String8 key = tok.lexeme;

			bool is_blend = str8_equals(key, s("blend"));
			bool is_color = is_blend || str8_equals(key, s("blend_color"));
			bool is_alpha = is_blend || str8_equals(key, s("blend_alpha"));

			if (is_color || is_alpha) {
				AST_Node *call = assign->last_child;
				ok = call && call->type == AST_NODE_EXPR_CALL;
				if (ok == false) {
					String8 message = str8_pushf(scratch.arena,
						s("Expect function call value for '%.*s'"),
						sspread(key));
					report(tok.line, tok.column, lexer_error_location_string(scratch.arena, tok), message);
					break;
				}

				int32_t op = eval_pipeline_state(blend_op_table, countof(blend_op_table), call->identifier.lexeme,
					s("Pipeline '%.*s': unknown blend operation '%.*s'"), sspread(name), sspread(call->identifier.lexeme));
				ok &= op != -1;

				AST_Node *src_node = call->first_child;
				AST_Node *dst_node = src_node ? src_node->next_sibling : 0;

				ok &= src_node && dst_node && src_node != dst_node;
				if (ok == false) {
					String8 message = str8_pushf(
						scratch.arena,
						s("Expect two arguments for blend operation '%.*s'."),
						sspread(key));
					report(tok.line, tok.column, lexer_error_location_string(scratch.arena, tok), message);
					break;
				}

				int32_t src = eval_pipeline_state(blend_factor_table, countof(blend_factor_table), src_node->identifier.lexeme,
					s("Pipeline '%.*s': unknown source blend factor '%.*s'"), sspread(name), sspread(src_node->identifier.lexeme));

				int32_t dst = eval_pipeline_state(blend_factor_table, countof(blend_factor_table), dst_node->identifier.lexeme,
					s("Pipeline '%.*s': unknown destination blend factor '%.*s'"), sspread(name), sspread(dst_node->identifier.lexeme));

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
			} else if (str8_equals(key, s("cull")) || str8_equals(key, s("cull_mode"))) {
				AST_Node *value_node = assign->last_child;

				ok = value_node;
				if (ok == false) {
					LOG_ERROR("Pipeline '%.*s': cull requires a value", sspread(name));
					break;
				}

				CullMode value = CULL_MODE_MAX;

				for (uint32_t index = 0; index < countof(cull_mode_table); ++index) {
					if (str8_equals(cull_mode_table[index], value_node->identifier.lexeme)) {
						value = (CullMode)index;
						break;
					}
				}

				ok = value != CULL_MODE_MAX;
				if (ok == false) {
					LOG_ERROR("Pipeline '%.*s': unknown cull mode '%.*s'", sspread(name), sspread(value_node->identifier.lexeme));
					break;
				}

				opts->cull_mode = value;
			} else if (str8_equals(key, s("depth_test")) || str8_equals(key, s("depth_write"))) {
				AST_Node *value_node = assign->last_child;

				ok = value_node;
				if (ok == false) {
					LOG_ERROR("Pipeline '%.*s': depth_test requires a value", sspread(name));
					break;
				}

				struct {
					String8 key;
					bool value;
				} depth_table[] = {
					{ s("true"), true },
					{ s("false"), false },
					{ s("1"), true },
					{ s("0"), false }
				};

				int32_t found = -1;
				for (uint32_t index = 0; index < countof(depth_table); ++index) {
					if (str8_equals(depth_table[index].key, value_node->identifier.lexeme)) {
						found = index;
						break;
					}
				}

				ok = found != -1;
				if (ok == false) {
					String8 message = str8_pushf(
						scratch.arena,
						s("Expect boolean for '%.*s', got '%.*s'"),
						sspread(key),
						sspread(value_node->identifier.lexeme));

					tok = value_node->identifier;
					report(tok.line, tok.column, lexer_error_location_string(scratch.arena, tok), message);
					break;
				}

				if (str8_equals(key, s("depth_test")))
					opts->disable_depth_test = !depth_table[found].value;
				else
					opts->disable_depth_write = !depth_table[found].value;
			} else if (str8_equals(key, s("polygon")) || str8_equals(key, s("polygon_mode"))) {
				AST_Node *value_node = assign->last_child;

				ok = value_node;
				if (ok == false) {
					LOG_ERROR("Pipeline '%.*s': polygon requires a value", sspread(name));
					break;
				}

				PolygonMode value = POLYGON_MODE_MAX;

				for (uint32_t index = 0; index < countof(cull_mode_table); ++index) {
					if (str8_equals(cull_mode_table[index], value_node->identifier.lexeme)) {
						value = (PolygonMode)index;
						break;
					}
				}

				ok = value != POLYGON_MODE_MAX;
				if (ok == false) {
					LOG_ERROR("Pipeline '%.*s': unknown polygon mode '%.*s'", sspread(name), sspread(value_node->identifier.lexeme));
					break;
				}

				opts->polygon_mode = value;
			} else {
				String8 message = str8_pushf(
					scratch.arena,
					s("Unknown pipeline property '%.*s'."),
					sspread(tok.lexeme));
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

int main(void) {
	Arena arena[] = { arena_make(MiB(32)) };

	String8 shader_directory = s("assets/shaders/");
	uint32_t file_count;
	String8 *files = os_directory_files(arena, shader_directory, &file_count);

	AST_Node *program = ast_make(arena, AST_NODE_PROGRAM);
	for (uint32_t index = 0; index < file_count; ++index) {
		if (str8_equals(str8_fileext(files[index]), s("shader")) == false) continue;
		String8 file = str8_concat(arena, shader_directory, files[index]);

		Lexer lexer[] = { lexer_make(os_file_read(arena, file), keyword_to_string, countof(keyword_to_string)) };
		while (lexer_at_end(lexer) == false) {
			if (lexer_match(lexer, TOKEN_KEYWORD_0 + KEYWORD_SHADER, 0))
				ast_pushback(program, ast_parse_shader_decl(arena, lexer));
			else
				lexer_advance(lexer);
		}
	}

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

	String8 code_output_directory = s("game/src/generated");
	if (os_directory_exists(code_output_directory) == false)
		os_directory_make(code_output_directory);
	FILE *header = fopen("game/src/generated/assets_generated.h", "w");
	FILE *source = fopen("game/src/generated/assets_generated.c", "w");

	if (header == 0 || source == 0) return -1;

	String8 output_directory = s("assets/shaders/generated");
	if (os_directory_exists(output_directory) == false)
		os_directory_make(output_directory);

	fprintf(header, "#pragma once\n");
	fprintf(header, "#include \"core/strings.h\"\n");
	fprintf(header, "#include \"gfx/gfx_types.h\"\n\n");

	fprintf(header, "typedef struct {\n");
	fprintf(header, "    String8 name;\n");
	fprintf(header, "    String8 filepaths[SHADER_STAGE_MAX];\n");
	fprintf(header, "    PipelineOptions pipelines[8];\n");
	fprintf(header, "    uint32_t pipeline_count;\n");
	fprintf(header, "} ShaderMetadata;\n\n");

	fprintf(header, "typedef enum {\n");
	AST_Node *shader = program->first_child;
	if (shader) do {
			String8 name = shader->identifier.lexeme;
			String8 name_upper = str8_upper(arena, name);
			fprintf(header, "    RES_SHADER_%.*s,\n", sspread(name_upper));

			String8 glsl_paths[SHADER_STAGE_MAX] = {
				[SHADER_STAGE_VERTEX] = str8_pushf(arena, s("%.*s/v_%.*s.glsl"), sspread(output_directory), sspread(name)),
				[SHADER_STAGE_FRAGMENT] = str8_pushf(arena, s("%.*s/f_%.*s.glsl"), sspread(output_directory), sspread(name)),
				[SHADER_STAGE_COMPUTE] = str8_pushf(arena, s("%.*s/c_%.*s.glsl"), sspread(output_directory), sspread(name)),
			};

			String8 spv_paths[SHADER_STAGE_MAX] = {
				[SHADER_STAGE_VERTEX] = str8_pushf(arena, s("%.*s/v_%.*s.spv"), sspread(output_directory), sspread(name)),
				[SHADER_STAGE_FRAGMENT] = str8_pushf(arena, s("%.*s/f_%.*s.spv"), sspread(output_directory), sspread(name)),
				[SHADER_STAGE_COMPUTE] = str8_pushf(arena, s("%.*s/c_%.*s.spv"), sspread(output_directory), sspread(name)),
			};

			String8 shared = { 0 };
			String8 shader_sources[SHADER_STAGE_MAX] = { 0 };

			uint32_t pipeline_count = 0;

			AST_Node *decl = shader->first_child;
			if (decl) do {
					if (decl->type == AST_NODE_SHARED_DECL)
						shared = str8_concat(arena, shared, decl->code);
					else if (decl->type == AST_NODE_VERTEX_DECL)
						shader_sources[SHADER_STAGE_VERTEX] = str8_concat(arena, shader_sources[SHADER_STAGE_VERTEX], decl->code);
					else if (decl->type == AST_NODE_FRAGMENT_DECL)
						shader_sources[SHADER_STAGE_FRAGMENT] = str8_concat(arena, shader_sources[SHADER_STAGE_FRAGMENT], decl->code);
					else if (decl->type == AST_NODE_COMPUTE_DECL)
						shader_sources[SHADER_STAGE_COMPUTE] = str8_concat(arena, shader_sources[SHADER_STAGE_COMPUTE], decl->code);
					else if (decl->type == AST_NODE_PIPELINE_DECL) {
						String8 pipeline_name = decl->identifier.lexeme;
						String8 pipeline_name_upper = str8_upper(arena, pipeline_name);
						fprintf(header, "#define PIPELINE_%.*s_%.*s %u\n", sspread(name_upper), sspread(pipeline_name_upper), pipeline_count++);
					}

					decl = decl->next_sibling;
				} while (decl != shader->first_child);

			if (pipeline_count == 0)
				fprintf(header, "#define PIPELINE_%.*s_%s %u\n", sspread(name_upper), "DEFAULT", pipeline_count++);

			String8 version_header = s("#version 450 core\n");
			String8 stage_info[SHADER_STAGE_MAX] = {
				[SHADER_STAGE_VERTEX] = s(
					"#pragma shader_stage(vertex)\n\n"
					"#define INOUT out\n"),
				[SHADER_STAGE_FRAGMENT] = s(
					"#pragma shader_stage(fragment)\n\n"
					"#define INOUT in\n"),
				[SHADER_STAGE_COMPUTE] = s("#pragma shader_stage(compute)"),
			};
			String8 include_dir = s("assets/shaders/");
			String8 cleaned_shared = str8_dedent(arena, shared);

			for (uint32_t index = 0; index < countof(shader_sources); ++index) {
				if (shader_sources[index].length == 0) continue;

				String8 cleaned_source = str8_dedent(arena, shader_sources[index]);

				String8 glsl = str8_pushf(arena,
					s("%.*s%.*s\n// --- shared_start ---\n%.*s\n\n// --- shared_end ---\n\n// --- source_start ---\n%.*s\n\n// --- source_end ---"),
					sspread(version_header),
					sspread(stage_info[index]),
					sspread(cleaned_shared),
					sspread(cleaned_source) //
				);
				bool glsl_modified = true;

				if (os_file_exists(glsl_paths[index])) {
					String8 existing = os_file_read(arena, glsl_paths[index]);

					if (str8_equals(existing, glsl))
						glsl_modified = false;
				}

				if (glsl_modified)
					os_file_write(glsl_paths[index], glsl.text, glsl.length);
				// Only recompile if GLSL was modified or SPV is missing/outdated
				if (glsl_modified || os_file_exists(spv_paths[index]) == false) {
					String8 cmd = str8_pushf(arena, s("glslc -I %.*s %.*s -o %.*s"),
						sspread(include_dir),
						sspread(glsl_paths[index]),
						sspread(spv_paths[index]));

					os_execute_command(cmd);
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
			String8 name = shader->identifier.lexeme;
			String8 upper = str8_upper(arena, name);

			String8 filepaths[SHADER_STAGE_MAX] = {
				[SHADER_STAGE_VERTEX] = str8_pushf(arena, s("%.*s/v_%.*s.spv"), sspread(output_directory), sspread(name)),
				[SHADER_STAGE_FRAGMENT] = str8_pushf(arena, s("%.*s/f_%.*s.spv"), sspread(output_directory), sspread(name)),
				[SHADER_STAGE_COMPUTE] = str8_pushf(arena, s("%.*s/c_%.*s.spv"), sspread(output_directory), sspread(name)),
			};

			bool is_compute = false;

			uint32_t pipeline_count = 0;
			AST_Node *decl = shader->first_child;
			if (decl) do {
					if (decl->type == AST_NODE_COMPUTE_DECL)
						is_compute = true;
					if (decl->type == AST_NODE_PIPELINE_DECL) pipeline_count++;

					decl = decl->next_sibling;
				} while (decl != shader->first_child);

			ASSERT_FORMAT((is_compute && pipeline_count) == false && pipeline_count < 8, "Shader '%.*s': compute and graphics pipeline defined together.", sspread(shader->identifier.lexeme));

			fprintf(source, "    [RES_SHADER_%.*s] = {\n", sspread(upper));
			fprintf(source, "      .name = scomp(\"%.*s\"),\n", sspread(name));
			fprintf(source, "      .filepaths = {\n");
			if (is_compute)
				fprintf(source, "          [SHADER_STAGE_COMPUTE] = scomp(\"%.*s\"),\n", sspread(filepaths[SHADER_STAGE_COMPUTE]));
			else {
				fprintf(source, "          [SHADER_STAGE_VERTEX] = scomp(\"%.*s\"),\n", sspread(filepaths[SHADER_STAGE_VERTEX]));
				fprintf(source, "          [SHADER_STAGE_FRAGMENT] = scomp(\"%.*s\"),\n", sspread(filepaths[SHADER_STAGE_FRAGMENT]));
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

							String8 pipeline_name = decl->identifier.lexeme;
							String8 pipeline_upper = str8_upper(arena, pipeline_name);

							fprintf(source, "          [PIPELINE_%.*s_%.*s] = {\n", sspread(upper), sspread(pipeline_upper)); // TODO: PipelineID
							fprintf(source, "              .cull_mode = %.*s,\n", sspread(cull_mode_to_string[options.cull_mode]));
							fprintf(source, "              .polygon_mode = %.*s,\n", sspread(polygon_mode_to_string[options.polygon_mode]));
							fprintf(source, "              .disable_depth_test = %s,\n", options.disable_depth_test ? "true" : "false");
							fprintf(source, "              .disable_depth_write = %s,\n", options.disable_depth_write ? "true" : "false");
							fprintf(source, "              .blend_enable = %s,\n", options.blend_enable ? "true" : "false");
							fprintf(source, "              .color_op = %.*s,\n", sspread(blend_op_to_string[options.color_op]));
							fprintf(source, "              .alpha_op = %.*s,\n", sspread(blend_op_to_string[options.alpha_op]));
							fprintf(source, "              .src_color_factor = %.*s,\n", sspread(blend_factor_to_string[options.src_color_factor]));
							fprintf(source, "              .dst_color_factor = %.*s,\n", sspread(blend_factor_to_string[options.dst_color_factor]));
							fprintf(source, "              .src_alpha_factor = %.*s,\n", sspread(blend_factor_to_string[options.src_alpha_factor]));
							fprintf(source, "              .dst_alpha_factor = %.*s,\n", sspread(blend_factor_to_string[options.dst_alpha_factor]));
							fprintf(source, "          },\n");
						}

						decl = decl->next_sibling;
					} while (decl != shader->first_child);

				if (pipeline_count == 0) {
                    pipeline_count = 1;
					PipelineOptions options = { 0 };
					options.src_color_factor = BLEND_FACTOR_ONE;
					options.src_alpha_factor = BLEND_FACTOR_ONE;

					fprintf(source, "          [PIPELINE_%.*s_%s] = {\n", sspread(upper), "DEFAULT");
					fprintf(source, "              .cull_mode = %.*s,\n", sspread(cull_mode_to_string[options.cull_mode]));
					fprintf(source, "              .polygon_mode = %.*s,\n", sspread(polygon_mode_to_string[options.polygon_mode]));
					fprintf(source, "              .disable_depth_test = %s,\n", options.disable_depth_test ? "true" : "false");
					fprintf(source, "              .disable_depth_write = %s,\n", options.disable_depth_write ? "true" : "false");
					fprintf(source, "              .blend_enable = %s,\n", options.blend_enable ? "true" : "false");
					fprintf(source, "              .color_op = %.*s,\n", sspread(blend_op_to_string[options.color_op]));
					fprintf(source, "              .alpha_op = %.*s,\n", sspread(blend_op_to_string[options.alpha_op]));
					fprintf(source, "              .src_color_factor = %.*s,\n", sspread(blend_factor_to_string[options.src_color_factor]));
					fprintf(source, "              .dst_color_factor = %.*s,\n", sspread(blend_factor_to_string[options.dst_color_factor]));
					fprintf(source, "              .src_alpha_factor = %.*s,\n", sspread(blend_factor_to_string[options.src_alpha_factor]));
					fprintf(source, "              .dst_alpha_factor = %.*s,\n", sspread(blend_factor_to_string[options.dst_alpha_factor]));
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
