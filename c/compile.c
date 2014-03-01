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

#define DEFUNC(n,x) static SmVar n (SmCompile* comp, x* expr)
#define GET_CODE SmCode* code = comp->code
#define PUSH_BLOCK(x) sm_code_push_block(code, x)
#define POP_BLOCK sm_code_pop_block(code)
#define RETVAL(x,y,z) SmVar _res_var={.x, .y, .z}; return _res_var
#define VISIT(x) call_compile_table (comp, EXPR(x))
#define PUSH_NEW_BLOCK PUSH_BLOCK(sm_code_new_block (code))

#define THUNK_FUNC 0
#define THUNK_CACHE 1
#define THUNK_SCOPES 2

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
	TYPE_SMI,
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
	int faillabel;
} SmExc;

typedef struct {
	SmCode* code;
	SmCodeBlock* decls;
	SmVar ret;
	SmScope* scope;
	GQueue* exc_stack;
	int cur_scopeid;
	int next_thunkid;
} SmCompile;

static SmVar call_compile_table (SmCompile* comp, SmExpr* expr);

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
	char* params = NULL;
	for (int i=0; i < nparams; i++) {
		char* old = params;
		params = g_strconcat (", %thunk*", params, NULL);
		free (old);
	}
	return params;
}

static void begin_closure_func (SmCompile* comp, int thunkid, int nparams) {
	GET_CODE;

	PUSH_NEW_BLOCK;
	char* params = closure_params (nparams);
	BEGIN_FUNC("%%tagged", "thunk_%d_eval", "%%thunk*%s", thunkid, params ? params : "");
	g_free (params);
	
	int thunk = sm_code_get_temp (code); // first param
	LABEL("entry");
	if (!nparams) {
		// next call will point to the cache
		int funcptr = GETPTR("%%thunk* %%%d", "i32 0, i32 %d", thunk, THUNK_FUNC);
		int func = BITCAST("%%tagged (%%thunk*)* " FUNC("thunk_cache"), "%%tagged (%%thunk*, ...)*");
		STORE("%%thunkfunc %%%d", "%%thunkfunc* %%%d", func, funcptr);
	} else {
		// reserve for parameters
		for (int i=0; i < nparams; i++) {
			sm_code_get_temp (code);
		}
	}
}

static void begin_thunk_func (SmCompile* comp, int thunkid) {
	begin_closure_func (comp, thunkid, 0);
}

static void closure_return (SmCompile* comp, int result, int docache) {
	GET_CODE;
	
	// cache object
	if (docache) {
		int thunk = 0; // first param
		int objptr = GETPTR("%%thunk* %%%d", "i32 0, i32 %d", thunk, THUNK_CACHE);
		STORE("%%tagged %%%d", "%%tagged* %%%d", result, objptr);
	}
	
	RET("%%tagged %%%d", result);
}

static void thunk_return (SmCompile* comp, int result) {
	closure_return (comp, result, TRUE);
}

static void end_thunk_func (SmCompile* comp) {
	GET_CODE;
	END_FUNC;
	POP_BLOCK;
}

static int eval_thunk (SmCompile* comp, int thunk) {
	GET_CODE;
	int funcptr = GETPTR("%%thunk* %%%d", "i32 0, i32 %d", thunk, THUNK_FUNC);
	int func = LOAD("%%thunkfunc* %%%d", funcptr);

	// eval thunk
	func = BITCAST("%%tagged (%%thunk*, ...)* %%%d", "%%tagged (%%thunk*)*", func);
	int object = CALL("%%tagged %%%d(%%thunk* %%%d)", func, thunk);

	return object;
}

