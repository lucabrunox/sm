#include <stdlib.h>
#include <stdio.h>
#include <glib.h>

#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "astdumper.h"

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

#define NEXT (sm_token_destroy(&parser->cur), parser->cur = sm_lexer_next(&parser->lexer), parser->cur)
#define PTOK puts(parser->cur.type)
#define FUNC(n) static SmExpr* n (SmParser* parser)
#define FUNC2(n) static SmExpr* n (SmParser* parser, SmExpr* inner)
#define TYPE (parser->cur.type)
#define EXPECT(x) if (!CASE(x)) { printf("expected " x ", got %s\n", TYPE); return NULL; }
#define SKIP(x) if (!CASE(x)) { printf("expected " x ", got %s\n", TYPE); return NULL; } NEXT;
#define ACCEPT(x) ((CASE(x)) ? (NEXT, 1) : 0)
#define ACCEPT_ID(x) ((CASE("id")) ? (!strcmp(STR, x) ? (NEXT, 1) : 0) : 0)
#define SKIP(x) EXPECT(x); NEXT;
#define STR (parser->cur.str)
#define NEW(n,x,t) x* n = (x*)calloc(1, sizeof(x)); (n)->base.type=t;
#define SAVE (*parser)
#define RESTORE(x) parser->lexer=x.cur.start; NEXT
#define CHECK(x) if (!x) return NULL
#define CASE(x) (!strcmp(TYPE, x))

static char* identifier (SmParser* parser) {
	EXPECT("id");
	char* val = STR;
	STR=NULL;
	NEXT;
	return val;
}

FUNC(seq);

FUNC2(member) {
	char* id = identifier(parser);
	CHECK(id);

	NEW(expr, SmMemberExpr, SM_MEMBER_EXPR);
	expr->inner = inner;
	expr->name = id;
	return EXPR(expr);
}

FUNC(primary) {
	SmExpr* expr = NULL;
	if (CASE("id")) {
		expr = member(parser, NULL);
	} else if (CASE("str")) {
		NEW(tmp, SmLiteral, SM_LITERAL);
		tmp->str = STR;
		STR=NULL;
		NEXT;
		expr = EXPR(tmp);
	} else if (CASE("(")) {
		SKIP("(");
		expr = seq(parser);
		SKIP(")");
	} else {
		printf("unexpected %s\n", TYPE);
		abort();
		return NULL;
	}
	
	return expr;
}

FUNC(assign) {
	SmParser begin = SAVE;

	if (CASE("id")) {
		char* name = identifier(parser);
		GPtrArray* names = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
		g_ptr_array_add (names, name);
		while (ACCEPT (",")) {
			if (CASE("id")) {
				g_ptr_array_add (names, STR);
				STR=NULL;
			} else {
				goto rollback;
			}
		}

		if (ACCEPT ("=")) {
			SmExpr* expr = primary(parser);
			CHECK(expr);
			NEW(a, SmAssignExpr, SM_ASSIGN_EXPR);
			a->names = names;
			a->value = expr;
			return EXPR(a);
		} else {
			goto rollback;
		}

	rollback:
		g_ptr_array_unref (names);
		RESTORE(begin);
	}

	return primary(parser);
}

FUNC(seq) {
	SmExpr* expr = assign(parser);
	CHECK(expr);
	if (expr->type != SM_ASSIGN_EXPR) {
		return expr;
	}

	NEW(seq, SmSeqExpr, SM_SEQ_EXPR);
	seq->assigns = g_queue_new ();
	while (ACCEPT(";")) {
		g_queue_push_tail (seq->assigns, expr);

		expr = assign(parser);
		CHECK(expr);
		if (expr->type != SM_ASSIGN_EXPR) {
			seq->result = expr;
			break;
		}
	}
	
	return EXPR(seq);
}

SmExpr* sm_parser_parse (SmParser* parser, SmLexer lexer) {
	memset(parser, '\0', sizeof(SmParser));
	parser->lexer = lexer;
	
	NEXT;
	SmExpr* expr = seq(parser);
	CHECK(expr);
	EXPECT("eof");
	return expr;
}

