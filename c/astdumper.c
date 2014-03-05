#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <glib.h>

#include "ast.h"

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

static char* dump_literal (SmLiteral* expr) {
	char* res = NULL;
	if (expr->base.type == SM_STR_LITERAL) {
		// TODO: quote
		asprintf(&res, "\"%s\"", expr->str);
	} else if (expr->base.type == SM_INT_LITERAL) {
		asprintf(&res, "%d", expr->intval);
	}
	return res;
}

static char* dump_assign_expr (SmAssignExpr* expr) {
	char* res = strdup("");
	char* old;

	GPtrArray* names = expr->names;
	for (int i=0; i < names->len; i++) {
		const char* name = (const char*) names->pdata[i];
		old = res;
		if (i==0) {
			res = strdup (name);
		} else {
			res = str("%s, %s", res, name);
		}
		free (old);
	}

	old = res;
	char* inner = sm_ast_dump (expr->value);
	res = str("%s = %s", res, inner);
	free (old);
	free (inner);
	return res;
}

static char* dump_func_expr (SmFuncExpr* expr) {
	char* res = strdup("");
	char* old;

	GPtrArray* params = expr->params;
	for (int i=0; i < params->len; i++) {
		const char* param = (const char*) params->pdata[i];
		old = res;
		if (i==0) {
			res = strdup (param);
		} else {
			res = str("%s %s", res, param);
		}
		free (old);
	}

	old = res;
	char* inner = sm_ast_dump (expr->body);
	res = str("%s: %s", res, inner);
	free (old);
	free (inner);
	return res;
}

static char* dump_seq_expr (SmSeqExpr* expr) {
	char* res = NULL;
	char* old;
	char* inner;
	for (int i=0; i < expr->assigns->len; i++) {
		SmAssignExpr* a = (SmAssignExpr*) expr->assigns->pdata[i];
		old = res;
		inner = sm_ast_dump (EXPR(a));
		res = str("%s%s;\n", res ? res : "", inner);
		free (inner);
		free (old);
	}

	old = res;
	inner = sm_ast_dump (expr->result);
	res = str("%s%s", res ? res : "", inner);
	free (old);
	free (inner);
	return res;
}

static char* dump_call_expr (SmCallExpr* expr) {
	char* func = sm_ast_dump (expr->func);
	char* res = func;
	for (int i=0; i < expr->args->len; i++) {
		char* arg = sm_ast_dump (EXPR(expr->args->pdata[i]));
		char* old = res;
		res = str("%s %s", res, arg);
		free (old);
	}

	return res;
}

static char* dump_binary_expr (SmBinaryExpr* expr) {
	char* left = sm_ast_dump (expr->left);
	char* right = sm_ast_dump (expr->right);
	char* res = str("(%s %s %s)", left, expr->op, right);
	free (left);
	free (right);
	return res;
}

static char* dump_cond_expr (SmCondExpr* expr) {
	char* cond = sm_ast_dump (expr->cond);
	char* truebody = sm_ast_dump (expr->truebody);
	char* falsebody = sm_ast_dump (expr->falsebody);
	char* res = str("(if %s then %s else %s)", cond, truebody, falsebody);
	free (cond);
	free (truebody);
	free (falsebody);
	return res;
}

static char* dump_list_expr (SmListExpr* expr) {
	char* res = strdup("[");
	for (int i=0; i < expr->elems->len; i++) {
		char* elem = sm_ast_dump (EXPR(expr->elems->pdata[i]));
		char* old = res;
		if (i == 0) {
			res = str("%s%s", res, elem);
		} else {
			res = str("%s, %s", res, elem);
		}
		free (old);
		free (elem);
	}
	
	char* old = res;
	res = str("%s]", res);
	free(old);
	return res;
}

#define CAST(x) (char* (*)(SmExpr*))(x)
char* (*dump_table[])(SmExpr*) = {
	[SM_MEMBER_EXPR] = CAST(dump_member_expr),
	[SM_SEQ_EXPR] = CAST(dump_seq_expr),
	[SM_ASSIGN_EXPR] = CAST(dump_assign_expr),
	[SM_STR_LITERAL] = CAST(dump_literal),
	[SM_INT_LITERAL] = CAST(dump_literal),
	[SM_FUNC_EXPR] = CAST(dump_func_expr),
	[SM_CALL_EXPR] = CAST(dump_call_expr),
	[SM_BINARY_EXPR] = CAST(dump_binary_expr),
	[SM_COND_EXPR] = CAST(dump_cond_expr),
	[SM_LIST_EXPR] = CAST(dump_list_expr)
};

char* sm_ast_dump (SmExpr* expr) {
	return dump_table[expr->type] (expr);
}
