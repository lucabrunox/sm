#include <assert.h>
#include <stdio.h>
#include <glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "codegen.h"
#include "ast.h"
#include "llvm.h"
#include "code.h"
#include "astdumper.h"
#include "scope.h"

#define DEFUNC(n,x) static SmVar n (SmCodegen* gen, x* expr, int prealloc)
#define GET_CODE SmCode* code = sm_compile_get_code(gen)
#define PUSH_BLOCK(x) sm_code_push_block(code, x)
#define POP_BLOCK sm_code_pop_block(code)
#define RETVAL(x,y,z) SmVar _res_var={.x, .y, .z}; return _res_var
#define VISIT(x) call_compile_table (gen, EXPR(x), -1)
#define PUSH_NEW_BLOCK PUSH_BLOCK(sm_code_new_block (code))
#define LOADSP sm_compile_load_sp(gen)
#define FINSP(sp,x,v,c) sm_compile_fin_sp(gen, sp, x, v, c)
#define VARSP(sp,x) sm_compile_var_sp(gen, sp, x)
#define SPGET(sp,x,c) sm_compile_sp_get(gen, sp, x, c)
#define SPSET(sp,x,v,c) sm_compile_sp_set(gen, sp, x, v, c)
#define ENTER(x) sm_compile_enter(gen, x)
#define BREAKPOINT CALL_("void @llvm.debugtrap()")

#define CLOSURE_FUNC 0
#define CLOSURE_CACHE 1
#define CLOSURE_SCOPE 2

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

typedef enum {
	TYPE_FUN,
	TYPE_LST,
	TYPE_EOS,
	TYPE_INT,
	TYPE_CHR,
	TYPE_STR,
	TYPE_NIL,
	TYPE_UNK // unknown at compile time
} SmVarType;

typedef struct {
	int id;
	int isthunk;
	SmVarType type;
} SmVar;

typedef struct {
	int use_temps;
} SmClosureData;

typedef struct {
	SmCodegenOpts opts;
	SmCode* code;
	SmCodeBlock* decls;
	SmVar ret;
	SmScope* scope;
	GQueue* closure_stack;
	int cur_scopeid;
	int next_closureid;
} SmCodegen;

static SmVar call_compile_table (SmCodegen* gen, SmExpr* expr, int prealloc);

SmCode* sm_compile_get_code (SmCodegen* gen) {
	return gen->code;
}

SmScope* sm_compile_get_scope (SmCodegen* gen) {
	return gen->scope;
}

int sm_compile_load_sp (SmCodegen* gen) {
	GET_CODE;
	return LOAD("i64** @sp");
}

int sm_compile_sp_get (SmCodegen* gen, int sp, int x, const char* cast) {
	GET_CODE;
	COMMENT("sp[%d]", x);
	sp = GETPTR("i64* %%%d, i32 %d", sp, x);
	int val = LOAD("i64* %%%d", sp);
	if (cast) {
		val = TOPTR("i64 %%%d", "%s", val, cast);
	}
	return val;
}

void sm_compile_sp_set (SmCodegen* gen, int sp, int x, int v, const char* cast) {
	GET_CODE;
	COMMENT("sp[%d] = %%%d", x, v);
	if (cast) {
		v = TOINT("%s %%%d", "i64", cast, v);
	}
	sp = GETPTR("i64* %%%d, i32 %d", sp, x)
	STORE("i64 %%%d", "i64* %%%d", v, sp);
}

