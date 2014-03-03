#include <assert.h>
#include <stdio.h>
#include <glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "compile.h"
#include "ast.h"
#include "llvm.h"
#include "code.h"
#include "astdumper.h"

#define DEFUNC(n,x) static SmVar n (SmCompile* comp, x* expr)
#define GET_CODE SmCode* code = comp->code
#define PUSH_BLOCK(x) sm_code_push_block(code, x)
#define POP_BLOCK sm_code_pop_block(code)
#define RETVAL(x,y,z) SmVar _res_var={.x, .y, .z}; return _res_var
#define VISIT(x) call_compile_table (comp, EXPR(x))
#define PUSH_NEW_BLOCK PUSH_BLOCK(sm_code_new_block (code))
#define LOADSP load_sp(comp)
#define SPGET(sp,x,c) sp_get(comp, sp, x, c)
#define SPSET(sp,x,v,c) sp_set(comp, sp, x, v, c)
#define SPFIN(sp,x,v,c) sp_fin(comp, sp, x, v, c)
#define VARSP(sp,x) var_sp(comp, sp, x)
#define ENTER(x) enter(comp, x)
#define NOTEMPS int old_use_temps = comp->use_temps; comp->use_temps = FALSE
#define BACKTEMPS comp->use_temps = old_use_temps
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

typedef struct _SmScope SmScope;

struct _SmScope {
	int nparams;
	GHashTable* map;
	SmScope* parent;
};

typedef struct {
	SmVar var;
	SmVarType type;
	int object;
	int faillabel;
} SmExc;

typedef struct {
	SmCompileOpts opts;
	SmCode* code;
	SmCodeBlock* decls;
	SmVar ret;
	SmScope* scope;
	GQueue* exc_stack;
	int cur_scopeid;
	int next_closureid;
	int use_temps;
} SmCompile;

static SmVar call_compile_table (SmCompile* comp, SmExpr* expr);

static int load_sp (SmCompile* comp) {
	GET_CODE;
	return LOAD("i64** @sp");
}

static int sp_get (SmCompile* comp, int sp, int x, const char* cast) {
	GET_CODE;
	COMMENT("sp[%d]", x);
	sp = GETPTR("i64* %%%d, i32 %d", sp, x);
	int val = LOAD("i64* %%%d", sp);
	if (cast) {
		val = TOPTR("i64 %%%d", "%s", val, cast);
	}
	return val;
}

static void sp_set (SmCompile* comp, int sp, int x, int v, const char* cast) {
	GET_CODE;
	COMMENT("sp[%d] = %%%d", x, v);
	if (cast) {
		v = TOINT("%s %%%d", "i64", cast, v);
	}
	sp = GETPTR("i64* %%%d, i32 %d", sp, x)
	STORE("i64 %%%d", "i64* %%%d", v, sp);
}

