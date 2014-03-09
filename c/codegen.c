#include <glib.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "codegen.h"

typedef struct {
	int use_temps;
	int sp;
	int hp;
} SmClosureData;

struct _SmCodegen {
	SmCodegenOpts opts;
	SmCode* code;
	SmCodeBlock* decls;
	SmScope* scope;
	GQueue* closure_stack;
	int cur_scopeid;
	int next_closureid;
	int sp; // stack pointer
	int hp; // heap pointer
};

SmCodegen* sm_codegen_new (SmCodegenOpts opts) {
	SmCodegen* gen = g_new0 (SmCodegen, 1);
	gen->opts = opts;
	SmCode* code = sm_code_new ();
	gen->code = code;
	gen->scope = sm_scope_new (NULL);
	gen->closure_stack = g_queue_new ();
	
	gen->decls = sm_code_new_block (code);

	return gen;
}

SmCode* sm_codegen_get_code (SmCodegen* gen) {
	return gen->code;
}

SmScope* sm_codegen_get_scope (SmCodegen* gen) {
	return gen->scope;
}

/* STACK POINTER management */

int sm_codegen_get_stack_pointer (SmCodegen* gen) {
	return gen->sp;
}

int sm_codegen_sp_get (SmCodegen* gen, int x, const char* cast) {
	GET_CODE;
	COMMENT("sp[%d]", x);
	int sp = GETPTR("i64* %%%d, i32 %d", gen->sp, x);
	int val = LOAD("i64* %%%d", sp);
	if (cast) {
		val = TOPTR("i64 %%%d", "%s", val, cast);
	}
	return val;
}

void sm_codegen_sp_set (SmCodegen* gen, int x, int v, const char* cast) {
	GET_CODE;
	COMMENT("sp[%d] = %%%d", x, v);
	if (cast) {
		v = TOINT("%s %%%d", "i64", cast, v);
	}
	int sp = GETPTR("i64* %%%d, i32 %d", gen->sp, x)
	STORE("i64 %%%d", "i64* %%%d", v, sp);
}

// set and update the sp
void sm_codegen_fin_sp (SmCodegen* gen, int x, int v, const char* cast) {
	GET_CODE;
	COMMENT("sp[%d] = %%%d", x, v);
	if (cast) {
		v = TOINT("%s %%%d", "i64", cast, v);
	}
	gen->sp = GETPTR("i64* %%%d, i32 %d", gen->sp, x)
	STORE("i64 %%%d", "i64* %%%d", v, gen->sp);
}

void sm_codegen_var_sp (SmCodegen* gen, int x) {
	if (!x) {
		return;
	}
	
	GET_CODE;
	COMMENT("sp += %d", x);
	gen->sp = GETPTR("i64* %%%d, i32 %d", gen->sp, x);
}

void sm_codegen_set_stack_pointer (SmCodegen* gen, int x) {
	gen->sp = x;
}

/* HEAP POINTER management */

int sm_codegen_get_heap_pointer (SmCodegen* gen) {
	return gen->hp;
}

int sm_codegen_hp_get (SmCodegen* gen, int x, const char* cast) {
	GET_CODE;
	COMMENT("hp[%d]", x);
	int hp = GETPTR("i64* %%%d, i32 %d", gen->hp, x);
	int val = LOAD("i64* %%%d", hp);
	if (cast) {
		val = TOPTR("i64 %%%d", "%s", val, cast);
	}
	return val;
}

void sm_codegen_hp_set (SmCodegen* gen, int x, int v, const char* cast) {
	GET_CODE;
	COMMENT("hp[%d] = %%%d", x, v);
	if (cast) {
		v = TOINT("%s %%%d", "i64", cast, v);
	}
	int hp = GETPTR("i64* %%%d, i32 %d", gen->hp, x)
	STORE("i64 %%%d", "i64* %%%d", v, hp);
}

// set and update the hp
void sm_codegen_fin_hp (SmCodegen* gen, int x, int v, const char* cast) {
	GET_CODE;
	COMMENT("hp[%d] = %%%d", x, v);
	if (cast) {
		v = TOINT("%s %%%d", "i64", cast, v);
	}
	gen->hp = GETPTR("i64* %%%d, i32 %d", gen->hp, x)
	STORE("i64 %%%d", "i64* %%%d", v, gen->hp);
}

void sm_codegen_var_hp (SmCodegen* gen, int x) {
	if (!x) {
		return;
	}
	
	GET_CODE;
	COMMENT("hp += %d", x);
	gen->hp = GETPTR("i64* %%%d, i32 %d", gen->hp, x);
}

void sm_codegen_set_heap_pointer (SmCodegen* gen, int x) {
	gen->hp = x;
}

void sm_codegen_enter (SmCodegen* gen, int closure) {
	GET_CODE;
	int funcptr = GETPTR("%%closure* %%%d, i32 0, i32 %d", closure, CLOSURE_FUNC);
	int func = LOAD("%%closurefunc* %%%d", funcptr);
	TAILCALL_ ("void %%%d(%%closure* %%%d, i64* %%%d, i64* %%%d)", func, closure, gen->sp, gen->hp);
	RET("void");
}

