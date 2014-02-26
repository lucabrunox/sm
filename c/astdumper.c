#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "uthash/src/utlist.h"

#include "ast.h"
#include "astdumper.h"

static char* str(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	char* res;
	vasprintf(&res, fmt, ap);
	va_end (ap);
	return res;
}

#define FUNC(n,x) static char* n (x* expr)

FUNC(dump_member_expr, SmMemberExpr) {
	if (expr->inner) {
		char* inner = sm_ast_dump (expr->inner);
		char* res = str("%s.%s", inner, expr->name);
		free (inner);
		return res;
	} else {
		return strdup(expr->name);
	}
}

FUNC(dump_assign_expr, SmAssignExpr) {
	char* names = strdup("");
	char* old;
	
	char** p = NULL;
	int first = 1;
	while ((p=(char**)utarray_next(expr->names, p))) {
		old = names;
		if (first) {
			names = strdup (*p);
			first = 0;
		} else {
			names = str("%s, %s", names, *p);
		}
		free (old);
	}

	old = names;
	char* inner = sm_ast_dump (expr->value);
	names = str("%s = %s", names, inner);
	free (old);
	free (inner);
	return names;
}

FUNC(dump_seq_expr, SmSeqExpr) {
	char* res = strdup("(");
	char* old;
	char* inner;
	SmAssignList* a;
	DL_FOREACH(expr->assigns, a) {
		old = res;
		inner = sm_ast_dump (EXPR(a->expr));
		res = str("%s%s;\n", res, inner);
		free (inner);
		free (old);
	}

	old = res;
	inner = sm_ast_dump (expr->result);
	res = str("%s%s)", res, inner);
	free (old);
	free (inner);
	return res;
}

FUNC(dump_literal, SmLiteral) {
	char* res;
	if (expr->str) {
		// FIXME: escape
		asprintf(&res, "\"%s\"", expr->str);
	} else {
		asprintf(&res, "%g", expr->num);
	}
	return res;
}

#define CAST(x) (char* (*)(SmExpr*))(x)
char* (*dump_table[])(SmExpr*) = {
	[SM_MEMBER_EXPR] = CAST(dump_member_expr),
	[SM_SEQ_EXPR] = CAST(dump_seq_expr),
	[SM_ASSIGN_EXPR] = CAST(dump_assign_expr),
	[SM_LITERAL] = CAST(dump_literal)
};

char* sm_ast_dump (SmExpr* expr) {
	return dump_table[expr->type] (expr);
}
