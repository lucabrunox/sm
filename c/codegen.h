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

#define LOADSP sm_codegen_get_stack_pointer(gen)
#define FINSP(x,v,c) sm_codegen_fin_sp(gen, x, v, c)
#define VARSP(x) sm_codegen_var_sp(gen, x)
#define SPGET(x,c) sm_codegen_sp_get(gen, x, c)
#define SPSET(x,v,c) sm_codegen_sp_set(gen, x, v, c)

#define LOADHP sm_codegen_get_heap_pointer(gen)
#define FINHP(x,v,c) sm_codegen_fin_hp(gen, x, v, c)
#define VARHP(x) sm_codegen_var_hp(gen, x)
#define HPGET(x,c) sm_codegen_hp_get(gen, x, c)
#define HPSET(x,v,c) sm_codegen_hp_set(gen, x, v, c)

#define ENTER(x) sm_codegen_enter(gen, x)
#define BREAKPOINT CALL_("void @llvm.debugtrap()")
#define RUNDBG(f,x,c) sm_codegen_debug(gen, f, x, c)

/* Currently favoring doubles, will change in the future to favor either lists or functions */
#define DBL_qNAN 0x7FF8000000000000ULL
#define TAG_MASK 0x7FFF000000000000ULL
#define OBJ_MASK 0x0000FFFFFFFFFFFFULL
#define TAG_FUN DBL_qNAN|(1ULL << 48)
#define TAG_LST DBL_qNAN|(2ULL << 48)
#define TAG_INT DBL_qNAN|(3ULL << 48)
#define TAG_CHR DBL_qNAN|(4ULL << 48)
#define TAG_STR DBL_qNAN|(5ULL << 48) // constant string
#define TAG_EXC DBL_qNAN|(6ULL << 48) // exception, carries an object
#define TAG_OBJ DBL_qNAN|(7ULL << 48)

#define OBJ_FALSE (TAG_OBJ)
#define OBJ_TRUE (TAG_OBJ|(1ULL))
#define OBJ_EOS (TAG_OBJ|(2ULL))

#define LIST_SIZE 0
#define LIST_ELEMS 1

#define CLOSURE_FUNC 0
#define CLOSURE_CACHE 1
#define CLOSURE_SCOPE 2

typedef enum {
	TYPE_FUN,
	TYPE_LST,
	TYPE_EOS,
	TYPE_INT,
	TYPE_CHR,
	TYPE_STR,
	TYPE_BOOL,
	TYPE_UNK // unknown at compile time
} SmVarType;

typedef struct {
	int id;
	int isthunk;
	SmVarType type;
} SmVar;

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

int sm_codegen_try_var (SmCodegen* gen, SmVar var, SmVarType type);

int sm_codegen_get_stack_pointer (SmCodegen* gen);
void sm_codegen_set_stack_pointer (SmCodegen* gen, int x);
int sm_codegen_sp_get (SmCodegen* gen, int x, const char* cast);
void sm_codegen_sp_set (SmCodegen* gen, int x, int v, const char* cast);
void sm_codegen_fin_sp (SmCodegen* gen, int x, int v, const char* cast);
void sm_codegen_var_sp (SmCodegen* gen, int x);

int sm_codegen_get_heap_pointer (SmCodegen* gen);
void sm_codegen_set_heap_pointer (SmCodegen* gen, int x);
int sm_codegen_hp_get (SmCodegen* gen, int x, const char* cast);
void sm_codegen_hp_set (SmCodegen* gen, int x, int v, const char* cast);
void sm_codegen_fin_hp (SmCodegen* gen, int x, int v, const char* cast);
void sm_codegen_var_hp (SmCodegen* gen, int x);

void sm_codegen_enter (SmCodegen* gen, int closure);

int sm_codegen_begin_closure_func (SmCodegen* gen);
void sm_codegen_end_closure_func (SmCodegen* gen);
int sm_codegen_allocate_closure (SmCodegen* gen);
int sm_codegen_create_closure (SmCodegen* gen, int closureid, int prealloc);
int sm_codegen_create_custom_closure (SmCodegen* gen, int scope_size, int closureid);

void sm_codegen_init_update_frame (SmCodegen* gen);
int sm_codegen_push_update_frame (SmCodegen* gen, int offset);

void sm_codegen_debug (SmCodegen* gen, const char* fmt, int var, const char* cast);

#endif