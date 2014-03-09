#ifndef SM_CODEGEN_H
#define SM_CODEGEN_H

#include "ast.h"
#include "llvm.h"
#include "code.h"
#include "scope.h"

#define GET_CODE SmCode* code = sm_codegen_get_code(gen)
#define PUSH_BLOCK(x) sm_code_push_block(code, x)
#define POP_BLOCK sm_code_pop_block(code)
#define PUSH_NEW_BLOCK PUSH_BLOCK(sm_code_new_block (code))
#define LOADSP sm_codegen_load_sp(gen)
#define FINSP(sp,x,v,c) sm_codegen_fin_sp(gen, sp, x, v, c)
#define VARSP(sp,x) sm_codegen_var_sp(gen, sp, x)
#define SPGET(sp,x,c) sm_codegen_sp_get(gen, sp, x, c)
#define SPSET(sp,x,v,c) sm_codegen_sp_set(gen, sp, x, v, c)
#define ENTER(x) sm_codegen_enter(gen, x)
#define BREAKPOINT CALL_("void @llvm.debugtrap()")
#define RUNDBG(f,x,c) sm_codegen_debug(gen, f, x, c)

#define CLOSURE_FUNC 0
#define CLOSURE_CACHE 1
#define CLOSURE_SCOPE 2

typedef struct {
	int debug;
} SmCodegenOpts;

typedef struct _SmCodegen SmCodegen;

SmCodegen* sm_codegen_new (SmCodegenOpts opts);

SmCode* sm_codegen_get_code (SmCodegen* gen);
SmCodeBlock* sm_codegen_get_decls_block (SmCodegen* gen);

SmScope* sm_codegen_get_scope (SmCodegen* gen);
void sm_codegen_set_scope (SmCodegen* gen, SmScope* scope);

int sm_codegen_get_use_temps (SmCodegen* gen);
void sm_codegen_set_use_temps (SmCodegen* gen, int use_temps);

int sm_codegen_load_sp (SmCodegen* gen);
int sm_codegen_sp_get (SmCodegen* gen, int sp, int x, const char* cast);
void sm_codegen_sp_set (SmCodegen* gen, int sp, int x, int v, const char* cast);
int sm_codegen_fin_sp (SmCodegen* gen, int sp, int x, int v, const char* cast);
int sm_codegen_var_sp (SmCodegen* gen, int sp, int x);
void sm_codegen_enter (SmCodegen* gen, int closure);

int sm_codegen_begin_closure_func (SmCodegen* gen);
void sm_codegen_end_closure_func (SmCodegen* gen);
int sm_codegen_allocate_closure (SmCodegen* gen);
int sm_codegen_create_closure (SmCodegen* gen, int closureid, int prealloc);
int sm_codegen_push_update_frame (SmCodegen* gen, int sp, int offset);

void sm_codegen_debug (SmCodegen* gen, const char* fmt, int var, const char* cast);

#endif