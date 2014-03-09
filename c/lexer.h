#ifndef SM_LEXER_H
#define SM_LEXER_H

#include <stdint.h>

typedef struct {
	const char* ptr;
	int row;
	int col;
} SmLexer;

typedef struct {
	SmLexer start;
	const char *type;
	union {
		double dblval;
		int intval;
		char chr;
		char *str;
	};
} SmToken;

void sm_lexer_init (SmLexer* lexer, const char* buf);
SmToken sm_lexer_next (SmLexer* lexer);
void sm_lexer_destroy (SmLexer* lexer);

void sm_token_destroy (SmToken* token);

#endif
