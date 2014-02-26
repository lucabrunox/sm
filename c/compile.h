#ifndef SM_COMPILE_H
#define SM_COMPILE_H

#include "ast.h"
#include "llvm.h"

SmJit* sm_compile (SmExpr* expr);
void sm_run (SmJit* mod);

#endif