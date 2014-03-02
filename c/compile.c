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
#define VARSP(sp,x) var_sp(comp, sp, x)
#define ENTER(x) enter(comp, x)

#define CLOSURE_FUNC 0
#define CLOSURE_CACHE 1
#define CLOSURE_SCOPES 2

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
	int level;
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
	SmCode* code;
	SmCodeBlock* decls;
	SmVar ret;
	SmScope* scope;
	GQueue* exc_stack;
	int cur_scopeid;
	int next_closureid;
} SmCompile;

static SmVar call_compile_table (SmCompile* comp, SmExpr* expr);

static int load_sp (SmCompile* comp) {
	GET_CODE;
	return LOAD("i64** @sp");
}

static int sp_get (SmCompile* comp, int sp, int x, const char* cast) {
	GET_CODE;
	sp = GETPTR("i64* %%%d, i32 %d", sp, x);
	int val = LOAD("i64* %%%d", sp);
	if (cast) {
		val = TOPTR("i64 %%%d", "%s", val, cast);
	}
	return val;
}

static void sp_set (SmCompile* comp, int sp, int x, int v, const char* cast) {
	GET_CODE;
	if (cast) {
		v = TOINT("%s %%%d", "i64", cast, v);
	}
	sp = GETPTR("i64* %%%d, i32 %d", sp, x)
	STORE("i64 %%%d", "i64* %%%d", v, sp);
}

static int var_sp (SmCompile* comp, int sp, int x) {
	GET_CODE;
	int newsp = GETPTR("i64* %%%d, i32 %d", sp, x);
	STORE("i64* %%%d", "i64** @sp", newsp);
	return newsp;
}

static void enter (SmCompile* comp, int closure) {
	GET_CODE;
	int funcptr = GETPTR("%%closure* %%%d, i32 0, i32 %d", closure, CLOSURE_FUNC);
	int func = LOAD("%%closurefunc* %%%d", funcptr);
	CALL_ ("void %%%d(%%closure* %%%d)", func, closure);
	RET("void");
}

static int scope_lookup (SmScope* scope, const char* name, int* level) {
	while (scope) {
		int64_t id;
		if (g_hash_table_lookup_extended (scope->map, (gpointer)name, NULL, (gpointer*)&id)) {
			if (level) {
				*level = scope->level;
			}
			return id;
		}
		scope = scope->parent;
	}
	return -1;
}

static char* closure_params (int nparams) {
	char* params = g_strdup ("%closure*");
	for (int i=0; i < nparams; i++) {
		char* old = params;
		params = g_strconcat (params, ", %closure*", NULL);
		free (old);
	}
	return params;
}

