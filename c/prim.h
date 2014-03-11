#ifndef SM_PRIM_H
#define SM_PRIM_H

#include "codegen.h"

void sm_prim_init_op (SmCodegen* gen, const char* prim_name, const char* prim_func);
void sm_prim_init_cond (SmCodegen* gen);
void sm_prim_init_eos (SmCodegen* gen);

#endif