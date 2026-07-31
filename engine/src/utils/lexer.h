#ifndef LEXER_H_
#define LEXER_H_

#include <core/strings.h>

typedef enum {
	TOKEN_UNKNOWN,

	// Single-character tokens.
	TOKEN_OPEN_PAREN, // (
	TOKEN_CLOSE_PAREN, // )
	TOKEN_OPEN_BRACE, // {
	TOKEN_CLOSE_BRACE, // }
	TOKEN_OPEN_BRACKET, // [
	TOKEN_CLOSE_BRACKET, // ]
	TOKEN_COMMA, // ,
	TOKEN_DOT, // .
	TOKEN_SEMICOLON, // ;
	TOKEN_COLON, // :
	TOKEN_SLASH, // /
	TOKEN_STAR, // *
	TOKEN_PERCENT, // %
	TOKEN_TILDE, // ~
	TOKEN_CARET, // ^
	TOKEN_QUESTION_MARK, // ?

	// One or two character tokens.
	TOKEN_MINUS, // -
	TOKEN_MINUS_MINUS, // --
	TOKEN_PLUS, // +
	TOKEN_PLUS_PLUS, // ++
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

	TOKEN_EOF,
	TOKEN_MAX,

	TOKEN_KEYWORD_0,
	TOKEN_KEYWORD_1,
	TOKEN_KEYWORD_2,
	TOKEN_KEYWORD_3,
	TOKEN_KEYWORD_4,
	TOKEN_KEYWORD_5,
	TOKEN_KEYWORD_6,
	TOKEN_KEYWORD_7,
	TOKEN_KEYWORD_8,
	TOKEN_KEYWORD_9,
	TOKEN_KEYWORD_10,
	TOKEN_KEYWORD_11,
	TOKEN_KEYWORD_12,
	TOKEN_KEYWORD_13,
	TOKEN_KEYWORD_14,
	TOKEN_KEYWORD_15,
	TOKEN_KEYWORD_16,
	TOKEN_KEYWORD_17,
	TOKEN_KEYWORD_18,
	TOKEN_KEYWORD_19,
	TOKEN_KEYWORD_20,
	TOKEN_KEYWORD_21,
	TOKEN_KEYWORD_22,
	TOKEN_KEYWORD_23,
	TOKEN_KEYWORD_24,
	TOKEN_KEYWORD_25,
	TOKEN_KEYWORD_26,
	TOKEN_KEYWORD_27,
	TOKEN_KEYWORD_28,
	TOKEN_KEYWORD_29,
	TOKEN_KEYWORD_30,
	TOKEN_KEYWORD_31,
	TOKEN_KEYWORD_32,
	TOKEN_KEYWORD_33,
	TOKEN_KEYWORD_34,
	TOKEN_KEYWORD_35,
	TOKEN_KEYWORD_36,
	TOKEN_KEYWORD_37,
	TOKEN_KEYWORD_38,
	TOKEN_KEYWORD_39,
	TOKEN_KEYWORD_40,
	TOKEN_KEYWORD_41,
	TOKEN_KEYWORD_42,
	TOKEN_KEYWORD_43,
	TOKEN_KEYWORD_44,
	TOKEN_KEYWORD_45,
	TOKEN_KEYWORD_46,
	TOKEN_KEYWORD_47,
	TOKEN_KEYWORD_48,
	TOKEN_KEYWORD_49,
	TOKEN_KEYWORD_50,
	TOKEN_KEYWORD_51,
	TOKEN_KEYWORD_52,
	TOKEN_KEYWORD_53,
	TOKEN_KEYWORD_54,
	TOKEN_KEYWORD_55,
	TOKEN_KEYWORD_56,
	TOKEN_KEYWORD_57,
	TOKEN_KEYWORD_58,
	TOKEN_KEYWORD_59,
	TOKEN_KEYWORD_60,
	TOKEN_KEYWORD_61,
	TOKEN_KEYWORD_62,
	TOKEN_KEYWORD_63,
	TOKEN_KEYWORD_64,
} TokenType;

typedef struct {
	TokenType type;
	String8 lexeme;
	int line;
	int column;
} Token;

typedef struct {
	String8 *keyword;
	uint32_t keyword_count;
} LexerConfig;

typedef struct {
	String8 source;

	String8 *keywords;
	uint32_t keyword_count;

	uint8_t *cursor;
	uint32_t line;
	int column;

	Token peeked;
	bool has_peeked;
} Lexer;

extern const String8 token_type_to_string[TOKEN_MAX];

static inline Lexer lexer_make(String8 source, String8 *keywords, uint32_t keyword_count) {
	return (Lexer){ .source = source, .cursor = source.text, .line = 1, .keyword_count = keyword_count, .keywords = keywords };
}

Token lexer_next(Lexer *lexer);
Token lexer_peek(Lexer *lexer);

uint8_t *lexer_skip_to_end_of_line(Lexer *lexer);

// If the next token matches `type`, consume and return it (ok=true).
// Otherwise leave it unconsumed and return a zeroed token (ok=false).
bool lexer_match(Lexer *lexer, TokenType type, Token *out);

// Consume the next token; assert it matches `type`.
// Returns the token. On mismatch you get TOKEN_UNKNOWN and can check .type.
Token lexer_expect(Lexer *lexer, TokenType type);
Token lexer_expect_multiple(Lexer *lexer, TokenType *types, uint32_t type_count);

// True if the next token is EOF.
bool lexer_at_end(Lexer *lexer);

#endif /* LEXER_H_ */
