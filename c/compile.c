#include <assert.h>
#include <stdio.h>
#include <glib.h>

#include "compile.h"
#include "ast.h"
#include "llvm.h"
#include "code.h"
#include "uthash/src/utarray.h"
#include "uthash/src/utlist.h"

#define DEFUNC(n,x) static SmVarType n (SmCompile* comp, x* expr)
#define GET_CODE SmCode* code = comp->code
#define PUSH_BLOCK(x) sm_code_push_block(code, x)
#define POP_BLOCK sm_code_pop_block(code)
#define RETVAL(x) SmVarType _res_var=x; return _res_var
#define VISIT(x) call_compile_table (comp, EXPR(x))
#define PUSH_NEW_BLOCK PUSH_BLOCK(sm_code_new_block (code))

#define TAG(x) (x & 5)
#define SMI_TAG 1
#define STRING_TAG 2
#define CHAR_TAG 4

#define THUNK_FUNC 0
#define THUNK_JUMP 1
#define THUNK_CACHE 2
#define THUNK_SCOPE 3

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
	int scopeid;
	int next_thunkid;
} SmCompile;

static SmVarType call_compile_table (SmCompile* comp, SmExpr* expr);

static int scope_lookup (SmScope* scope, const char* name, int* level) {
	while (scope) {
		int64_t id;
		if (g_hash_table_lookup_extended (scope->map, name, NULL, &id)) {
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
	EMIT_ ("@thunklabel_%d = internal global [2 x i8*] [i8* blockaddress(" FUNC("thunk_%d") ", %%eval), i8* blockaddress(" FUNC("thunk_%d") ", %%cache)], align 8",
		   thunkid, thunkid, thunkid);
	POP_BLOCK;

	PUSH_NEW_BLOCK;
	BEGIN_FUNC("%%object", "thunk_%d", "%%thunk*", thunkid);
	int thunk = sm_code_get_temp (code); // first param
	LABEL("entry");
	int jmpptr = EMIT ("getelementptr %%thunk* %%%d, i32 0, i32 %d", thunk, THUNK_JUMP);
	int jmp = EMIT ("load i32* %%%d", jmpptr);
	int labelptr = EMIT("getelementptr inbounds [2 x i8*]* @thunklabel_%d, i32 0, i32 %%%d", thunkid, jmp);
	int label = EMIT("load i8** %%%d, align 8", labelptr);
	EMIT_("indirectbr i8* %%%d, [label %%eval, label %%cache]", label);

	LABEL("eval");
	// next jump will point to the cache
	EMIT_("store i32 1, i32* %%%d", jmpptr);
}

static void end_thunk_func (SmCompile* comp, int result) {
	GET_CODE;
	int thunk = 0; // first param
	
	LABEL("cache");
	int objptr = EMIT("getelementptr %%thunk* %%%d, i32 0, i32 %d", thunk, THUNK_CACHE);
	int obj = EMIT("load %%object* %%%d", objptr);
	RET("%%object %%%d", obj);
	
	END_FUNC;
	POP_BLOCK;
}

static int eval_thunk (SmCompile* comp, int thunk) {
	GET_CODE;
	int funcptr = EMIT("getelementptr %%thunk* %%%d, i32 0, i32 %d", thunk, THUNK_FUNC);
	int func = EMIT("load %%thunkfunc* %%%d", funcptr);

	// eval thunk
	int object = CALL("%%object %%%d(%%thunk* %%%d)", func, thunk);
	// cache evaluation
	int objptr = EMIT("getelementptr %%thunk* %%%d, i32 0, i32 %d", thunk, THUNK_CACHE);
	EMIT_("store %%object %%%d, %%object* %%%d", object, objptr);

	return object;
}

static int create_thunk (SmCompile* comp, int thunkid) {
	GET_CODE;
	int thunksizeptr = EMIT("getelementptr %%thunk* null, i32 1, i32 0");
	int thunksize = EMIT("ptrtoint %%thunkfunc* %%%d to i32", thunksizeptr);
	int alloc = EMIT("call i8* @malloc(i32 %%%d)", thunksize);
	int thunk = EMIT("bitcast i8* %%%d to %%thunk*", alloc);

	int funcptr = EMIT("getelementptr %%thunk* %%%d, i32 0, i32 %d", thunk, THUNK_FUNC);
	EMIT_("store %%thunkfunc " FUNC("thunk_%d") ", %%thunkfunc* %%%d", thunkid, funcptr);

	int jmpptr = EMIT("getelementptr %%thunk* %%%d, i32 0, i32 %d", thunk, THUNK_JUMP);
	EMIT_("store i32 0, i32* %%%d", jmpptr);

	int scopeptr = EMIT("getelementptr %%thunk* %%%d, i32 0, i32 %d", thunk, THUNK_SCOPE);
	EMIT_("store %%thunk** %%%d, %%thunk*** %%%d", comp->scopeid, scopeptr);

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

	int addr = EMIT("getelementptr %%thunk** %%%d, i32 %d", comp->scopeid, varid);
	int res = EMIT("load %%thunk** %%%d", addr);
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
	SmAssignList* el = NULL;
	DL_FOREACH(expr->assigns, el) {
		GPtrArray* names = el->expr->names;
		for (int i=0; i < names->len; i++) {
			const char* name = (const char*) names->pdata[i];
			printf("%d\n", strlen(name));

			int existing = scope_lookup (scope, name, NULL);
			if (existing >= 0) {
				printf("shadowing %s\n", name);
				exit(0);
			}

			varid++;
			g_hash_table_insert (scope->map, name, GINT_TO_POINTER(varid));
		}
	}

	int allocsize = varid*sizeof(void*);
	int alloc = EMIT("call i8* @malloc(i32 %d)", allocsize);
	comp->scopeid = EMIT("bitcast i8* %%%d to %%thunk**", alloc);

	/* assign values to scope */
	el = NULL;
	DL_FOREACH(expr->assigns, el) {
		GPtrArray* names = el->expr->names;
		if (names->len == 1) {
			const char* name = (const char*) names->pdata[0];
			int level;
			varid = scope_lookup (scope, name, &level);
			if (varid < 0) {
				printf ("assert not found '%s'\n", name);
				exit(0);
			}

			SmVarType value = VISIT(el->expr->value);
			int addr = EMIT("getelementptr %%thunk** %%%d, i32 %d", comp->scopeid, varid);
			EMIT_("store %%thunk* %%%d, %%thunk** %%%d", value.id, addr);
		} else {
			printf("unsupported pattern match\n");
			exit(0);
		}
	}

	SmVarType result = VISIT(expr->result);

	comp->scope = scope->parent;
	g_hash_table_unref (scope->map);
	free (scope);

	return result;
}

DEFUNC(compile_assign_expr, SmAssignExpr) {
	GET_CODE;
}

DEFUNC(compile_literal, SmLiteral) {
	GET_CODE;
	if (expr->str) {
		// define constant string
		PUSH_BLOCK(comp->decls);
		int consttmp = sm_code_get_temp (code);
		int len = strlen(expr->str)+1;
		// FIXME: escape
		EMIT_ ("@.const%d = private constant [%d x i8] c\"%s\\00\"", consttmp, len, expr->str);
		POP_BLOCK;

		int thunkid = comp->next_thunkid++;
		// expression code
		begin_thunk_func (comp, thunkid);
		int ptr = EMIT ("getelementptr [%d x i8]* @.const%d, i32 0, i32 0", len, consttmp);
		int obj = EMIT ("ptrtoint i8* %%%d to %%object", ptr);
		/* int tagged = EMIT ("or i64 %%%d, 2", num); */
		int tagged = obj;
		RET("%%object %%%d", tagged);
		end_thunk_func (comp, tagged);

		// build thunk
		int thunk = create_thunk (comp, thunkid);
		RETVAL({ .id=thunk });
	} else {
		assert(0);
	}
}

#define CAST(x) (SmVarType (*)(SmCompile*, SmExpr*))(x)
SmVarType (*compile_table[])(SmCompile*, SmExpr*) = {
	[SM_MEMBER_EXPR] = CAST(compile_member_expr),
	[SM_SEQ_EXPR] = CAST(compile_seq_expr),
	[SM_ASSIGN_EXPR] = CAST(compile_assign_expr),
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
	EMIT_ ("%%object = type i64");
	EMIT_ ("%%thunkfunc = type %%object (%%thunk*)*");
	DEFINE_STRUCT ("thunk", "%%thunkfunc, i32, %%object, %%thunk**"); // func, jump label index, cached object, scope
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