// set and update the sp
static int sp_fin (SmCompile* comp, int sp, int x, int v, const char* cast) {
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

static int var_sp (SmCompile* comp, int sp, int x) {
	if (!x) {
		return sp;
	}
	
	GET_CODE;
	COMMENT("sp += %d", x);
	sp = GETPTR("i64* %%%d, i32 %d", sp, x);
	STORE("i64* %%%d", "i64** @sp", sp);
	return sp;
}

static void enter (SmCompile* comp, int closure) {
	GET_CODE;
	int funcptr = GETPTR("%%closure* %%%d, i32 0, i32 %d", closure, CLOSURE_FUNC);
	int func = LOAD("%%closurefunc* %%%d", funcptr);
	TAILCALL_ ("void %%%d(%%closure* %%%d)", func, closure);
	RET("void");
}

static int scope_size (SmScope* scope) {
	if (!scope) {
		return 0;
	}
	int size = g_hash_table_size (scope->map);
	return scope_size (scope->parent)+size;
}


static int scope_lookup (SmScope* scope, const char* name) {
	while (scope) {
		int64_t id;
		if (g_hash_table_lookup_extended (scope->map, (gpointer)name, NULL, (gpointer*)&id)) {
			return scope_size (scope->parent)+id;
		}
		scope = scope->parent;
	}
	return -1;
}

static int begin_closure_func (SmCompile* comp, int closureid) {
	GET_CODE;

	PUSH_NEW_BLOCK;
	BEGIN_FUNC("fastcc void", "closure_%d_eval", "%%closure*", closureid);
	
	int closure = sm_code_get_temp (code); // first param
	LABEL("entry");
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
	return closure;
}

static void end_closure_func (SmCompile* comp) {
	GET_CODE;
	END_FUNC;
	POP_BLOCK;
}

static int create_closure (SmCompile* comp, int closureid) {
	GET_CODE;
	int parent_size = comp->scope ? scope_size (comp->scope->parent) : 0;
	int local_size = comp->scope ? g_hash_table_size (comp->scope->map) : 0;
	COMMENT("alloc closure with %d vars", parent_size+local_size);
	int alloc = CALL("i8* @aligned_alloc(i32 8, i32 %lu)",
					 sizeof(void*)*CLOSURE_SCOPE+sizeof(void*)*(parent_size+local_size));
	int closure = BITCAST("i8* %%%d", "%%closure*", alloc);

	COMMENT("store closure function");
	int funcptr = GETPTR("%%closure* %%%d, i32 0, i32 %d", closure, CLOSURE_FUNC);
	STORE("%%closurefunc " FUNC("closure_%d_eval"), "%%closurefunc* %%%d", closureid, funcptr);

	if (comp->scope) {
		// 0 = this closure
		if (comp->use_temps) {
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

void sm_debug (SmCompile* comp, const char* fmt, int var, const char* cast) {
	if (!comp->opts.debug) {
		return;
	}
	
	GET_CODE;
	
	int len = strlen(fmt)+1;
	PUSH_BLOCK(comp->decls);
	int consttmp = sm_code_get_temp (code);
	EMIT_ ("@.const%d = private constant [%d x i8] c\"%s\\00\", align 8", consttmp, len, fmt);
	POP_BLOCK;

	if (cast) {
		var = TOINT("%s %%%d", "i64", cast, var);
	}
	int strptr = BITCAST("[%d x i8]* @.const%d", "i8*", len, consttmp);
	CALL ("i32 (i8*, ...)* @printf(i8* %%%d, i64 %%%d)", strptr, var);
}

int try_var (SmCompile* comp, SmVar var, SmVarType type) {
	GET_CODE;
	COMMENT("try %%%d, expect %d", var.id, type);
	
	int object = var.id;
	sm_debug(comp, "try var %p\n", object, "%tagged");
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
			PUSH_BLOCK(comp->decls);
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

	int varid = scope_lookup (comp->scope, expr->name);
	if (varid < 0) {
		printf("not in scope %s\n", expr->name);
		exit(0);
	}

	int parent_size = comp->scope ? scope_size (comp->scope->parent) : 0;

	{
		int sp = LOADSP;
		sm_debug(comp, g_strdup_printf("-> member %s, sp=%%p\n", expr->name), sp, "i64*");
	}
	int obj;
	if (comp->use_temps) {
		if (varid < parent_size) {
			COMMENT("member %s(%d) from closure", expr->name, varid);
			// 0 = closure param
			int objptr = GETPTR("%%closure* %%0, i32 0, i32 %d, i32 %d", CLOSURE_SCOPE, varid);
			obj = LOAD("%%closure** %%%d", objptr);
			sm_debug(comp, "use temps, closure member %p\n", obj, "%closure*");
		} else {
			// from the stack
			COMMENT("member %s(%d) from stack", expr->name, varid);
			int sp = LOADSP;
			obj = SPGET(sp, varid-parent_size, "%closure*");
			sm_debug(comp, "stack member %p\n", obj, "%closure*");
		}
	} else {
		// 0 = closure param
		int objptr = GETPTR("%%closure* %%0, i32 0, i32 %d, i32 %d", CLOSURE_SCOPE, varid);
		obj = LOAD("%%closure** %%%d", objptr);
		sm_debug(comp, "no temps, closure member %p\n", obj, "%closure*");
	}		
	RETVAL(id=obj, isthunk=TRUE, type=TYPE_UNK);
}

DEFUNC(compile_seq_expr, SmSeqExpr) {
	GET_CODE;

	SmFuncExpr* func = (expr->base.parent && expr->base.parent->type == SM_FUNC_EXPR) ? (SmFuncExpr*) expr->base.parent : NULL;
	
	SmScope* scope = g_new0 (SmScope, 1);
	if (comp->scope) {
		scope->parent = comp->scope;
	}
	scope->map = g_hash_table_new (g_str_hash, g_str_equal);
	comp->scope = scope;

	int nparams = func ? func->params->len : 0;
	scope->nparams = nparams;
	
	int closureid = comp->next_closureid++;
	begin_closure_func (comp, closureid);
	int old_use_temps = comp->use_temps;
	comp->use_temps = TRUE;
	COMMENT("seq/func closure");
	int sp = LOADSP;
	sm_debug(comp, "-> seq, sp=%p\n", sp, "i64*");
	
	int varid = 0;
	/* assign ids to locals */
	for (int i=0; i < expr->assigns->len; i++) {
		SmAssignExpr* assign = (SmAssignExpr*) expr->assigns->pdata[i];
		GPtrArray* names = assign->names;
		for (int i=0; i < names->len; i++) {
			const char* name = (const char*) names->pdata[i];

			int existing = scope_lookup (scope, name);
			if (existing >= 0) {
				printf("shadowing %s\n", name);
				exit(0);
			}

			g_hash_table_insert (scope->map, (gpointer)name, GINT_TO_POINTER(varid++));
		}
	}

	/* assign ids to arguments */
	for (int i=0; i < nparams; i++) {
		const char* name = (const char*) func->params->pdata[i];
		
		int existing = scope_lookup (scope, name);
		if (existing >= 0) {
			printf("shadowing %s\n", name);
			exit(0);
		}

		g_hash_table_insert (scope->map, (gpointer)name, GINT_TO_POINTER(varid++));
	}
	
	// make room for locals
	sp = VARSP(sp, -expr->assigns->len);
	
	/* visit assignments */
	for (int i=0; i < expr->assigns->len; i++) {
		SmAssignExpr* assign = (SmAssignExpr*) expr->assigns->pdata[i];
		GPtrArray* names = assign->names;
		if (names->len == 1) {
			const char* name = (const char*) names->pdata[0];
			COMMENT("assign for %s(%d)", name, i);
			SmVar value = VISIT(assign->value);
			SPSET(sp, i, value.id, "%closure*");
			sm_debug(comp, "assign %p\n", value.id, "%closure*");
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
	sm_debug(comp, "enter %p\n", result.id, "%closure*");
	ENTER(result.id);
	comp->use_temps = old_use_temps;
	end_closure_func (comp);

	comp->scope = scope->parent;
	g_hash_table_unref (scope->map);
	free (scope);

	COMMENT("create seq closure");
	COMMENT("ast: %s", g_strescape (sm_ast_dump(EXPR(expr)), NULL));
	int closure = create_closure (comp, closureid);

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
	int closureid = comp->next_closureid++;
	begin_closure_func (comp, closureid);
	NOTEMPS;
	COMMENT("func thunk");
	COMMENT("get cont");
	int sp = LOADSP;
	int cont = SPGET(sp, 0, "%closure*");
	sm_debug(comp, "-> func, sp=%p\n", sp, "i64*");
	
	COMMENT("visit body");
	SmVar result = VISIT(expr->body);
	COMMENT("push func object");
	SPSET(sp, 0, result.id, NULL);
	
	COMMENT("enter cont");
	sm_debug(comp, "enter %p\n", cont, "%closure*");
	ENTER(cont);
	BACKTEMPS;
	end_closure_func (comp);
	
	int closure = create_closure (comp, closureid);
	RETVAL(id=closure, isthunk=TRUE, type=TYPE_FUN);
}

DEFUNC(compile_literal, SmLiteral) {
	GET_CODE;
	if (expr->str) {
		// define constant string
		// FIXME: do not create a thunk
		
		PUSH_BLOCK(comp->decls);
		int consttmp = sm_code_get_temp (code);
		int len = strlen(expr->str)+1;
		// FIXME:
		EMIT_ ("@.const%d = private constant [%d x i8] c\"%s\\00\", align 8", consttmp, len, expr->str);
		POP_BLOCK;

		int thunkid = comp->next_closureid++;
		// expression code
		begin_closure_func (comp, thunkid);
		NOTEMPS;
		COMMENT("string thunk code for '%s' string", expr->str);
		int sp = LOADSP;
		sm_debug(comp, "-> literal, sp=%p\n", sp, "i64*");
		
		int cont = SPGET(sp, 0, "%closure*");
		int obj = GETPTR("[%d x i8]* @.const%d, i32 0, i32 0", len, consttmp);
		obj = EMIT ("ptrtoint i8* %%%d to %%tagged", obj);
		obj = EMIT ("lshr exact %%tagged %%%d, 3", obj);
		obj = EMIT ("or %%tagged %%%d, %llu", obj, TAG_STR);
		SPSET(sp, 0, obj, NULL);

		sm_debug(comp, "enter %p\n", cont, "%closure*");
		ENTER(cont);
		BACKTEMPS;
		end_closure_func (comp);

		// build thunk
		COMMENT("create string thunk");
		int thunk = create_closure (comp, thunkid);
		RETVAL(id=thunk, isthunk=TRUE, type=TYPE_STR);
	} else {
		// TODO: 
		printf("only string literals supported\n");
		exit(0);
	}
}

static int create_real_call_closure (SmCompile* comp, SmCallExpr* expr) {
	GET_CODE;

	int closureid = comp->next_closureid++;
	begin_closure_func (comp, closureid);
	COMMENT("real call func");
	NOTEMPS;

	int sp = LOADSP;
	COMMENT("get func");
	int func = SPGET(sp, 0, "%tagged");
	sm_debug(comp, "-> real call, sp=%p\n", sp, "i64*");

	SmVar funcvar = { .id=func, .isthunk=FALSE, .type=TYPE_UNK };
	func = try_var (comp, funcvar, TYPE_FUN);
	
	COMMENT("set arguments");
	for (int i=0; i < expr->args->len; i++) {
		COMMENT("visit arg %d", i);
		SmVar arg = VISIT(EXPR(expr->args->pdata[i]));
		SPSET(sp, i-expr->args->len+1, arg.id, "%closure*");
	}
	
	COMMENT("push args onto the stack");
	VARSP(sp, -expr->args->len+1);
	COMMENT("enter real func");
	sm_debug(comp, "enter %p\n", func, "%closure*");
	ENTER(func);

	BACKTEMPS;
	end_closure_func (comp);

	COMMENT("create real call closure");
	int closure = create_closure (comp, closureid);
	return closure;
}

DEFUNC(compile_call_expr, SmCallExpr) {
	GET_CODE;
	
	int thunkid = comp->next_closureid++;
	begin_closure_func (comp, thunkid);
	NOTEMPS;
	COMMENT("call thunk func");
	int sp = LOADSP;
	sm_debug(comp, "-> call, sp=%p\n", sp, "i64*");
	
	COMMENT("visit func");
	SmVar func = VISIT(expr->func);

	int realfunc = create_real_call_closure (comp, expr);
	SPFIN(sp, -1, realfunc, "%closure*");
	
	COMMENT("force func");
	sm_debug(comp, "enter %p\n", func.id, "%closure*");
	ENTER(func.id);
	
	BACKTEMPS;
	end_closure_func (comp);
	
	// build thunk
	COMMENT("create call thunk");
	int thunk = create_closure (comp, thunkid);
	RETVAL(id=thunk, isthunk=TRUE, type=TYPE_UNK);
}

#define CAST(x) (SmVar (*)(SmCompile*, SmExpr*))(x)
SmVar (*compile_table[])(SmCompile*, SmExpr*) = {
	[SM_MEMBER_EXPR] = CAST(compile_member_expr),
	[SM_SEQ_EXPR] = CAST(compile_seq_expr),
	[SM_LITERAL] = CAST(compile_literal),
	[SM_FUNC_EXPR] = CAST(compile_func_expr),
	[SM_CALL_EXPR] = CAST(compile_call_expr)
};

static SmVar call_compile_table (SmCompile* comp, SmExpr* expr) {
	return compile_table[expr->type](comp, expr);
}

static int create_nop_closure (SmCompile* comp) {
	GET_CODE;
	int nopid = comp->next_closureid++;
	begin_closure_func (comp, nopid);
	NOTEMPS;
	
	COMMENT("nop func"); // discards one object from the stack
	int sp = LOADSP;
	sm_debug(comp, "nop, sp=%d\n", sp, "i64*");
	VARSP(sp, 1);
	// end of the program
	RET("void");

	BACKTEMPS;
	end_closure_func (comp);
	COMMENT("nop closure");
	
	int nopclo = create_closure (comp, nopid);
	return nopclo;
}

static int create_prim_print (SmCompile* comp) {
	GET_CODE;
	int directid = comp->next_closureid++;
	begin_closure_func (comp, directid);
	COMMENT("real print func");
	NOTEMPS;
	int sp = LOADSP;
	COMMENT("get string");
	int str = SPGET(sp, 0, NULL);
	sm_debug(comp, "-> real print, string object=%p\n", str, "i64");
	sm_debug(comp, "sp=%p\n", sp, "i64*");

	COMMENT("get continuation");
	int cont = SPGET(sp, 1, "%closure*");
	sm_debug(comp, "cont=%p\n", cont, "%closure*");

	SmVar var = { .id=str, .isthunk=FALSE, .type=TYPE_UNK };
	str = try_var (comp, var, TYPE_STR);
	CALL ("i32 (i8*, ...)* @printf(i8* %%%d)", str);

	COMMENT("put string back in the stack");
	SPFIN(sp, 1, str, "i8*");
	sm_debug(comp, "enter %p", cont, "%closure*");
	ENTER(cont);
	BACKTEMPS;
	end_closure_func (comp);

	int direct = create_closure (comp, directid);
	return direct;
}

static int create_print_closure (SmCompile* comp) {
	GET_CODE;
	
	int printid = comp->next_closureid++;
	begin_closure_func (comp, printid);
	NOTEMPS;
	COMMENT("print closure func");
	COMMENT("create direct closure");

	int sp = LOADSP;
	COMMENT("get string thunk");
	int str = SPGET(sp, 0, "%closure*");
	sm_debug(comp, "-> print closure, sp=%p\n", sp, "i64*");

	COMMENT("push direct print closure");
	int direct = create_prim_print (comp);
	SPFIN(sp, 0, direct, "%closure*");

	COMMENT("enter string");
	sm_debug(comp, "enter string %p\n", str, "%closure*");
	ENTER(str);
	BACKTEMPS;
	end_closure_func (comp);

	COMMENT("create print closure");
	int printclo = create_closure (comp, printid);
	return printclo;
}

SmJit* sm_compile (SmCompileOpts opts, const char* name, SmExpr* expr) {
	static int initialized = 0;
	if (!initialized) {
		initialized = 1;
		sm_jit_init ();
	}

	SmCompile* comp = g_new0 (SmCompile, 1);
	comp->opts = opts;
	SmCode* code = sm_code_new ();
	comp->code = code;
	comp->exc_stack = g_queue_new ();

	comp->decls = sm_code_new_block (code);
	
	PUSH_BLOCK(comp->decls);
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
	sm_debug(comp, "bottom sp=%p\n", sp, "i64*");

	int nopclo = create_nop_closure (comp);
	int printclo = create_print_closure (comp);
	
	SPSET(sp, 0, nopclo, "%closure*");

	COMMENT("visit root expression");
	SmVar var = VISIT(expr);
	COMMENT("push root expression");
	sp = SPFIN(sp, -1, var.id, "%closure*");
	sm_debug(comp, "root expr %p\n", var.id, "%closure*");
	sm_debug(comp, "sp=%p\n", sp, "i64*");

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

