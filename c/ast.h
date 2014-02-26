#ifndef SM_AST_H
#define SM_AST_H

#include "uthash/src/utarray.h"

typedef enum {
	SM_MEMBER_EXPR,
	SM_SEQ_EXPR,
	SM_ASSIGN_EXPR
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
	UT_array* names;
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

#endif