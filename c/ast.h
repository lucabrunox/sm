#ifndef SM_AST_H
#define SM_AST_H

typedef enum {
	SM_MEMBER_EXPR
} SmExprType;

typedef struct {
	SmExprType type;
} SmExpr;

typedef struct {
	SmExpr base;
	SmExpr* inner;
	char* name;
} SmMemberExpr;

#endif