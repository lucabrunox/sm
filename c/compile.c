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

#define DEFUNC(n,x) static SmVarType n (SmCompile* comp, x* expr)
#define GET_CODE SmCode* code = comp->code
#define PUSH_BLOCK(x) sm_code_push_block(code, x)
#define POP_BLOCK sm_code_pop_block(code)
#define RETVAL(x) SmVarType _res_var=x; return _res_var
#define VISIT(x) call_compile_table (comp, EXPR(x))
#define PUSH_NEW_BLOCK PUSH_BLOCK(sm_code_new_block (code))

#define THUNK_FUNC 0
#define THUNK_JUMP 1
#define THUNK_CACHE 2
#define THUNK_SCOPES 3

typedef struct {
	int id;
	int isconst;
} SmVarType;

typedef struct _SmScope SmScope;

struct _SmScope {
	int level;
	GHashTable* map;
	SmScope* parent;
};

typedef struct {
	SmCode* code;
	SmCodeBlock* decls;
	SmVarType ret;
	SmScope* scope;
	int cur_scopeid;
	int next_thunkid;
} SmCompile;

static SmVarType call_compile_table (SmCompile* comp, SmExpr* expr);

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

static void begin_thunk_func (SmCompile* comp, int thunkid) {
	GET_CODE;
	PUSH_BLOCK(comp->decls);
	EMIT_ ("@thunklabel_%d = internal global [2 x i8*] [i8* blockaddress(" FUNC("thunk_%d") ", %%eval), i8* blockaddress(" FUNC("thunk_%d") ", %%cache)]",
		   thunkid, thunkid, thunkid);
	POP_BLOCK;

	PUSH_NEW_BLOCK;
	BEGIN_FUNC("%%object", "thunk_%d", "%%thunk*", thunkid);
	int thunk = sm_code_get_temp (code); // first param
	LABEL("entry");
	int jmpptr = GETPTR("%%thunk* %%%d", "i32 0, i32 %d", thunk, THUNK_JUMP);
	int jmp = LOAD("i64* %%%d", jmpptr);
	int labelptr = GETPTR("[2 x i8*]* @thunklabel_%d", "i32 0, i64 %%%d", thunkid, jmp);
	int label = LOAD("i8** %%%d", labelptr);
	EMIT_("indirectbr i8* %%%d, [label %%eval, label %%cache]", label);

	LABEL("eval");
	// next jump will point to the cache
	STORE("i64 1", "i64* %%%d", jmpptr);
}

static void end_thunk_func (SmCompile* comp, int result) {
	GET_CODE;
	int thunk = 0; // first param
	
	LABEL("cache");
	int objptr = GETPTR("%%thunk* %%%d", "i32 0, i32 %d", thunk, THUNK_CACHE);
	int obj = LOAD("%%object* %%%d", objptr);
	RET("%%object %%%d", obj);
	
	END_FUNC;
	POP_BLOCK;
}

static int eval_thunk (SmCompile* comp, int thunk) {
	GET_CODE;
	int funcptr = GETPTR("%%thunk* %%%d", "i32 0, i32 %d", thunk, THUNK_FUNC);
	int func = LOAD("%%thunkfunc* %%%d", funcptr);

	// eval thunk
	int object = CALL("%%object %%%d(%%thunk* %%%d)", func, thunk);
	// cache evaluation
	int objptr = GETPTR("%%thunk* %%%d", "i32 0, i32 %d", thunk, THUNK_CACHE);
	STORE("%%object %%%d", "%%object* %%%d", object, objptr);

	return object;
}

static int create_thunk (SmCompile* comp, int thunkid, int notseq) {
	GET_CODE;
	int alloc = CALL("i8* @malloc(i32 %lu)", sizeof(void*)*THUNK_SCOPES+sizeof(void*)*(comp->scope->level+1));
	int thunk = BITCAST("i8* %%%d", "%%thunk*", alloc);

	int funcptr = GETPTR("%%thunk* %%%d", "i32 0, i32 %d", thunk, THUNK_FUNC);
	STORE("%%thunkfunc " FUNC("thunk_%d"), "%%thunkfunc* %%%d", thunkid, funcptr);

	int jmpptr = GETPTR("%%thunk* %%%d", "i32 0, i32 %d", thunk, THUNK_JUMP);
	STORE("i64 0", "i64* %%%d", jmpptr);

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
	RETVAL({ .id=res });
}

