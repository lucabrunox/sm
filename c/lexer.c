#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "lexer.h"

#define PEEK (*(lexer->ptr))
#define READ (PEEK ? ((PEEK == '\n' ? (lexer->row++, lexer->col=0) : lexer->col++), *(lexer->ptr)++) : 0)
#define LEX1(e) if (c == e[0]) { READ; return { .type=e }; }
#define LEX2(e1,e2) if (c == e1[0]) { READ; if (PEEK == e2[0]) { READ; return { .type=e1 e2 }; } else { return { .type=e1 }; } }

void sm_lexer_init (SmLexer* lexer, const char* buf) {
	lexer->ptr = buf;
	lexer->row = lexer->col = 0;
}

void sm_lexer_destroy (void) {
}

SmToken sm_lexer_next (SmLexer* lexer) {
	while (isspace (PEEK)) READ;
	char c = PEEK;
	if (!c) {
		return { .type="eof" };
	}

	while (c == '#') {
		while (PEEK != '\n') READ;
		while (isspace (PEEK)) READ;
		c = PEEK;
	}
	if (!c) {
		return { .type="eof" };
	}

	if (isalpha (c)) {
		char* id = NULL;
		while (isalnum (PEEK)) {
			char* old = id;
			asprintf(&id, "%s%c", id ? id : "", READ);
			free(old);
		}
		if (PEEK == '?') {
			char* old = id;
			asprintf(&id, "%s?", id);
			free(old);
		}
		SmToken t = { .type="id" };
		t.str = id;
		return t;
	}

	if (isdigit (c)) {
		double val = 0;
		while (isdigit (PEEK)) {
			val *= 10;
			val += READ-'0';
		}
		SmToken t = { .type="num" };
		t.num=val;
		return t;
	}

	if (c == '_') {
		SmToken t = { .type="id" };
		t.str = strdup ("_");
		READ;
		return t;
	}

	if (c == '!') {
		READ;
		if (READ != '=') {
			return { .type="unknown" };
		}
		return { .type="!=" };
	}

	LEX1("+");
	LEX1("-");
	LEX1("(");
	LEX1(")");
	LEX1("[");
	LEX1("]");
	LEX1("{");
	LEX1("}");
	LEX1(".");
	LEX1(",");
	LEX1(";");
	LEX1(":");
	LEX1("|");

	LEX2("=", "=");
	LEX2("<", "=");
	LEX2(">", "=");
	LEX2("*", "*");
	LEX2("/", "/");

	if (strchr("'\"~`", c)) {
		char q = c;
		char* str = NULL;
		READ;
		while (PEEK != q) {
			if (PEEK == '\\') {
				READ;
				if ((q == '\'' || q == '"') && PEEK == 'n') {
					char* old = str;
					asprintf(&str, "%s\n", str ? str : "", READ);
					free(old);
					continue;
				}
			}
			if (!PEEK) {
				return { .type="unterminated string" };
			}
			char* old = str;
			asprintf(&str, "%s%c", str ? str : "", READ);
			free(old);
		}
		READ;
		SmToken t = { .type="str" };
		t.str = str;
		return t;
	}
	
	return { .type="unknown" };
}

void sm_token_destroy (SmToken* token) {
	if (token->str) {
		free (token->str);
	}
}

