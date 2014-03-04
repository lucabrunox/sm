#ifndef SM_CODEGEN_H
#define SM_CODEGEN_H

#include "ast.h"
#include "llvm.h"

typedef struct {
	int debug;
} SmCodegenOpts;

SmJit* sm_compile (SmCodegenOpts opts, const char* name, SmExpr* expr);
void sm_run (SmJit* mod);

#endif