static int create_closure (SmCompile* comp, int thunkid, int notseq, int nparams) {
	GET_CODE;
	int alloc = CALL("i8* @aligned_alloc(i32 8, i32 %lu)", sizeof(void*)*THUNK_SCOPES+sizeof(void*)*(comp->scope->level+1));
	int thunk = BITCAST("i8* %%%d", "%%thunk*", alloc);

	int funcptr = GETPTR("%%thunk* %%%d", "i32 0, i32 %d", thunk, THUNK_FUNC);
	char* params = closure_params (nparams);
	int func = BITCAST("%%tagged (%%thunk*%s)* " FUNC("thunk_%d_eval"), "%%tagged (%%thunk*, ...)*",
					   params ? params : "", thunkid);
	g_free (params);
	STORE("%%thunkfunc %%%d", "%%thunkfunc* %%%d", func, funcptr);

	if (comp->scope->parent) {
		// 0 = first param
		int srcptr = GETPTR("%%thunk* %%0", "i32 0, i32 %d, i32 0", THUNK_SCOPES);
		int destptr = GETPTR("%%thunk* %%%d", "i32 0, i32 %d, i32 0", thunk, THUNK_SCOPES);
		int srccast = BITCAST("%%thunk*** %%%d", "i8*", srcptr);
		int destcast = BITCAST("%%thunk*** %%%d", "i8*", destptr);
		CALL_("void @llvm.memcpy.p0i8.p0i8.i32(i8* %%%d, i8* %%%d, i32 %lu, i32 1, i1 false)", destcast, srccast, sizeof(void*)*(comp->scope->level+notseq));
	}

	return thunk;
}

static int create_thunk (SmCompile* comp, int thunkid) {
	return create_closure (comp, thunkid, 1, 0);
}

static int begin_try_string (SmCompile* comp, SmVar var) {
	GET_CODE;
	SmExc* exc = g_new (SmExc, 1);
	exc->var = var;
	g_queue_push_tail (comp->exc_stack, exc);
	
	int object = var.id;
	if (var.isthunk) {
		object = eval_thunk (comp, object);
	}
	
	if (var.type != TYPE_UNK) {
		if (var.type != TYPE_STR) {
			printf ("expected string\n");
			exit(0);
		} else {
			return object;
		}
	} else {
		int tag = EMIT("and %%tagged %%%d, %llu", object, TAG_MASK);
		int isstr = sm_code_get_label (code);
		exc->faillabel = sm_code_get_label (code);
		SWITCH("%%tagged %%%d", "label %%fail%d", "i64 %llu, label %%isstr%d", tag, exc->faillabel, TAG_STR, isstr);
		LABEL("isstr%d", isstr);
		object = EMIT("and %%tagged %%%d, %llu", object, OBJ_MASK);
		object = EMIT("shl nuw %%tagged %%%d, 3", object);
		object = TOPTR("%%tagged %%%d", "i8*", object);
		return object;
	}
}

static void end_try_string (SmCompile* comp) {
	GET_CODE;
	SmExc* exc = g_queue_pop_tail (comp->exc_stack);

	if (exc->var.type != TYPE_UNK) {
		free (exc);
		return;
	}
	
	static int consttmp = -1;
	const char* str = "expected string\n";
	int len = strlen(str)+1;
	if (consttmp < 0) {
		PUSH_BLOCK(comp->decls);
		consttmp = sm_code_get_temp (code);
		EMIT_ ("@.const%d = private constant [%d x i8] c\"%s\\00\", align 8", consttmp, len, str);
		POP_BLOCK;
	}
	
	int endstr = sm_code_get_label (code);
	BR("label %%endstr%d", endstr);
	
	LABEL("fail%d", exc->faillabel);
	int strptr = BITCAST("[%d x i8]* @.const%d", "i8*", len, consttmp);
	CALL ("i32 (i8*, ...)* @printf(i8* %%%d)", strptr);
	if (!comp->scope) {
		// we're in main
		RET("void");
	} else {
		// FIXME:
		RET("i64 0");
	}

	LABEL("endstr%d", endstr);

	free(exc);
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
	int scopeptr = GETPTR("%%thunk* %%0", "i32 0, i32 %d, i32 %d", THUNK_SCOPES, level);
	int scope = LOAD("%%thunk*** %%%d", scopeptr);
	int addr = GETPTR("%%thunk** %%%d", "i32 %d", scope, varid);
	int res = LOAD("%%thunk** %%%d", addr);
	RETVAL(id=res, isthunk=TRUE, type=TYPE_UNK);
}

