#ifndef SM_AST_H
#define SM_AST_H

#include <glib.h>

#define EXPR(x) ((SmExpr*)x)

typedef enum {
	SM_MEMBER_EXPR,
	SM_LET_EXPR,
	SM_ASSIGN_EXPR,
	SM_STR_LITERAL,
	SM_INT_LITERAL,
	SM_CHR_LITERAL,
	SM_DBL_LITERAL,
	SM_FUNC_EXPR,
	SM_CALL_EXPR,
	SM_BINARY_EXPR,
	SM_COND_EXPR,
	SM_LIST_EXPR
} SmExprType;

typedef struct _SmExpr SmExpr;

struct _SmExpr {
	SmExprType type;
	SmExpr* parent;
};

char* sm_ast_dump (SmExpr* expr);


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
	int nlocals;
	SmExpr* result;
} SmLetExpr;

typedef struct {
	SmExpr base;
	GPtrArray* params;
	SmExpr* body;
} SmFuncExpr;

typedef struct {
	SmExpr base;
	SmExpr* func;
	GPtrArray* args;
} SmCallExpr;

typedef struct {
	SmExpr base;
	union {
		double dblval;
		int intval;
		char chr;
		char* str;
	};
} SmLiteral;

typedef struct {
	SmExpr base;
	const char* op;
	SmExpr* left;
	SmExpr* right;
} SmBinaryExpr;

typedef struct {
	SmExpr base;
	SmExpr* cond;
	SmExpr* truebody;
	SmExpr* falsebody;
} SmCondExpr;

typedef struct {
	SmExpr base;
	GPtrArray* elems;
} SmListExpr;

#endif