static int begin_closure_func (SmCompile* comp, int closureid) {
	GET_CODE;

	PUSH_NEW_BLOCK;
	BEGIN_FUNC("void", "closure_%d_eval", "%%closure*", closureid);
	
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

static void begin_thunk_func (SmCompile* comp, int closureid) {
	begin_closure_func (comp, closureid);
}

static void closure_return (SmCompile* comp, int result, int docache) {
	GET_CODE;
	
	// cache object
	if (docache) {
		COMMENT("save cached object");
		int closure = 0; // first param
		int objptr = GETPTR("%%closure* %%%d, i32 0, i32 %d", closure, CLOSURE_CACHE);
		STORE("%%tagged %%%d", "%%tagged* %%%d", result, objptr);
	}
	
	RET("%%tagged %%%d", result);
}

static void thunk_return (SmCompile* comp, int result) {
	closure_return (comp, result, TRUE);
}

static void end_closure_func (SmCompile* comp) {
	GET_CODE;
	END_FUNC;
	POP_BLOCK;
}

static void end_thunk_func (SmCompile* comp) {
	end_closure_func (comp);
}

static int eval_thunk (SmCompile* comp, int thunk) {
	GET_CODE;
	COMMENT("eval_thunk: load func");
	int funcptr = GETPTR("%%closure* %%%d, i32 0, i32 %d", thunk, CLOSURE_FUNC);
	int func = LOAD("%%closurefunc* %%%d", funcptr);

	// eval thunk
	COMMENT("eval_thunk: call func");
	func = BITCAST("%%closurefunc %%%d", "%%thunkfunc", func);
	int object = CALL("%%tagged %%%d(%%closure* %%%d)", func, thunk);

	return object;
}

static int create_closure (SmCompile* comp, int closureid) {
	GET_CODE;
	int size = (comp->scope ? comp->scope->level+1 : 0);
	COMMENT("alloc closure of size %d", size);
	int alloc = CALL("i8* @aligned_alloc(i32 8, i32 %lu)", sizeof(void*)*CLOSURE_SCOPES+sizeof(void*)*(size));
	int closure = BITCAST("i8* %%%d", "%%closure*", alloc);

	COMMENT("store closure function");
	int funcptr = GETPTR("%%closure* %%%d, i32 0, i32 %d", closure, CLOSURE_FUNC);
	STORE("%%closurefunc " FUNC("closure_%d_eval"), "%%closurefunc* %%%d", closureid, funcptr);

	if (comp->scope) {
		// 0 = first param
		COMMENT("copy scopes");
		int srcptr = GETPTR("%%closure* %%0, i32 0, i32 %d, i32 0", CLOSURE_SCOPES);
		int destptr = GETPTR("%%closure* %%%d, i32 0, i32 %d, i32 0", closure, CLOSURE_SCOPES);
		int srccast = BITCAST("%%closure*** %%%d", "i8*", srcptr);
		int destcast = BITCAST("%%closure*** %%%d", "i8*", destptr);
		CALL_("void @llvm.memcpy.p0i8.p0i8.i32(i8* %%%d, i8* %%%d, i32 %lu, i32 1, i1 false)", destcast, srccast, sizeof(void*)*(comp->scope->level+1));
	}

	return closure;
}

static int create_thunk (SmCompile* comp, int thunkid) {
	return create_closure (comp, thunkid);
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
		
static int try_var (SmCompile* comp, SmVar var, SmVarType type) {
	GET_CODE;
	int object = var.id;
	if (var.isthunk) {
		object = eval_thunk (comp, object);
	}
	
	if (var.type != TYPE_UNK) {
		if (var.type != type) {
			printf ("compile-time expected %d, got %d\n", type, var.type);
			exit(0);
		} else {
			return object;
		}
	} else {
		COMMENT ("try type %d", type);

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
		if (!comp->scope) {
			// we're in main
			RET("void");
		} else {
		// FIXME: throw meaningful exception
			RET("i64 0");
		}

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

	int level;
	int varid = scope_lookup (comp->scope, expr->name, &level);
	if (varid < 0) {
		printf("not in scope %s\n", expr->name);
		exit(0);
	}

	// 0 = first param
	COMMENT("member %s(%d), level %d", expr->name, varid, level);
	int scopeptr = GETPTR("%%closure* %%0, i32 0, i32 %d, i32 %d", CLOSURE_SCOPES, level);
	int scope = LOAD("%%closure*** %%%d", scopeptr);
	int addr = GETPTR("%%closure** %%%d, i32 %d", scope, varid);
	int res = LOAD("%%closure** %%%d", addr);
	if (!strcmp(expr->name, "x")) {
		CALL_("void @llvm.debugtrap()");
	}
	RETVAL(id=res, isthunk=TRUE, type=TYPE_UNK);
}

DEFUNC(compile_seq_expr, SmSeqExpr) {
	GET_CODE;

	SmFuncExpr* func = (expr->base.parent && expr->base.parent->type == SM_FUNC_EXPR) ? (SmFuncExpr*) expr->base.parent : NULL;
	
	SmScope* scope = g_new0 (SmScope, 1);
	if (comp->scope) {
		scope->parent = comp->scope;
		scope->level = comp->scope->level+1;
	}
	scope->map = g_hash_table_new (g_str_hash, g_str_equal);
	comp->scope = scope;

	int nparams = func ? func->params->len : 0;
	
	/* compute scope size */
	int varid = 0;
	for (int i=0; i < nparams; i++) {
		const char* name = (const char*) func->params->pdata[i];
		
		int existing = scope_lookup (scope, name, NULL);
		if (existing >= 0) {
			printf("shadowing %s\n", name);
			exit(0);
		}
		
		g_hash_table_insert (scope->map, (gpointer)name, GINT_TO_POINTER(varid++));
	}
	
	for (int i=0; i < expr->assigns->len; i++) {
		SmAssignExpr* assign = (SmAssignExpr*) expr->assigns->pdata[i];
		GPtrArray* names = assign->names;
		for (int i=0; i < names->len; i++) {
			const char* name = (const char*) names->pdata[i];

			int existing = scope_lookup (scope, name, NULL);
			if (existing >= 0) {
				printf("shadowing %s\n", name);
				exit(0);
			}

			g_hash_table_insert (scope->map, (gpointer)name, GINT_TO_POINTER(varid++));
		}
	}

	int closureid = comp->next_closureid++;

	begin_closure_func (comp, closureid);
	COMMENT("seq/func closure");

	int allocsize = varid*sizeof(void*);
	COMMENT("alloc scope of size %d", varid);
	int alloc = CALL("i8* @aligned_alloc(i32 8, i32 %d)", allocsize);
	int scopeid = BITCAST("i8* %%%d", "%%closure**", alloc);
	COMMENT("set this scope to the thunk scopes");
	// 0 = first param
	int scopeptr = GETPTR("%%closure* %%0, i32 0, i32 %d, i32 %d", CLOSURE_SCOPES, scope->level);
	STORE("%%closure** %%%d", "%%closure*** %%%d", scopeid, scopeptr);

	/* assign parameters to scope */
	for (int i=0; i < nparams; i++) {
		COMMENT("assign param %s(%d)", (const char*)func->params->pdata[i], i);
		int addr = GETPTR("%%closure** %%%d, i32 %d", scopeid, i);
		STORE("%%closure* %%%d", "%%closure** %%%d", i+1, addr); // parameter i+1, because param 0 is the closure itself
	}
	
	/* assign values to scope */
	for (int i=0; i < expr->assigns->len; i++) {
		SmAssignExpr* assign = (SmAssignExpr*) expr->assigns->pdata[i];
		GPtrArray* names = assign->names;
		if (names->len == 1) {
			const char* name = (const char*) names->pdata[0];
			SmVar value = VISIT(assign->value);
			COMMENT("assign expression to %s(%d), level %d", name, i, scope->level);
			int addr = GETPTR("%%closure** %%%d, i32 %d", scopeid, i);
			STORE("%%closure* %%%d", "%%closure** %%%d", value.id, addr);
		} else {
			printf("unsupported pattern match\n");
			exit(0);
		}
	}

	SmVar result = VISIT(expr->result);
	int object = result.id;
	if (result.isthunk) {
		object = eval_thunk (comp, result.id);
	}

	closure_return (comp, object, !func);
	end_closure_func (comp);

	comp->scope = scope->parent;
	g_hash_table_unref (scope->map);
	free (scope);

	COMMENT("seq/func create closure with %d params", nparams);
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
	// seq
	// FIXME: drop this thunk and return the real closure
	int thunkid = comp->next_closureid++;
	begin_thunk_func (comp, thunkid);
	COMMENT("function creation thunk func");
	SmVar result = VISIT(expr->body);
	/* int obj = result.id; */
	/* if (result.isthunk) { */
		/* COMMENT("eval result"); */
		/* obj = eval_thunk (comp, obj); */
	/* } */
	thunk_return (comp, result.id);
	end_thunk_func (comp);

	// build thunk
	COMMENT("function creation thunk");
	int thunk = create_thunk (comp, thunkid);
	RETVAL(id=thunk, isthunk=TRUE, type=TYPE_FUN);
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
		begin_thunk_func (comp, thunkid);
		COMMENT("string thunk code for '%s' string", expr->str);
		int sp = LOADSP;
		int cont = SPGET(sp, 0, "%closure*");
		int obj = GETPTR("[%d x i8]* @.const%d, i32 0, i32 0", len, consttmp);
		obj = EMIT ("ptrtoint i8* %%%d to %%tagged", obj);
		obj = EMIT ("lshr exact %%tagged %%%d, 3", obj);
		obj = EMIT ("or %%tagged %%%d, %llu", obj, TAG_STR);
		SPSET(sp, 0, obj, NULL);
		ENTER(cont);
		end_thunk_func (comp);

		// build thunk
		COMMENT("create string thunk");
		int thunk = create_thunk (comp, thunkid);
		RETVAL(id=thunk, isthunk=TRUE, type=TYPE_STR);
	} else {
		// TODO: 
		printf("only string literals supported\n");
		exit(0);
	}
}

DEFUNC(compile_call_expr, SmCallExpr) {
	GET_CODE;
	
	int thunkid = comp->next_closureid++;
	begin_thunk_func (comp, thunkid);
	COMMENT("call thunk func");
	
	COMMENT("eval the function");
	SmVar closurevar = VISIT(expr->func);
	int closure = try_var (comp, closurevar, TYPE_FUN);

	int funcptr = GETPTR("%%closure* %%%d, i32 0, i32 %d", closure, CLOSURE_FUNC);
	int func = LOAD("%%closurefunc* %%%d", funcptr);

	char* args = g_strdup_printf ("%%closure* %%%d", closure);
	for (int i=0; i < expr->args->len; i++) {
		COMMENT("visit arg %d", i);
		SmVar arg = VISIT(EXPR(expr->args->pdata[i]));
		char* old = args;
		args = g_strdup_printf ("%s, %%closure* %%%d", args, arg.id);
		g_free (old);
	}

	COMMENT("call function");
	char* params = closure_params (expr->args->len);
	func = BITCAST("%%closurefunc %%%d", "%%tagged (%s)*", func, params);
	g_free (params);
	int result = CALL("%%tagged %%%d(%s)", func, args);
	g_free (args);

	thunk_return (comp, result);
	end_thunk_func (comp);
	
	// build thunk
	COMMENT("create call thunk");
	int thunk = create_thunk (comp, thunkid);
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
	COMMENT("nop func"); // discards one object from the stack
	int sp = LOADSP;
	VARSP(sp, 1);
	RET("void");
	end_closure_func (comp);
	COMMENT("nop closure");
	
	int nopclo = create_closure (comp, nopid);
	return nopclo;
}

static int create_print_closure (SmCompile* comp) {
	GET_CODE;
	int printid = comp->next_closureid++;
	begin_closure_func (comp, printid);
	COMMENT("print func");
	int sp = LOADSP;
	int str = SPGET(sp, 0, NULL);
	int cont = SPGET(sp, 1, "%closure*");
	
	SmVar var = { .id=str, .isthunk=FALSE, .type=TYPE_UNK };
	str = try_var (comp, var, TYPE_STR);
	CALL ("i32 (i8*, ...)* @printf(i8* %%%d)", str);
	
	SPSET(sp, 1, str, "i8*");
	VARSP(sp, 1);
	ENTER(cont);
	end_closure_func (comp);
	
	int printclo = create_closure (comp, printid);
	return printclo;
}

SmJit* sm_compile (const char* name, SmExpr* expr) {
	static int initialized = 0;
	if (!initialized) {
		initialized = 1;
		sm_jit_init ();
	}

	SmCompile* comp = g_new0 (SmCompile, 1);
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
	DEFINE_STRUCT ("closure", "%%closurefunc, %%tagged, [0 x %%closure**]"); // func, cached object, scope
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

	int nopclo = create_nop_closure (comp);
	int printclo = create_print_closure (comp);
	
	SPSET(sp, 0, nopclo, "%closure*");
	SPSET(sp, -1, printclo, "%closure*");
	VARSP(sp, -1);

	COMMENT("visit root expression");
	SmVar var = VISIT(expr);
	COMMENT("enter");
	ENTER(var.id);
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