SmCodeBlock* sm_codegen_get_decls_block (SmCodegen* gen) {
	return gen->decls;
}


int sm_codegen_get_use_temps (SmCodegen* gen) {
	SmClosureData *data = (SmClosureData*) g_queue_peek_tail (gen->closure_stack);
	return data ? data->use_temps : FALSE;
}

void sm_codegen_set_use_temps (SmCodegen* gen, int use_temps) {
	SmClosureData *data = (SmClosureData*) g_queue_peek_tail (gen->closure_stack);
	assert (data);
	data->use_temps = TRUE;
}

int sm_codegen_begin_closure_func (SmCodegen* gen) {
	GET_CODE;
	
	int closureid = gen->next_closureid++;
	PUSH_NEW_BLOCK;
	BEGIN_FUNC("fastcc void", "closure_%d_eval", "%%closure*, i64*, i64*", closureid);
	
	(void) sm_code_get_temp (code); // first param, self closure
	int sp = sm_code_get_temp (code); // second param, stack pointer
	int hp = sm_code_get_temp (code); // third param, heap pointer
	LABEL("entry");
	
	SmClosureData* data = g_new0 (SmClosureData, 1);
	// save sp
	data->sp = gen->sp;
	gen->sp = sp;
	// save hp
	data->hp = gen->hp;
	gen->hp = hp;
	g_queue_push_tail (gen->closure_stack, data);
	/* if (!nparams) { */
	/* // next call will point to the cache */
	/* COMMENT("jump to cache at next call"); */
	/* int funcptr = GETPTR("%%closure* %%%d", "i32 0, i32 %d", closure, CLOSURE_FUNC); */
	/* int func = BITCAST("%%tagged (%%closure*)* " FUNC("thunk_cache"), "%%tagged (%%closure*, ...)*"); */
	/* STORE("%%closurefunc %%%d", "%%closurefunc* %%%d", func, funcptr); */
	/* } else { */
	/* // reserve for parameters */
	/* for (int i=0; i < nparams; i++) { */
	/* sm_code_get_temp (code); */
	/* } */
	/* } */
	return closureid;
}

void sm_codegen_end_closure_func (SmCodegen* gen) {
	GET_CODE;
	END_FUNC;
	POP_BLOCK;
	
	SmClosureData* data = g_queue_pop_tail (gen->closure_stack);
	gen->sp = data->sp;
	gen->hp = data->hp;
}


int sm_codegen_create_custom_closure (SmCodegen* gen, int scope_size, int closureid) {
	GET_CODE;

	VARHP(-(int)(sizeof(void*)*CLOSURE_SCOPE+sizeof(void*)*scope_size));
	int closure = LOADHP;
	closure = BITCAST("i64* %%%d", "%%closure*", closure);

	COMMENT("store update function");
	int funcptr = GETPTR("%%closure* %%%d, i32 0, i32 %d", closure, CLOSURE_FUNC);
	STORE("%%closurefunc " FUNC("closure_%d_eval"), "%%closurefunc* %%%d", closureid, funcptr);

	return closure;
}

int sm_codegen_allocate_closure (SmCodegen* gen) {
	GET_CODE;
	
	int parent_size = sm_scope_get_size (sm_scope_get_parent (gen->scope));
	int local_size = sm_scope_get_local_size (gen->scope);
	COMMENT("alloc closure with %d vars", parent_size+local_size);
	VARHP(-(int)(sizeof(void*)*CLOSURE_SCOPE+sizeof(void*)*(parent_size+local_size)));
	int closure = LOADHP;
	closure = BITCAST("i64* %%%d", "%%closure*", closure);
	return closure;
}

static int create_thunk_cache_func (SmCodegen* gen) {
	static int thunk_cache = -1;
	if (thunk_cache >= 0) {
		return thunk_cache;
	}

	GET_CODE;
	thunk_cache = sm_codegen_begin_closure_func (gen);
	COMMENT("cached func");
	RUNDBG("-> cached func, sp=%p\n", LOADSP, "i64*");
	int cont = SPGET(0, "%closure*");

	COMMENT("get cached object");
	int ptr = GETPTR("%%closure* %%0, i32 0, i32 %d", CLOSURE_CACHE);
	int obj = LOAD("%%tagged* %%%d", ptr);
	SPSET(0, obj, NULL);
	
	RUNDBG("enter cont %p\n", cont, "%closure*");
	ENTER(cont);
	sm_codegen_end_closure_func (gen);

	return thunk_cache;
}

