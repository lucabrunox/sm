#ifndef SM_COMPILE_H
#define SM_COMPILE_H

#include "ast.h"
#include "llvm.h"

typedef struct {
	int debug;
} SmCompileOpts;

SmJit* sm_compile (SmCompileOpts opts, const char* name, SmExpr* expr);
void sm_run (SmJit* mod);

#endif