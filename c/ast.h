#ifndef SM_AST_H
#define SM_AST_H

#include <glib.h>

#define EXPR(x) ((SmExpr*)x)

typedef enum {
	SM_MEMBER_EXPR,
	SM_SEQ_EXPR,
	SM_ASSIGN_EXPR,
	SM_LITERAL,
	SM_FUNC_EXPR,
	SM_CALL_EXPR
} SmExprType;

typedef enum {
	SM_AND_OP,
	SM_OR_OP,
	SM_ADD_OP,
	SM_SUB_OP,
	SM_DIV_OP,
	SM_MUL_OP,
	SM_EQ_OP,
	SM_NEQ_OP,
	SM_LT_OP,
	SM_GT_OP,
	SM_LE_OP,
	SM_GE_OP
} SmBinaryOp;

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
	SmExpr* func;
	GPtrArray* args;
} SmCallExpr;

typedef struct {
	SmExpr base;
	union {
		double num;
		char* str;
	};
} SmLiteral;

typedef struct {
	SmExpr base;
	SmBinaryOp op;
	SmExpr* left;
	SmExpr* right;
} SmBinaryExpr;

#endif