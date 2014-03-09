#include <glib.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "codegen.h"

typedef struct {
	int use_temps;
} SmClosureData;

struct _SmCodegen {
	SmCodegenOpts opts;
	SmCode* code;
	SmCodeBlock* decls;
	SmScope* scope;
	GQueue* closure_stack;
	int cur_scopeid;
	int next_closureid;
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

int sm_codegen_load_sp (SmCodegen* gen) {
	GET_CODE;
	return LOAD("i64** @sp");
}

int sm_codegen_sp_get (SmCodegen* gen, int sp, int x, const char* cast) {
	GET_CODE;
	COMMENT("sp[%d]", x);
	sp = GETPTR("i64* %%%d, i32 %d", sp, x);
	int val = LOAD("i64* %%%d", sp);
	if (cast) {
		val = TOPTR("i64 %%%d", "%s", val, cast);
	}
	return val;
}

void sm_codegen_sp_set (SmCodegen* gen, int sp, int x, int v, const char* cast) {
	GET_CODE;
	COMMENT("sp[%d] = %%%d", x, v);
	if (cast) {
		v = TOINT("%s %%%d", "i64", cast, v);
	}
	sp = GETPTR("i64* %%%d, i32 %d", sp, x)
	STORE("i64 %%%d", "i64* %%%d", v, sp);
}

// set and update the sp
int sm_codegen_fin_sp (SmCodegen* gen, int sp, int x, int v, const char* cast) {
	GET_CODE;
	COMMENT("sp[%d] = %%%d", x, v);
	if (cast) {
		v = TOINT("%s %%%d", "i64", cast, v);
	}
	sp = GETPTR("i64* %%%d, i32 %d", sp, x)
	STORE("i64 %%%d", "i64* %%%d", v, sp);
	if (x) {
		COMMENT("sp += %d", x);
		STORE("i64* %%%d", "i64** @sp", sp);
	}
	return sp;
}

int sm_codegen_var_sp (SmCodegen* gen, int sp, int x) {
	if (!x) {
		return sp;
	}
	
	GET_CODE;
	COMMENT("sp += %d", x);
	sp = GETPTR("i64* %%%d, i32 %d", sp, x);
	STORE("i64* %%%d", "i64** @sp", sp);
	return sp;
}

void sm_codegen_enter (SmCodegen* gen, int closure) {
	GET_CODE;
	int funcptr = GETPTR("%%closure* %%%d, i32 0, i32 %d", closure, CLOSURE_FUNC);
	int func = LOAD("%%closurefunc* %%%d", funcptr);
	TAILCALL_ ("void %%%d(%%closure* %%%d)", func, closure);
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
	BEGIN_FUNC("fastcc void", "closure_%d_eval", "%%closure*", closureid);
	
	(void) sm_code_get_temp (code); // first param
	LABEL("entry");
	
	SmClosureData* data = g_new0 (SmClosureData, 1);
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
	
	g_queue_pop_tail (gen->closure_stack);
}

int sm_codegen_allocate_closure (SmCodegen* gen) {
	GET_CODE;
	
	int parent_size = sm_scope_get_size (sm_scope_get_parent (gen->scope));
	int local_size = sm_scope_get_local_size (gen->scope);
	COMMENT("alloc closure with %d vars", parent_size+local_size);
	int closure = CALL("i8* @aligned_alloc(i32 8, i32 %lu)",
					 sizeof(void*)*CLOSURE_SCOPE+sizeof(void*)*(parent_size+local_size));
	closure = BITCAST("i8* %%%d", "%%closure*", closure);
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
	int sp = LOADSP;
	RUNDBG("-> cached func, sp=%p\n", sp, "i64*");
	int cont = SPGET(sp, 0, "%closure*");

	COMMENT("get cached object");
	int ptr = GETPTR("%%closure* %%0, i32 0, i32 %d", CLOSURE_CACHE);
	int obj = LOAD("%%tagged* %%%d", ptr);
	SPSET(sp, 0, obj, NULL);
	
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
	int sp = LOADSP;
	RUNDBG("-> update func, sp=%p\n", sp, "i64*");
	int obj = SPGET(sp, 0, NULL);
	int thunk = SPGET(sp, 1, "%closure*");
	int cont = SPGET(sp, 2, "%closure*");
	
	COMMENT("change function to use the cache");
	int funcptr = GETPTR("%%closure* %%%d, i32 0, i32 %d", thunk, CLOSURE_FUNC);
	STORE("%%closurefunc " FUNC("closure_%d_eval"), "%%closurefunc* %%%d", create_thunk_cache_func (gen), funcptr);

	COMMENT("cache result object");
	int ptr = GETPTR("%%closure* %%%d, i32 0, i32 %d", thunk, CLOSURE_CACHE);
	STORE("%%tagged %%%d", "%%tagged* %%%d", obj, ptr);

	RUNDBG("object to cache %p\n", obj, NULL);
	
	FINSP(sp, 2, obj, NULL);
	RUNDBG("enter cont %p\n", cont, "%closure*");
	ENTER(cont);
	sm_codegen_end_closure_func (gen);

	COMMENT("alloc update closure");
	int closure = CALL("i8* @aligned_alloc(i32 8, i32 %lu)", sizeof(void*)*CLOSURE_SCOPE);
	closure = BITCAST("i8* %%%d", "%%closure*", closure);

	COMMENT("store update function");
	funcptr = GETPTR("%%closure* %%%d, i32 0, i32 %d", closure, CLOSURE_FUNC);
	STORE("%%closurefunc " FUNC("closure_%d_eval"), "%%closurefunc* %%%d", closureid, funcptr);

	// set global variable to the update closure func
	PUSH_BLOCK(sm_codegen_get_decls_block (gen));
	EMIT_ ("@updatethunk = global %%closure* null, align 8");
	POP_BLOCK;

	STORE("%%closure* %%%d", "%%closure** @updatethunk", closure);
}

int sm_codegen_push_update_frame (SmCodegen* gen, int sp, int offset) {
	GET_CODE;
	COMMENT("push update frame");
	SPSET(sp, offset, 0, "%closure*");

	int update_thunk = LOAD("%%closure** @updatethunk");
	SPSET(sp, offset-1, update_thunk, "%closure*");

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
			int sp = LOADSP;
			for (int i=0; i < local_size; i++) {
				int destptr = GETPTR("%%closure* %%%d, i32 0, i32 %d, i32 %d", closure, CLOSURE_SCOPE, i+parent_size);
				int src = SPGET(sp, i, "%closure*");
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
