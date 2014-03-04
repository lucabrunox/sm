#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>

#include "lexer.h"

#define PEEK (*(lexer->ptr))
#define READ (PEEK ? ((PEEK == '\n' ? (lexer->row++, lexer->col=0) : lexer->col++), *(lexer->ptr)++) : 0)
#define LEX1(e) if (c == e[0]) { READ; SmToken t = { .start=start, .type=e }; return t; }
#define LEX2(e1,e2) if (c == e1[0]) { READ; if (PEEK == e2[0]) { READ; SmToken t = { .start=start, .type=e1 e2 }; return t; } else { SmToken t = { .start=start, .type=e1 }; return t; } }

void sm_lexer_init (SmLexer* lexer, const char* buf) {
	lexer->ptr = buf;
	lexer->row = lexer->col = 0;
}

void sm_lexer_destroy (SmLexer* lexer) {
}

SmToken sm_lexer_next (SmLexer* lexer) {
	SmLexer start = *lexer;
	
	while (isspace (PEEK)) READ;
	char c = PEEK;
	if (!c) {
		SmToken t = { .start=start, .type="eof" };
		return t;
	}

	while (c == '#') {
		while (PEEK != '\n') READ;
		while (isspace (PEEK)) READ;
		c = PEEK;
	}
	if (!c) {
		SmToken t = { .start=start, .type="eof" };
		return t;
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
		SmToken t = { .start=start, .type="id" };
		t.str = id;
		return t;
	}

	if (isdigit (c)) {
		int val = 0;
		while (isdigit (PEEK)) {
			val *= 10;
			val += READ-'0';
		}
		SmToken t = { .start=start, .type="int" };
		t.intval=val;
		return t;
	}

	if (c == '_') {
		SmToken t = { .start=start, .type="id" };
		t.str = strdup ("_");
		READ;
		return t;
	}

	if (c == '!') {
		READ;
		if (READ != '=') {
			SmToken t = { .start=start, .type="unknown" };
			return t;
		}
		SmToken t = { .start=start, .type="!=" };
		return t;
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
				// canonicalize to "..."
				if (PEEK == '\'') {
					char* old = str;
					asprintf(&str, "%s'", str ? str : "");
					free(old);
				} else {
					char* old = str;
					asprintf(&str, "%s\\", str ? str : "");
					free(old);
				}
			}
			if (!PEEK) {
				SmToken t = { .start=start, .type="unterminated string" };
				return t;
			}
			char* old = str;
			asprintf(&str, "%s%c", str ? str : "", READ);
			free(old);
		}
		READ;
		SmToken t = { .start=start, .type="str" };
		char* compressed = g_strcompress (str);
		free (str);
		t.str = g_strescape (compressed, NULL);
		free (compressed);
		return t;
	}
	
	SmToken t = { .start=start, .type="unknown" };
	return t;
}

void sm_token_destroy (SmToken* token) {
	if (token->str) {
		free (token->str);
	}
}

