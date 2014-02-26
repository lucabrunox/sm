#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "uthash/src/utlist.h"

#include "ast.h"
#include "astdumper.h"

#define EXPR(x) ((SmExpr*) x)

static char* str(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	char* res;
	vasprintf(&res, fmt, ap);
	va_end (ap);
	return res;
}

static char* dump_member_expr (SmMemberExpr* expr) {
	if (expr->inner) {
		char* inner = sm_ast_dump (expr->inner);
		char* res = str("%s.%s", inner, expr->name);
		free (inner);
		return res;
	} else {
		return strdup(expr->name);
	}
}

static char* dump_assign_expr (SmAssignExpr* expr) {
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

static char* dump_seq_expr (SmSeqExpr* expr) {
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

#define CAST(x) (char* (*)(SmExpr*))(x)
char* (*dump_table[])(SmExpr*) = {
	[SM_MEMBER_EXPR] = CAST(dump_member_expr),
	[SM_SEQ_EXPR] = CAST(dump_seq_expr),
	[SM_ASSIGN_EXPR] = CAST(dump_assign_expr)
};

char* sm_ast_dump (SmExpr* expr) {
	return dump_table[expr->type] (expr);
}