// set and update the sp
int sm_compile_fin_sp (SmCodegen* gen, int sp, int x, int v, const char* cast) {
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

int sm_compile_var_sp (SmCodegen* gen, int sp, int x) {
	if (!x) {
		return sp;
	}
	
	GET_CODE;
	COMMENT("sp += %d", x);
	sp = GETPTR("i64* %%%d, i32 %d", sp, x);
	STORE("i64* %%%d", "i64** @sp", sp);
	return sp;
}

void sm_compile_enter (SmCodegen* gen, int closure) {
	GET_CODE;
	int funcptr = GETPTR("%%closure* %%%d, i32 0, i32 %d", closure, CLOSURE_FUNC);
	int func = LOAD("%%closurefunc* %%%d", funcptr);
	TAILCALL_ ("void %%%d(%%closure* %%%d)", func, closure);
	RET("void");
}

SmCodeBlock* sm_compile_get_decls_block (SmCodegen* gen) {
	return gen->decls;
}


int sm_compile_get_use_temps (SmCodegen* gen) {
	SmClosureData *data = (SmClosureData*) g_queue_peek_tail (gen->closure_stack);
	return data ? data->use_temps : FALSE;
}

void sm_compile_set_use_temps (SmCodegen* gen, int use_temps) {
	SmClosureData *data = (SmClosureData*) g_queue_peek_tail (gen->closure_stack);
	assert (data);
	data->use_temps = TRUE;
}

int sm_compile_begin_closure_func (SmCodegen* gen) {
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

void sm_compile_end_closure_func (SmCodegen* gen) {
	GET_CODE;
	END_FUNC;
	POP_BLOCK;

	g_queue_pop_tail (gen->closure_stack);
}

int sm_compile_allocate_closure (SmCodegen* gen) {
	GET_CODE;
	
	int parent_size = sm_scope_get_size (sm_scope_get_parent (gen->scope));
	int local_size = sm_scope_get_local_size (gen->scope);
	COMMENT("alloc closure with %d vars", parent_size+local_size);
	int alloc = CALL("i8* @aligned_alloc(i32 8, i32 %lu)",
					 sizeof(void*)*CLOSURE_SCOPE+sizeof(void*)*(parent_size+local_size));
	int closure = BITCAST("i8* %%%d", "%%closure*", alloc);
	return closure;
}

int sm_compile_create_closure (SmCodegen* gen, int closureid, int prealloc) {
	GET_CODE;
	int parent_size = sm_scope_get_size (sm_scope_get_parent (gen->scope));
	int local_size = sm_scope_get_local_size (gen->scope);
	int closure;
	if (prealloc >= 0) {
		closure = prealloc;
	} else {
		closure = sm_compile_allocate_closure (gen);
	}

	COMMENT("store closure function");
	int funcptr = GETPTR("%%closure* %%%d, i32 0, i32 %d", closure, CLOSURE_FUNC);
	STORE("%%closurefunc " FUNC("closure_%d_eval"), "%%closurefunc* %%%d", closureid, funcptr);

	if (gen->scope) {
		// 0 = this closure
		if (sm_compile_get_use_temps (gen)) {
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

static long long unsigned int tagmap[] = {
	[TYPE_FUN] = TAG_FUN,
	[TYPE_LST] = TAG_LST,
	[TYPE_EOS] = 0,
	[TYPE_INT] = TAG_INT,
	[TYPE_CHR] = TAG_CHR,
	[TYPE_STR] = TAG_STR,
	[TYPE_NIL] = 0
};

static void sm_debug (SmCodegen* gen, const char* fmt, int var, const char* cast) {
	if (!gen->opts.debug) {
		return;
	}
	
	GET_CODE;
	
	int len = strlen(fmt)+1;
	PUSH_BLOCK(sm_compile_get_decls_block (gen));
	int consttmp = sm_code_get_temp (code);
	EMIT_ ("@.const%d = private constant [%d x i8] c\"%s\\00\", align 8", consttmp, len, fmt);
	POP_BLOCK;

	if (cast) {
		var = TOINT("%s %%%d", "i64", cast, var);
	}
	int strptr = BITCAST("[%d x i8]* @.const%d", "i8*", len, consttmp);
	CALL ("i32 (i8*, ...)* @printf(i8* %%%d, i64 %%%d)", strptr, var);
}

int try_var (SmCodegen* gen, SmVar var, SmVarType type) {
	GET_CODE;
	COMMENT("try %%%d, expect %d", var.id, type);
	
	int object = var.id;
	sm_debug(gen, "try var %p\n", object, "%tagged");
	if (var.type != TYPE_UNK) {
		if (var.type != type) {
			printf ("compile-time expected %d, got %d\n", type, var.type);
			exit(0);
		} else {
			return object;
		}
	} else {
		int tag = EMIT("and %%tagged %%%d, %llu", object, TAG_MASK);
		int faillabel = sm_code_get_label (code);
		int ok = sm_code_get_label (code);
		SWITCH("%%tagged %%%d", "label %%fail%d", "i64 %llu, label %%ok%d", tag, faillabel, tagmap[type], ok);

		static int consttmp = -1;
		static const char* str = "runtime expected %llu, got %llu\n";
		int len = strlen(str)+1;
		if (consttmp < 0) {
			PUSH_BLOCK(sm_compile_get_decls_block (gen));
			consttmp = sm_code_get_temp (code);
			EMIT_ ("@.const%d = private constant [%d x i8] c\"%s\\00\", align 8", consttmp, len, str);
			POP_BLOCK;
		}
		
		LABEL("fail%d", faillabel);
		int strptr = BITCAST("[%d x i8]* @.const%d", "i8*", len, consttmp);
		CALL ("i32 (i8*, ...)* @printf(i8* %%%d, i64 %llu, i64 %%%d)", strptr, tagmap[type], tag);
		RET("void");

		LABEL("ok%d", ok);
		object = EMIT("and %%tagged %%%d, %llu", object, OBJ_MASK);
		object = EMIT("shl nuw %%tagged %%%d, 3", object);
		if (type == TYPE_STR) {
			object = TOPTR("%%tagged %%%d", "i8*", object);
		} else if (type == TYPE_FUN) {
			object = TOPTR("%%tagged %%%d", "%%closure*", object);
		} else {
			assert(FALSE);
		}
		return object;
	}
}

DEFUNC(compile_member_expr, SmMemberExpr) {
	GET_CODE;
	if (expr->inner) {
		printf("unsupported inner member\n");
		exit(0);
	}

	printf("member %p\n", sm_compile_get_scope(gen));
	int varid = sm_scope_lookup (sm_compile_get_scope (gen), expr->name);
	printf("member %p\n", sm_compile_get_scope(gen));
	if (varid < 0) {
		printf("not in scope %s\n", expr->name);
		exit(0);
	}

	int parent_size = sm_scope_get_size (sm_scope_get_parent (sm_compile_get_scope (gen)));

	{
		int sp = LOADSP;
		sm_debug(gen, g_strdup_printf("-> member %s, sp=%%p\n", expr->name), sp, "i64*");
	}
	int obj;
	if (sm_compile_get_use_temps (gen)) {
		if (varid < parent_size) {
			COMMENT("member %s(%d) from closure", expr->name, varid);
			// 0 = closure param
			int objptr = GETPTR("%%closure* %%0, i32 0, i32 %d, i32 %d", CLOSURE_SCOPE, varid);
			obj = LOAD("%%closure** %%%d", objptr);
			sm_debug(gen, "use temps, closure member %p\n", obj, "%closure*");
		} else {
			// from the stack
			COMMENT("member %s(%d) from stack", expr->name, varid);
			int sp = LOADSP;
			obj = SPGET(sp, varid-parent_size, "%closure*");
			sm_debug(gen, "stack member %p\n", obj, "%closure*");
		}
	} else {
		// 0 = closure param
		int objptr = GETPTR("%%closure* %%0, i32 0, i32 %d, i32 %d", CLOSURE_SCOPE, varid);
		obj = LOAD("%%closure** %%%d", objptr);
		sm_debug(gen, "no temps, closure member %p\n", obj, "%closure*");
	}
	RETVAL(id=obj, isthunk=TRUE, type=TYPE_UNK);
}

void sm_compile_set_scope (SmCodegen* gen, SmScope* scope) {
	printf("set %p\n", scope);
	gen->scope = scope;
}

DEFUNC(compile_seq_expr, SmSeqExpr) {
	GET_CODE;

	SmFuncExpr* func = (expr->base.parent && expr->base.parent->type == SM_FUNC_EXPR) ? (SmFuncExpr*) expr->base.parent : NULL;

	SmScope* scope = sm_scope_new (sm_compile_get_scope (gen));
	sm_compile_set_scope (gen, scope);

	int nparams = func ? func->params->len : 0;
	
	int closureid = sm_compile_begin_closure_func (gen);
	sm_compile_set_use_temps (gen, TRUE);
	COMMENT("seq/func closure");
	int sp = LOADSP;
	sm_debug(gen, "-> seq, sp=%p\n", sp, "i64*");
	
	int varid = 0;
	/* assign ids to locals */
	for (int i=0; i < expr->assigns->len; i++) {
		SmAssignExpr* assign = (SmAssignExpr*) expr->assigns->pdata[i];
		GPtrArray* names = assign->names;
		for (int i=0; i < names->len; i++) {
			const char* name = (const char*) names->pdata[i];

			int existing = sm_scope_lookup (scope, name);
			if (existing >= 0) {
				printf("shadowing %s\n", name);
				exit(0);
			}

			sm_scope_set (scope, name, varid++);
		}
	}

	/* assign ids to arguments */
	for (int i=0; i < nparams; i++) {
		const char* name = (const char*) func->params->pdata[i];
		
		int existing = sm_scope_lookup (scope, name);
		if (existing >= 0) {
			printf("shadowing %s\n", name);
			exit(0);
		}

		sm_scope_set (scope, name, varid++);
	}
	
	// make room for locals
	sp = VARSP(sp, -expr->assigns->len);

	/* preallocate closures */
	/* as a big lazy hack, keep track of the number of temporaries necessary to allocate a closure */
	int start_alloc = -1;
	int cur_alloc = 0;
	int temp_diff = 0; // keep track of the number of temporaries necessary to allocate a closure
	for (int i=0; i < expr->assigns->len; i++) {
		SmAssignExpr* assign = (SmAssignExpr*) expr->assigns->pdata[i];
		GPtrArray* names = assign->names;
		if (names->len == 1) {
			const char* name = (const char*) names->pdata[0];
			COMMENT("allocate for %s(%d)", name, i);
			int alloc = sm_compile_allocate_closure (gen);
			temp_diff = alloc-cur_alloc;
			cur_alloc = alloc;
			if (start_alloc < 0) {
				start_alloc = alloc;
			}
			SPSET(sp, i, alloc, "%closure*");
		} else {
			printf("unsupported pattern match\n");
			exit(0);
		}
	}

	/* visit assignments */
	for (int i=0; i < expr->assigns->len; i++) {
		SmAssignExpr* assign = (SmAssignExpr*) expr->assigns->pdata[i];
		GPtrArray* names = assign->names;
		if (names->len == 1) {
			const char* name = (const char*) names->pdata[0];
			COMMENT("assign for %s(%d)", name, i);
			sm_debug(gen, "assign %p\n", start_alloc, "%closure*");
			call_compile_table (gen, EXPR(assign->value), start_alloc);
			start_alloc += temp_diff;
		} else {
			printf("unsupported pattern match\n");
			exit(0);
		}
	}

	COMMENT("visit seq result");
	SmVar result = VISIT(expr->result);
	COMMENT("pop parameters and locals");
	VARSP(sp, nparams+expr->assigns->len);
	COMMENT("enter result");
	sm_debug(gen, "enter %p\n", result.id, "%closure*");
	ENTER(result.id);
	sm_compile_end_closure_func (gen);

	sm_compile_set_scope (gen, sm_scope_get_parent (scope));
	sm_scope_free (scope);

	COMMENT("create seq closure");
	COMMENT("ast: %s", g_strescape (sm_ast_dump(EXPR(expr)), NULL));
	int closure = sm_compile_create_closure (gen, closureid, prealloc);

	if (!func) {
		RETVAL(id=closure, isthunk=TRUE, type=result.type);
	} else {
		// closure object
		COMMENT("tag closure");
		closure = TOINT("%%closure* %%%d", "%%tagged", closure);
		closure = EMIT("lshr exact %%tagged %%%d, 3", closure);
		closure = EMIT("or %%tagged %%%d, %llu", closure, TAG_FUN);
		RETVAL(id=closure, isthunk=FALSE, type=TYPE_FUN);
	}
}

DEFUNC(compile_func_expr, SmFuncExpr) {
	GET_CODE;
	int closureid = sm_compile_begin_closure_func (gen);
	COMMENT("func thunk");
	COMMENT("get cont");
	int sp = LOADSP;
	int cont = SPGET(sp, 0, "%closure*");
	sm_debug(gen, "-> func, sp=%p\n", sp, "i64*");
	
	COMMENT("visit body");
	SmVar result = VISIT(expr->body);
	COMMENT("push func object");
	SPSET(sp, 0, result.id, NULL);
	
	COMMENT("enter cont");
	sm_debug(gen, "enter %p\n", cont, "%closure*");
	ENTER(cont);
	sm_compile_end_closure_func (gen);
	
	int closure = sm_compile_create_closure (gen, closureid, prealloc);
	RETVAL(id=closure, isthunk=TRUE, type=TYPE_FUN);
}

DEFUNC(compile_literal, SmLiteral) {
	GET_CODE;
	if (expr->str) {
		// define constant string
		// FIXME: do not create a thunk
		
		PUSH_BLOCK(sm_compile_get_decls_block (gen));
		int consttmp = sm_code_get_temp (code);
		int len = strlen(expr->str)+1;
		// FIXME:
		EMIT_ ("@.const%d = private constant [%d x i8] c\"%s\\00\", align 8", consttmp, len, expr->str);
		POP_BLOCK;

		// expression code
		int closureid = sm_compile_begin_closure_func (gen);
		COMMENT("string thunk code for '%s' string", expr->str);
		int sp = LOADSP;
		sm_debug(gen, "-> literal, sp=%p\n", sp, "i64*");
		
		int cont = SPGET(sp, 0, "%closure*");
		int obj = GETPTR("[%d x i8]* @.const%d, i32 0, i32 0", len, consttmp);
		obj = EMIT ("ptrtoint i8* %%%d to %%tagged", obj);
		obj = EMIT ("lshr exact %%tagged %%%d, 3", obj);
		obj = EMIT ("or %%tagged %%%d, %llu", obj, TAG_STR);
		SPSET(sp, 0, obj, NULL);

		sm_debug(gen, "enter %p\n", cont, "%closure*");
		ENTER(cont);
		sm_compile_end_closure_func (gen);

		// build thunk
		COMMENT("create string thunk");
		int closure = sm_compile_create_closure (gen, closureid, prealloc);
		RETVAL(id=closure, isthunk=TRUE, type=TYPE_STR);
	} else {
		// TODO: 
		printf("only string literals supported\n");
		exit(0);
	}
}

static int create_real_call_closure (SmCodegen* gen, SmCallExpr* expr) {
	GET_CODE;

	int closureid = sm_compile_begin_closure_func (gen);
	COMMENT("real call func");

	int sp = LOADSP;
	COMMENT("get func");
	int func = SPGET(sp, 0, "%tagged");
	sm_debug(gen, "-> real call, sp=%p\n", sp, "i64*");

	SmVar funcvar = { .id=func, .isthunk=FALSE, .type=TYPE_UNK };
	func = try_var (gen, funcvar, TYPE_FUN);
	
	COMMENT("set arguments");
	for (int i=0; i < expr->args->len; i++) {
		COMMENT("visit arg %d", i);
		SmVar arg = VISIT(EXPR(expr->args->pdata[i]));
		SPSET(sp, i-expr->args->len+1, arg.id, "%closure*");
	}
	
	COMMENT("push args onto the stack");
	VARSP(sp, -expr->args->len+1);
	COMMENT("enter real func");
	sm_debug(gen, "enter %p\n", func, "%closure*");
	ENTER(func);

	sm_compile_end_closure_func (gen);

	COMMENT("create real call closure");
	int closure = sm_compile_create_closure (gen, closureid, -1);
	return closure;
}

DEFUNC(compile_call_expr, SmCallExpr) {
	GET_CODE;

	int closureid = sm_compile_begin_closure_func (gen);
	COMMENT("call thunk func");
	int sp = LOADSP;
	sm_debug(gen, "-> call, sp=%p\n", sp, "i64*");
	
	COMMENT("visit func");
	SmVar func = VISIT(expr->func);

	int realfunc = create_real_call_closure (gen, expr);
	FINSP(sp, -1, realfunc, "%closure*");
	
	COMMENT("force func");
	sm_debug(gen, "enter %p\n", func.id, "%closure*");
	ENTER(func.id);
	
	sm_compile_end_closure_func (gen);
	
	// build thunk
	COMMENT("create call thunk");
	int closure = sm_compile_create_closure (gen, closureid, prealloc);
	RETVAL(id=closure, isthunk=TRUE, type=TYPE_UNK);
}

#define CAST(x) (SmVar (*)(SmCodegen*, SmExpr*, int prealloc))(x)
SmVar (*compile_table[])(SmCodegen*, SmExpr*, int prealloc) = {
	[SM_MEMBER_EXPR] = CAST(compile_member_expr),
	[SM_SEQ_EXPR] = CAST(compile_seq_expr),
	[SM_LITERAL] = CAST(compile_literal),
	[SM_FUNC_EXPR] = CAST(compile_func_expr),
	[SM_CALL_EXPR] = CAST(compile_call_expr)
};

static SmVar call_compile_table (SmCodegen* gen, SmExpr* expr, int prealloc) {
	return compile_table[expr->type](gen, expr, prealloc);
}

static int create_nop_closure (SmCodegen* gen) {
	GET_CODE;
	int nopid = sm_compile_begin_closure_func (gen);
	
	COMMENT("nop func"); // discards one object from the stack
	int sp = LOADSP;
	sm_debug(gen, "nop, sp=%d\n", sp, "i64*");
	VARSP(sp, 1);
	// end of the program
	RET("void");

	sm_compile_end_closure_func (gen);
	COMMENT("nop closure");
	
	int nopclo = sm_compile_create_closure (gen, nopid, -1);
	return nopclo;
}

static int create_prim_print (SmCodegen* gen) {
	GET_CODE;
	int directid = sm_compile_begin_closure_func (gen);
	COMMENT("real print func");
	int sp = LOADSP;
	COMMENT("get string");
	int str = SPGET(sp, 0, NULL);
	sm_debug(gen, "-> real print, string object=%p\n", str, "i64");
	sm_debug(gen, "sp=%p\n", sp, "i64*");

	COMMENT("get continuation");
	int cont = SPGET(sp, 1, "%closure*");
	sm_debug(gen, "cont=%p\n", cont, "%closure*");

	SmVar var = { .id=str, .isthunk=FALSE, .type=TYPE_UNK };
	str = try_var (gen, var, TYPE_STR);
	CALL ("i32 (i8*, ...)* @printf(i8* %%%d)", str);

	COMMENT("put string back in the stack");
	FINSP(sp, 1, str, "i8*");
	sm_debug(gen, "enter %p", cont, "%closure*");
	ENTER(cont);
	sm_compile_end_closure_func (gen);

	int direct = sm_compile_create_closure (gen, directid, -1);
	return direct;
}

static int create_print_closure (SmCodegen* gen) {
	GET_CODE;
	
	int printid = sm_compile_begin_closure_func (gen);
	COMMENT("print closure func");
	COMMENT("create direct closure");

	int sp = LOADSP;
	COMMENT("get string thunk");
	int str = SPGET(sp, 0, "%closure*");
	sm_debug(gen, "-> print closure, sp=%p\n", sp, "i64*");

	COMMENT("push direct print closure");
	int direct = create_prim_print (gen);
	FINSP(sp, 0, direct, "%closure*");

	COMMENT("enter string");
	sm_debug(gen, "enter string %p\n", str, "%closure*");
	ENTER(str);
	sm_compile_end_closure_func (gen);

	COMMENT("create print closure");
	int printclo = sm_compile_create_closure (gen, printid, -1);
	return printclo;
}

SmJit* sm_compile (SmCodegenOpts opts, const char* name, SmExpr* expr) {
	static int initialized = 0;
	if (!initialized) {
		initialized = 1;
		sm_jit_init ();
	}

	SmCodegen* gen = g_new0 (SmCodegen, 1);
	gen->opts = opts;
	SmCode* code = sm_code_new ();
	gen->code = code;
	gen->scope = sm_scope_new (NULL);
	gen->closure_stack = g_queue_new ();

	gen->decls = sm_code_new_block (code);
	
	PUSH_BLOCK(gen->decls);
	DECLARE ("i32 @printf(i8*, ...)");
	DECLARE ("i8* @aligned_alloc(i32, i32)");
	DECLARE ("void @llvm.memcpy.p0i8.p0i8.i32(i8*, i8*, i32, i32, i1)");
	DECLARE ("void @llvm.donothing() nounwind readnone");
	DECLARE ("void @llvm.debugtrap() nounwind");
	EMIT_ ("%%tagged = type i64");
	EMIT_ ("%%closurefunc = type void (%%closure*)*");
	EMIT_ ("@stack = global i64* null, align 8");
	EMIT_ ("@sp = global i64* null, align 8");
	DEFINE_STRUCT ("closure", "%%closurefunc, %%tagged, [0 x %%closure*]"); // func, cached object, scope
	POP_BLOCK;

	PUSH_NEW_BLOCK;
	BEGIN_FUNC_ATTRS("%%tagged", "thunk_cache", "%%closure*", "readonly");
	int thunk = sm_code_get_temp (code); // first param
	LABEL("entry");
	int objptr = GETPTR("%%closure* %%%d, i32 0, i32 %d", thunk, CLOSURE_CACHE);
	int obj = LOAD("%%tagged* %%%d", objptr);
	RET("%%tagged %%%d", obj);
	END_FUNC;
	POP_BLOCK;
	PUSH_NEW_BLOCK;
	BEGIN_FUNC("void", "main", "");
	COMMENT("main");
	LABEL("entry");

	COMMENT("alloc stack");
	int stack = CALL("i8* @aligned_alloc(i32 8, i32 %d)", (int)(4096*sizeof(void*)));
	stack = BITCAST("i8* %%%d", "i64*", stack);
	STORE("i64* %%%d", "i64** @stack", stack);
	
	STORE("i64* %%%d", "i64** @sp", stack);
	int sp = LOADSP;
	sp = VARSP(sp, 4096-8);
	sm_debug(gen, "bottom sp=%p\n", sp, "i64*");

	int nopclo = create_nop_closure (gen);
	int printclo = create_print_closure (gen);
	
	SPSET(sp, 0, nopclo, "%closure*");

	COMMENT("visit root expression");
	SmVar var = VISIT(expr);
	COMMENT("push root expression");
	sp = FINSP(sp, -1, var.id, "%closure*");
	sm_debug(gen, "root expr %p\n", var.id, "%closure*");
	sm_debug(gen, "sp=%p\n", sp, "i64*");

	COMMENT("enter print");
	ENTER(printclo);
	/* RET("void"); */
	END_FUNC;
	POP_BLOCK;

	char* unit = sm_code_link (code);
	puts(unit);
	sm_code_unref (code);
	
	SmJit* mod = sm_jit_compile ("<stdin>", unit);
	/* free (unit); */

	return mod;
}

void sm_run (SmJit* mod) {
	void (*entrypoint)() = (void (*)()) sm_jit_get_function (mod, FUNC("main"));
	if (!entrypoint) {
		return;
	}
	entrypoint();
}

