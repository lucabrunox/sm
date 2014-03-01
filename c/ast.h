#ifndef SM_AST_H
#define SM_AST_H

#include <glib.h>

#define EXPR(x) ((SmExpr*)x)

typedef enum {
	SM_MEMBER_EXPR,
	SM_SEQ_EXPR,
	SM_ASSIGN_EXPR,
	SM_LITERAL,
	SM_FUNC_EXPR
} SmExprType;

typedef struct _SmExpr SmExpr;

struct _SmExpr {
	SmExprType type;
	SmExpr* parent;
};

typedef struct {
	SmExpr base;
	SmExpr* inner;
	char* name;
} SmMemberExpr;

typedef struct {
	SmExpr base;
	GPtrArray* names;
	SmExpr* value;
} SmAssignExpr;

typedef struct _SmAssignList SmAssignList;

typedef struct {
	SmExpr base;
	GPtrArray* assigns;
	SmExpr* result;
} SmSeqExpr;

typedef struct {
	SmExpr base;
	GPtrArray* params;
	SmExpr* body;
} SmFuncExpr;

typedef struct {
	SmExpr base;
	union {
		double num;
		char* str;
	};
} SmLiteral;

#endif