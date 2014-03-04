#ifndef SM_COMPILE_H
#define SM_COMPILE_H

#include "ast.h"
#include "llvm.h"
#include "codegen.h"

SmJit* sm_compile (SmCodegenOpts opts, const char* name, SmExpr* expr);
void sm_run (SmJit* mod);

#endif