DEFUNC(compile_seq_expr, SmSeqExpr) {
	GET_CODE;
	
	SmScope* scope = (SmScope*) calloc(1, sizeof (SmScope));
	if (comp->scope) {
		scope->parent = comp->scope;
		scope->level = comp->scope->level+1;
	}
	scope->map = g_hash_table_new (g_str_hash, g_str_equal);
	comp->scope = scope;

	/* compute scope size */
	int varid = 0;
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

	begin_thunk_func (comp, thunkid);
	int allocsize = varid*sizeof(void*);
	int alloc = CALL("i8* @malloc(i32 %d)", allocsize);
	int scopeid = BITCAST("i8* %%%d", "%%thunk**", alloc);
	// set to the current thunk, 0 = first param
	int scopeptr = GETPTR("%%thunk* %%0", "i32 0, i32 %d, i32 %d", THUNK_SCOPES, scope->level);
	STORE("%%thunk** %%%d", "%%thunk*** %%%d", scopeid, scopeptr);

	/* assign values to scope */
	for (int i=0; i < expr->assigns->len; i++) {
		SmAssignExpr* assign = (SmAssignExpr*) expr->assigns->pdata[i];
		GPtrArray* names = assign->names;
		if (names->len == 1) {
			const char* name = (const char*) names->pdata[0];
			int level;
			varid = scope_lookup (scope, name, &level);
			if (varid < 0) {
				printf ("assert not found '%s'\n", name);
				exit(0);
			}

			SmVarType value = VISIT(assign->value);
			int addr = GETPTR("%%thunk** %%%d", "i32 %d", scopeid, varid);
			STORE("%%thunk* %%%d", "%%thunk** %%%d", value.id, addr);
		} else {
			printf("unsupported pattern match\n");
			exit(0);
		}
	}

	SmVarType resthunk = VISIT(expr->result);
	int object = eval_thunk (comp, resthunk.id);
	RET("%%object %%%d", object);
	end_thunk_func (comp, thunkid);

	int thunk = create_thunk (comp, thunkid, 0);

	comp->scope = scope->parent;
	g_hash_table_unref (scope->map);
	free (scope);

	RETVAL({ .id=thunk });
}

DEFUNC(compile_literal, SmLiteral) {
	GET_CODE;
	if (expr->str) {
		// define constant string
		PUSH_BLOCK(comp->decls);
		int consttmp = sm_code_get_temp (code);
		int len = strlen(expr->str)+1;
		// FIXME:
		EMIT_ ("@.const%d = private constant [%d x i8] c\"%s\\00\"", consttmp, len, expr->str);
		POP_BLOCK;

		int thunkid = comp->next_thunkid++;
		// expression code
		begin_thunk_func (comp, thunkid);
		int ptr = GETPTR("[%d x i8]* @.const%d", "i32 0, i32 0", len, consttmp);
		int obj = EMIT ("ptrtoint i8* %%%d to %%object", ptr);
		/* int tagged = EMIT ("or i64 %%%d, 2", num); */
		int tagged = obj;
		RET("%%object %%%d", tagged);
		end_thunk_func (comp, tagged);

		// build thunk
		int thunk = create_thunk (comp, thunkid, 1);
		RETVAL({ .id=thunk });
	} else {
		assert(0);
	}
}

#define CAST(x) (SmVarType (*)(SmCompile*, SmExpr*))(x)
SmVarType (*compile_table[])(SmCompile*, SmExpr*) = {
	[SM_MEMBER_EXPR] = CAST(compile_member_expr),
	[SM_SEQ_EXPR] = CAST(compile_seq_expr),
	[SM_LITERAL] = CAST(compile_literal)
};

static SmVarType call_compile_table (SmCompile* comp, SmExpr* expr) {
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

	comp->decls = sm_code_new_block (code);
	
	PUSH_BLOCK(comp->decls);
	DECLARE ("i32 @printf(i8*, ...)");
	DECLARE ("i8* @malloc(i32)");
	DECLARE ("void @llvm.memcpy.p0i8.p0i8.i32(i8*, i8*, i32, i32, i1)");
	EMIT_ ("%%object = type i64");
	EMIT_ ("%%thunkfunc = type %%object (%%thunk*)*");
	DEFINE_STRUCT ("thunk", "%%thunkfunc, i64, %%object, [0 x %%thunk**]"); // func, jump label index, cached object, scope
	POP_BLOCK;

	PUSH_NEW_BLOCK;
	BEGIN_FUNC("void", "main", "");
	LABEL("entry");
	SmVarType thunk = VISIT(expr);

	// first call
	int object = eval_thunk (comp, thunk.id);
	int strptr = EMIT("inttoptr %%object %%%d to i8*", object);
	CALL ("i32 (i8*, ...)* @printf(i8* %%%d)", strptr);

	// second call
	object = eval_thunk (comp, thunk.id);
	strptr = EMIT("inttoptr %%object %%%d to i8*", object);
	CALL ("i32 (i8*, ...)* @printf(i8* %%%d)", strptr);
	
	RET("void");
	END_FUNC;
	POP_BLOCK;

	char* unit = sm_code_link (code);
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

