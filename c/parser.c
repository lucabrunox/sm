#include <stdlib.h>
#include <stdio.h>

#include "lexer.h"
#include "parser.h"
#include "ast.h"

#define NEXT (parser->cur = sm_lexer_next(&parser->lexer), parser->cur)
#define PTOK(t) puts(t.type)

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

#define EXPR(x) ((SmExpr*)x)
#define FUNC(n) static SmExpr* n (SmParser* parser)
#define FUNC2(n) static SmExpr* n (SmParser* parser, SmExpr* inner)
#define TYPE (parser->cur.type)
#define EXPECT(x) if (TYPE != x) { puts("expected " x); return NULL; }
#define STR (parser->cur.str)
#define NEW(n,x,t) x* n = (x*)calloc(1, sizeof(x)); (n)->base.type=t;

static char* identifier (SmParser* parser) {
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

SmExpr* sm_parser_parse (SmParser* parser, SmLexer lexer) {
	parser->lexer = lexer;
	parser->cur = {0};
	
	NEXT;
	SmExpr* expr = primary(parser);
	EXPECT("eof");
	return expr;
}

