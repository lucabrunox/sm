#ifndef SM_AST_H
#define SM_AST_H

#include <glib.h>

#define EXPR(x) ((SmExpr*)x)

typedef enum {
	SM_MEMBER_EXPR,
	SM_SEQ_EXPR,
	SM_ASSIGN_EXPR,
	SM_LITERAL
} SmExprType;

typedef struct {
	SmExprType type;
} SmExpr;

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

struct _SmAssignList {
	SmAssignExpr* expr;
	SmAssignList* next;
	SmAssignList* prev;
};

typedef struct {
	SmExpr base;
	SmAssignList* assigns;
	SmExpr* result;
} SmSeqExpr;

typedef struct {
	SmExpr base;
	union {
		double num;
		char* str;
	};
} SmLiteral;

#endif