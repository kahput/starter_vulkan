#ifndef LEXER_H_
#define LEXER_H_

#include <core/strings.h>

typedef enum {
	TOKEN_UNKNOWN,

	// Single-character tokens.
	TOKEN_LEFT_PAREN, // (
	TOKEN_RIGHT_PAREN, // )
	TOKEN_LEFT_BRACE, // {
	TOKEN_RIGHT_BRACE, // }
	TOKEN_LEFT_BRACKET, // [
	TOKEN_RIGHT_BRACKET, // ]
	TOKEN_COMMA, // ,
	TOKEN_DOT, // .
	TOKEN_MINUS, // -
	TOKEN_PLUS, // +
	TOKEN_SEMICOLON, // ;
	TOKEN_COLON, // :
	TOKEN_SLASH, // /
	TOKEN_STAR, // *
	TOKEN_PERCENT, // %

	// One or two character tokens.
	TOKEN_BANG, // !
	TOKEN_BANG_EQUAL, // !=
	TOKEN_EQUAL, // =
	TOKEN_EQUAL_EQUAL, // ==
	TOKEN_GREATER, // >
	TOKEN_GREATER_EQUAL, // >=
	TOKEN_LESS, //
	TOKEN_LESS_EQUAL, // <=
	TOKEN_AMP, // &
	TOKEN_AMP_AMP, // &&
	TOKEN_PIPE, // |
	TOKEN_PIPE_PIPE, // ||

	// Literals.
	TOKEN_IDENTIFIER,
	TOKEN_STRING,
	TOKEN_FLOAT,
	TOKEN_INTEGER,

	// Keywords
	TOKEN_TRUE,
	TOKEN_FALSE,
	TOKEN_NULL,

	TOKEN_EOF,
	TOKEN_TYPE_COUNT,
} TokenType;

typedef struct {
	TokenType type;
	String string;
	int line;
	int column;
} Token;

typedef struct {
	String source;
	char *at;
	int line;
	int column;

	Token peeked;
	bool has_peeked;
} Lexer;

extern const char *token_type_names[TOKEN_TYPE_COUNT];

static inline Lexer lexer_make(String source) { return (Lexer){ .source = source, .at = source.memory }; }

Token lexer_next(Lexer *lexer);
Token lexer_peek(Lexer *lexer);

// If the next token matches `type`, consume and return it (ok=true).
// Otherwise leave it unconsumed and return a zeroed token (ok=false).
bool lexer_match(Lexer *lexer, TokenType type, Token *out);

// Consume the next token; assert it matches `type`.
// Returns the token. On mismatch you get TOKEN_UNKNOWN and can check .type.
Token lexer_expect(Lexer *lexer, TokenType type);

// True if the next token is EOF.
bool lexer_at_end(Lexer *lexer);

#endif /* LEXER_H_ */