void sm_codegen_init_update_frame (SmCodegen* gen) {
	static int initialized = FALSE;
	if (initialized) {
		return;
	}
	
	initialized = TRUE;
	GET_CODE;
	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("update func");
	RUNDBG("-> update func, sp=%p\n", LOADSP, "i64*");
	int obj = SPGET(0, NULL);
	int thunk = SPGET(1, "%closure*");
	int cont = SPGET(2, "%closure*");
	
	COMMENT("change function to use the cache");
	int funcptr = GETPTR("%%closure* %%%d, i32 0, i32 %d", thunk, CLOSURE_FUNC);
	STORE("%%closurefunc " FUNC("closure_%d_eval"), "%%closurefunc* %%%d", create_thunk_cache_func (gen), funcptr);

	COMMENT("cache result object");
	int ptr = GETPTR("%%closure* %%%d, i32 0, i32 %d", thunk, CLOSURE_CACHE);
	STORE("%%tagged %%%d", "%%tagged* %%%d", obj, ptr);

	RUNDBG("object to cache %p\n", obj, NULL);
	
	FINSP(2, obj, NULL);
	RUNDBG("enter cont %p\n", cont, "%closure*");
	ENTER(cont);
	sm_codegen_end_closure_func (gen);

	int closure = sm_codegen_create_custom_closure (gen, 0, closureid);
	
	// set global variable to the update closure func
	PUSH_BLOCK(sm_codegen_get_decls_block (gen));
	EMIT_ ("@updatethunk = global %%closure* null, align 8");
	POP_BLOCK;

	STORE("%%closure* %%%d", "%%closure** @updatethunk", closure);
}

int sm_codegen_push_update_frame (SmCodegen* gen, int offset) {
	GET_CODE;
	COMMENT("push update frame");
	SPSET(offset, 0, "%closure*");

	int update_thunk = LOAD("%%closure** @updatethunk");
	SPSET(offset-1, update_thunk, "%closure*");

	return offset-2;
}

int sm_codegen_create_closure (SmCodegen* gen, int closureid, int prealloc) {
	GET_CODE;
	int parent_size = sm_scope_get_size (sm_scope_get_parent (gen->scope));
	int local_size = sm_scope_get_local_size (gen->scope);
	int closure;
	if (prealloc >= 0) {
		closure = prealloc;
	} else {
		closure = sm_codegen_allocate_closure (gen);
	}
	
	COMMENT("store closure function");
	int funcptr = GETPTR("%%closure* %%%d, i32 0, i32 %d", closure, CLOSURE_FUNC);
	STORE("%%closurefunc " FUNC("closure_%d_eval"), "%%closurefunc* %%%d", closureid, funcptr);
	
	if (gen->scope) {
		// 0 = this closure
		if (sm_codegen_get_use_temps (gen)) {
			COMMENT("use temps");
			COMMENT("capture parent scope");
			for (int i=0; i < parent_size; i++) {
				int srcptr = GETPTR("%%closure* %%0, i32 0, i32 %d, i32 %d", CLOSURE_SCOPE, i);
				int destptr = GETPTR("%%closure* %%%d, i32 0, i32 %d, i32 %d", closure, CLOSURE_SCOPE, i);
				int src = LOAD("%%closure** %%%d", srcptr);
				STORE("%%closure* %%%d", "%%closure** %%%d", src, destptr);
			}
			
			COMMENT("capture params+locals");
			for (int i=0; i < local_size; i++) {
				int destptr = GETPTR("%%closure* %%%d, i32 0, i32 %d, i32 %d", closure, CLOSURE_SCOPE, i+parent_size);
				int src = SPGET(i, "%closure*");
				STORE("%%closure* %%%d", "%%closure** %%%d", src, destptr);
			}
		} else {
			COMMENT("capture scope from within a thunk");
			for (int i=0; i < parent_size+local_size; i++) {
				int srcptr = GETPTR("%%closure* %%0, i32 0, i32 %d, i32 %d", CLOSURE_SCOPE, i);
				int destptr = GETPTR("%%closure* %%%d, i32 0, i32 %d, i32 %d", closure, CLOSURE_SCOPE, i);
				int src = LOAD("%%closure** %%%d", srcptr);
				STORE("%%closure* %%%d", "%%closure** %%%d", src, destptr);
			}
		}
	}
	
	return closure;
}


void sm_codegen_debug (SmCodegen* gen, const char* fmt, int var, const char* cast) {
	if (!gen->opts.debug) {
		return;
	}
	
	GET_CODE;
	
	int len = strlen(fmt)+1;
	PUSH_BLOCK(sm_codegen_get_decls_block (gen));
	int consttmp = sm_code_get_temp (code);
	EMIT_ ("@.const%d = private constant [%d x i8] c\"%s\\00\", align 8", consttmp, len, fmt);
	POP_BLOCK;
	
	if (cast) {
		var = TOINT("%s %%%d", "i64", cast, var);
	}
	int strptr = BITCAST("[%d x i8]* @.const%d", "i8*", len, consttmp);
	CALL ("i32 (i8*, ...)* @printf(i8* %%%d, i64 %%%d)", strptr, var);
}

void sm_codegen_set_scope (SmCodegen* gen, SmScope* scope) {
	gen->scope = scope;
}
