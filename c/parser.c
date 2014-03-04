#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "astdumper.h"

struct _SmParser {
	SmLexer lexer;
	SmToken cur;
};

SmParser* sm_parser_new (void) {
	SmParser* parser = g_new0 (SmParser, 1);
	return parser;
}

void sm_parser_free (SmParser* parser) {
	sm_token_destroy (&parser->cur);
	g_free (parser);
}

#define NEXT (sm_token_destroy(&CUR), CUR = sm_lexer_next(&parser->lexer))
#define CUR (parser->cur)
#define PTOK puts(CUR.type)
#define FUNC(n) static SmExpr* n (SmParser* parser)
#define FUNC2(n,x) static SmExpr* n (SmParser* parser, x)
#define TYPE (CUR.type)
#define EXPECT(x) if (!CASE(x)) { printf("expected " x ", got %s\n", TYPE); return NULL; }
#define ACCEPT(x) ((CASE(x)) ? (NEXT, 1) : 0)
#define ACCEPT_ID(x) ((CASE("id")) ? (!strcmp(STR, x) ? (NEXT, 1) : 0) : 0)
#define SKIP(x) EXPECT(x); NEXT;
#define SKIP_ID(x) EXPECT("id"); if (strcmp(STR, x)) { printf("expected " x ", got %s\n", STR); return NULL; } else { NEXT; }
#define STR (CUR.str)
#define NEW(n,x,t) x* n = g_new0(x, 1); (n)->base.type=t
#define SAVE (*parser)
#define RESTORE(x) parser->lexer=x.cur.start; NEXT
#define CHECK(x) if (!x) return NULL
#define CASE(x) (!strcmp(TYPE, x))
#define CASESTR(x) (!strcmp(STR, x))

static char* identifier (SmParser* parser) {
	EXPECT("id");
	char* val = STR;
	STR=NULL;
	NEXT;
	return val;
}

FUNC(seq);

FUNC2(member, SmExpr* inner) {
	char* id = identifier(parser);
	CHECK(id);

	NEW(expr, SmMemberExpr, SM_MEMBER_EXPR);
	expr->inner = inner;
	expr->name = id;
	if (inner) {
		inner->parent = EXPR(expr);
	}
	return EXPR(expr);
}

FUNC(primary) {
	SmExpr* expr = NULL;
	if (CASE("id")) {
		expr = member(parser, NULL);
	} else if (CASE("str")) {
		NEW(tmp, SmLiteral, SM_STR_LITERAL);
		tmp->str = STR;
		STR=NULL;
		NEXT;
		expr = EXPR(tmp);
	} else if (CASE("int")) {
		NEW(tmp, SmLiteral, SM_INT_LITERAL);
		tmp->intval = CUR.intval;
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

FUNC(call) {
	SmExpr* expr = primary(parser);
	CHECK(expr);

	GPtrArray* args = g_ptr_array_new ();
	NEW(call, SmCallExpr, SM_CALL_EXPR);
	while (TRUE) {
		if (!CASE("id") && !CASE("(") && !CASE("[") && !CASE("{")) {
			break;
		}
		if (CASE("id") && (CASESTR("if") || CASESTR("then") || CASESTR("else"))) {
			break;
		}
		SmExpr* arg = primary(parser);
		CHECK(arg);
		g_ptr_array_add (args, arg);
		arg->parent = EXPR(call);
	}
	
	if (!args->len) {
		g_ptr_array_unref (args);
		return expr;
	} else {
		call->func = expr;
		call->args = args;
		expr->parent = EXPR(call);
		return EXPR(call);
	}
}

FUNC(binary) {
	SmExpr* left = call(parser);
	CHECK(left);
	
	if (CASE("<") || CASE("<=") || CASE(">") || CASE(">=") || CASE("==") || CASE("!=") || CASE("and") || CASE("or") ||
		CASE("+") || CASE("-") || CASE("*") || CASE("/") || CASE("**") || CASE("//")) {
		NEW(bin, SmBinaryExpr, SM_BINARY_EXPR);
		bin->op = TYPE;
		NEXT;
		
		bin->left = left;
		left->parent = EXPR(bin);

		SmExpr* right = binary(parser);
		CHECK(right);
		
		bin->right = right;
		right->parent = EXPR(bin);

		return EXPR(bin);
	}
	return left;
}

FUNC(cond) {
	if (ACCEPT_ID("if")) {
		SmExpr* condexpr = binary(parser);
		CHECK(condexpr);

		SKIP_ID("then");
		SmExpr* truebody = binary(parser);
		CHECK(truebody);

		SKIP_ID("else");
		SmExpr* falsebody = cond(parser);
		CHECK(falsebody);

		NEW(expr, SmCondExpr, SM_COND_EXPR);
		expr->cond = condexpr;
		expr->truebody = truebody;
		expr->falsebody = falsebody;
		condexpr->parent = EXPR(expr);
		truebody->parent = EXPR(expr);
		falsebody->parent = EXPR(expr);
		
		return EXPR(expr);
	}
		
	return binary(parser);
}
		

FUNC2(function, int allow_seq) {
	SmParser begin = SAVE;
	if (CASE("id")) {
		GPtrArray* params = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
		while (CASE("id")) {
			g_ptr_array_add (params, identifier(parser));
			if (ACCEPT(":")) {
				SmExpr* body;
				if (allow_seq) {
					body = seq(parser);
				} else {
					body = cond(parser);
				}
				CHECK(body);
				if (body->type != SM_SEQ_EXPR) {
					// create a seq expr because it's easier at compile time
					NEW(seq, SmSeqExpr, SM_SEQ_EXPR);
					seq->assigns = g_ptr_array_new ();
					seq->result = body;
					body->parent = EXPR(seq);
					body = EXPR(seq);
				}
				
				NEW(expr, SmFuncExpr, SM_FUNC_EXPR);
				expr->params = params;
				expr->body = body;
				body->parent = EXPR(expr);
				return EXPR(expr);
			} else {
				g_ptr_array_unref (params);
				RESTORE(begin);
				break;
			}
		}
	}

	return cond(parser);
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
			SmExpr* expr = function(parser, TRUE);
			CHECK(expr);
			NEW(a, SmAssignExpr, SM_ASSIGN_EXPR);
			a->names = names;
			a->value = expr;
			expr->parent = EXPR(a);
			return EXPR(a);
		} else {
			goto rollback;
		}

	rollback:
		g_ptr_array_unref (names);
		RESTORE(begin);
	}

	return cond(parser);
}

FUNC(seq) {
	SmExpr* expr = assign(parser);
	CHECK(expr);
	if (expr->type != SM_ASSIGN_EXPR) {
		return expr;
	}

	NEW(seq, SmSeqExpr, SM_SEQ_EXPR);
	seq->assigns = g_ptr_array_new ();
	while (ACCEPT(";")) {
		g_ptr_array_add (seq->assigns, expr);

		expr = assign(parser);
		CHECK(expr);
		expr->parent = EXPR(seq);
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