DEFUNC(compile_seq_expr, SmSeqExpr) {
	GET_CODE;

	SmFuncExpr* func = (expr->base.parent && expr->base.parent->type == SM_FUNC_EXPR) ? (SmFuncExpr*) expr->base.parent : NULL;
	
	SmScope* scope = (SmScope*) calloc(1, sizeof (SmScope));
	if (comp->scope) {
		scope->parent = comp->scope;
		scope->level = comp->scope->level+1;
	}
	scope->map = g_hash_table_new (g_str_hash, g_str_equal);
	comp->scope = scope;

	/* compute scope size */
	int varid = 0;
	if (func) {
		for (int i=0; i < func->params->len; i++) {
			const char* name = (const char*) func->params->pdata[i];

			int existing = scope_lookup (scope, name, NULL);
			if (existing >= 0) {
				printf("shadowing %s\n", name);
				exit(0);
			}

			g_hash_table_insert (scope->map, (gpointer)name, GINT_TO_POINTER(varid++));
		}
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

	int thunkid = comp->next_thunkid++;

	begin_closure_func (comp, thunkid, func ? func->params->len : 0);
	COMMENT("seq/func closure");

	int allocsize = varid*sizeof(void*);
	COMMENT("alloc scope");
	int alloc = CALL("i8* @aligned_alloc(i32 8, i32 %d)", allocsize);
	int scopeid = BITCAST("i8* %%%d", "%%thunk**", alloc);
	// set to the current thunk, 0 = first param
	COMMENT("set this scope to the thunk scopes");
	int scopeptr = GETPTR("%%thunk* %%0", "i32 0, i32 %d, i32 %d", THUNK_SCOPES, scope->level);
	STORE("%%thunk** %%%d", "%%thunk*** %%%d", scopeid, scopeptr);

	if (func) {
		/* assign parameters to scope */
		for (int i=0; i < func->params->len; i++) {
			COMMENT("assign param %d", i);
			int addr = GETPTR("%%thunk** %%%d", "i32 %d", scopeid, varid);
			STORE("%%thunk* %%%d", "%%thunk** %%%d", i+1, addr); // parameter i+1, because param 0 is the closure itself
		}
	}
	
	/* assign values to scope */
	for (int i=0; i < expr->assigns->len; i++) {
		SmAssignExpr* assign = (SmAssignExpr*) expr->assigns->pdata[i];
		GPtrArray* names = assign->names;
		if (names->len == 1) {
			const char* name = (const char*) names->pdata[0];
			varid = scope_lookup (scope, name, NULL);
			if (varid < 0) {
				printf ("assert not found '%s'\n", name);
				exit(0);
			}

			SmVar value = VISIT(assign->value);
			COMMENT("assign expression to %s, level %d", name, scope->level);
			int addr = GETPTR("%%thunk** %%%d", "i32 %d", scopeid, varid);
			STORE("%%thunk* %%%d", "%%thunk** %%%d", value.id, addr);
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
	end_thunk_func (comp);

	int thunk = create_closure (comp, thunkid, 0, func ? func->params->len : 0);

	comp->scope = scope->parent;
	g_hash_table_unref (scope->map);
	free (scope);

	if (!func) {
		RETVAL(id=thunk, isthunk=TRUE, type=result.type);
	} else {
		// closure object
		COMMENT("tag closure");
		int closure = TOINT("%%thunk* %%%d", "%%tagged", thunk);
		closure = EMIT("lshr exact %%tagged %%%d, 3", closure);
		closure = EMIT("or %%tagged %%%d, %llu", closure, TAG_FUN);
		RETVAL(id=closure, isthunk=FALSE, type=TYPE_FUN);
	}
}

DEFUNC(compile_func_expr, SmFuncExpr) {
	GET_CODE; 
	// seq
	// FIXME: drop this thunk and return the real closure
	int thunkid = comp->next_thunkid++;
	begin_thunk_func (comp, thunkid);
	COMMENT("function creation thunk func");
	SmVar result = VISIT(expr->body);
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

		int thunkid = comp->next_thunkid++;
		// expression code
		begin_thunk_func (comp, thunkid);
		int obj = GETPTR("[%d x i8]* @.const%d", "i32 0, i32 0", len, consttmp);
		obj = EMIT ("ptrtoint i8* %%%d to %%tagged", obj);
		obj = EMIT ("lshr exact %%tagged %%%d, 3", obj);
		obj = EMIT ("or %%tagged %%%d, %llu", obj, TAG_STR);
		thunk_return (comp, obj);
		end_thunk_func (comp);

		// build thunk
		int thunk = create_thunk (comp, thunkid);
		RETVAL(id=thunk, isthunk=TRUE, type=TYPE_STR);
	} else {
		// TODO: 
		printf("only string literals supported\n");
		exit(0);
	}
}

#define CAST(x) (SmVar (*)(SmCompile*, SmExpr*))(x)
SmVar (*compile_table[])(SmCompile*, SmExpr*) = {
	[SM_MEMBER_EXPR] = CAST(compile_member_expr),
	[SM_SEQ_EXPR] = CAST(compile_seq_expr),
	[SM_LITERAL] = CAST(compile_literal),
	[SM_FUNC_EXPR] = CAST(compile_func_expr)
};

static SmVar call_compile_table (SmCompile* comp, SmExpr* expr) {
	return compile_table[expr->type](comp, expr);
}

SmJit* sm_compile (const char* name, SmExpr* expr) {
	static int initialized = 0;
	if (!initialized) {
		initialized = 1;
		sm_jit_init ();
	}

	SmCompile* comp = (SmCompile*)calloc(1, sizeof(SmCompile));
	SmCode* code = sm_code_new ();
	comp->code = code;
	comp->exc_stack = g_queue_new ();

	comp->decls = sm_code_new_block (code);
	
	PUSH_BLOCK(comp->decls);
	DECLARE ("i32 @printf(i8*, ...)");
	DECLARE ("i8* @aligned_alloc(i32, i32)");
	DECLARE ("void @llvm.memcpy.p0i8.p0i8.i32(i8*, i8*, i32, i32, i1)");
	DECLARE ("void @llvm.donothing() nounwind readnone");
	EMIT_ ("%%tagged = type i64");
	EMIT_ ("%%thunkfunc = type %%tagged (%%thunk*, ...)*");
	DEFINE_STRUCT ("thunk", "%%thunkfunc, %%tagged, [0 x %%thunk**]"); // func, cached object, scope
	POP_BLOCK;

	PUSH_NEW_BLOCK;
	BEGIN_FUNC("%%tagged", "thunk_cache", "%%thunk*");
	int thunk = sm_code_get_temp (code); // first param
	LABEL("entry");
	int objptr = GETPTR("%%thunk* %%%d", "i32 0, i32 %d", thunk, THUNK_CACHE);
	int obj = LOAD("%%tagged* %%%d", objptr);
	RET("%%tagged %%%d", obj);
	END_FUNC;
	POP_BLOCK;

	PUSH_NEW_BLOCK;
	BEGIN_FUNC("void", "main", "");
	LABEL("entry");
	SmVar exprvar = VISIT(expr);

	// first call
	int strptr = begin_try_string (comp, exprvar);
	CALL ("i32 (i8*, ...)* @printf(i8* %%%d)", strptr);
	end_try_string (comp);

	// second call
	strptr = begin_try_string (comp, exprvar);
	CALL ("i32 (i8*, ...)* @printf(i8* %%%d)", strptr);
	end_try_string (comp);

	RET("void");
	END_FUNC;
	POP_BLOCK;

	char* unit = sm_code_link (code);
	puts(unit);
	sm_code_unref (code);
	
	SmJit* mod = sm_jit_compile ("<stdin>", unit);
	free (unit);

	return mod;
}

void sm_run (SmJit* mod) {
	void (*entrypoint)() = (void (*)()) sm_jit_get_function (mod, FUNC("main"));
	if (!entrypoint) {
		return;
	}
	entrypoint();
}

