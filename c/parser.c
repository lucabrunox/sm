#include <stdlib.h>
#include <stdio.h>

#include "uthash/src/utlist.h"
#include "uthash/src/utarray.h"

#include "lexer.h"
#include "parser.h"
#include "ast.h"

struct _SmParser {
	SmLexer lexer;
	SmToken cur;
};

SmParser* sm_parser_new (void) {
	SmParser* parser = (SmParser*) calloc (1, sizeof (SmParser));
	return parser;
}

void sm_parser_free (SmParser* parser) {
	sm_token_destroy (&parser->cur);
	free (parser);
}

#define STATIC static
#define NEXT (sm_token_destroy(&parser->cur), parser->cur = sm_lexer_next(&parser->lexer), parser->cur)
#define PTOK(t) puts(t.type)
#define EXPR(x) ((SmExpr*)x)
#define FUNC(n) STATIC SmExpr* n (SmParser* parser)
#define FUNC2(n) STATIC SmExpr* n (SmParser* parser, SmExpr* inner)
#define TYPE (parser->cur.type)
#define EXPECT(x) if (TYPE != x) { puts("expected " x); return NULL; }
#define ACCEPT(x) ((TYPE == x) ? (NEXT, 1) : 0)
#define ACCEPT_ID(x) ((TYPE == "id") ? (!strcmp(STR, x) ? (NEXT, 1) : 0) : 0)
#define SKIP(x) EXPECT(x); NEXT;
#define STR (parser->cur.str)
#define NEW(n,x,t) x* n = (x*)calloc(1, sizeof(x)); (n)->base.type=t;
#define SAVE (*parser)
#define RESTORE(x) sm_token_destroy(&parser->cur); *parser = x

STATIC char* identifier (SmParser* parser) {
	EXPECT("id");
	char* val = STR;
	STR=NULL;
	NEXT;
	return val;
}

FUNC2(member) {
	char* id = identifier(parser);
	NEW(expr, SmMemberExpr, SM_MEMBER_EXPR);
	expr->inner = inner;
	expr->name = id;
	return EXPR(expr);
}

FUNC(primary) {
	SmExpr* expr = NULL;
	if (TYPE == "id") {
		expr = member(parser, NULL);
	}
	
	return expr;
}

FUNC(assign) {
	SmParser begin = SAVE;

	if (TYPE == "id") {
		UT_array* names;
		utarray_new (names, &ut_str_icd);
		utarray_push_back (names, &STR);
		STR=NULL;

		while (ACCEPT (",")) {
			if (TYPE == "id") {
				utarray_push_back (names, &STR);
				STR=NULL;
			} else {
				goto rollback;
			}
		}

		if (TYPE == "=") {
			SmExpr* expr = primary(parser);
			NEW(a, SmAssignExpr, SM_ASSIGN_EXPR);
			a->names = names;
			return EXPR(a);
		} else {
			goto rollback;
		}

	rollback:
		utarray_free (names);
		RESTORE(begin);
	}
		
	return primary(parser);
}

FUNC(seq) {
	SmExpr* expr = assign(parser);
	if (expr->type != SM_ASSIGN_EXPR) {
		return expr;
	}

	NEW(seq, SmSeqExpr, SM_SEQ_EXPR);
	while (ACCEPT(";")) {
		SmAssignList* entry = (SmAssignList*) calloc(1, sizeof(SmAssignList));
		entry->expr = (SmAssignExpr*) expr;
		DL_APPEND(seq->assigns, entry);

		expr = assign(parser);
		if (expr->type != SM_ASSIGN_EXPR) {
			seq->result = expr;
			break;
		}
	}
	
	return EXPR(seq);
}

SmExpr* sm_parser_parse (SmParser* parser, SmLexer lexer) {
	parser->lexer = lexer;
	parser->cur = {0};
	
	NEXT;
	SmExpr* expr = seq(parser);
	EXPECT("eof");
	return expr;
}

