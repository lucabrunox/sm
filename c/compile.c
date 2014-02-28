#include <assert.h>
#include <stdio.h>

#include "compile.h"
#include "ast.h"
#include "llvm.h"
#include "code.h"
#include "uthash/src/utarray.h"
#include "uthash/src/utlist.h"
#include "uthash/src/uthash.h"

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

typedef struct {
	char name[256];
	int id;
	UT_hash_handle hh;
} SmScope;

typedef struct {
	SmCode* code;
	SmCodeBlock* decls;
	SmVarType ret;
	SmScope* scope;
	int scopeid;
	int thunkid;
} SmCompile;

static SmVarType call_compile_table (SmCompile* comp, SmExpr* expr);

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

	return thunk;
}

DEFUNC(compile_member_expr, SmMemberExpr) {
	GET_CODE;
	if (expr->inner) {
		printf("unsupported inner member\n");
		exit(0);
	}
	
	SmScope* entry;
	HASH_FIND_STR(comp->scope, expr->name, entry);
	if (!entry) {
		printf("not in scope %s\n", expr->name);
		exit(0);
	}

	int addr = EMIT("getelementptr i8** %%%d, i32 %d", comp->scopeid, entry->id);
	int res = EMIT("load i8** %%%d", addr);
	RETVAL({ .id=res });
}

DEFUNC(compile_seq_expr, SmSeqExpr) {
	GET_CODE;
	comp->scope = NULL;

	/* compute scope size */
	int varid = 0;
	SmAssignList* el = NULL;
	DL_FOREACH(expr->assigns, el) {
		GPtrArray* names = el->expr->names;
		for (int i=0; i < names->len; i++) {
			const char* name = (const char*) names->pdata[i];

			SmScope* entry;
			HASH_FIND_STR(comp->scope, name, entry);
			if (entry) {
				printf("shadowing %s\n", name);
				exit(0);
			}

			varid++;
			entry = (SmScope*)calloc(1, sizeof(SmScope));
			strcpy (entry->name, name);
			entry->id = varid;
			HASH_ADD_STR(comp->scope, name, entry);
		}
	}

	int allocsize = varid*sizeof(void*);
	int alloc = EMIT("call i8* @malloc(i32 %d)", allocsize);
	comp->scopeid = EMIT("bitcast i8* %%%d to i8**", alloc);

	/* assign values to scope */
	el = NULL;
	DL_FOREACH(expr->assigns, el) {
		GPtrArray* names = el->expr->names;
		if (names->len == 1) {
			const char* name = (const char*) names->pdata[0];
			SmVarType value = VISIT(el->expr->value);
			
			SmScope* found;
			HASH_FIND_STR(comp->scope, name, found);
			if (!found) {
				printf ("assert not found '%s'\n", name);
				exit(0);
			}
			int addr = EMIT("getelementptr i8** %%%d, i32 %d", comp->scopeid, found->id);
			EMIT_("store i8* %%%d, i8** %%%d", value.id, addr);
		}
	}

	SmVarType result = VISIT(expr->result);
	return result;
}

DEFUNC(compile_assign_expr, SmAssignExpr) {
	GET_CODE;
}

DEFUNC(compile_literal, SmLiteral) {
	GET_CODE;
	if (expr->str) {
		int thunkid = comp->thunkid++;
		
		PUSH_BLOCK(comp->decls);
		int consttmp = sm_code_get_temp (code);
		int len = strlen(expr->str)+1;
		// FIXME: escape
		EMIT_ ("@.const%d = private constant [%d x i8] c\"%s\\00\"", consttmp, len, expr->str);
		EMIT_ ("@thunklabel_%d = internal global [2 x i8*] [i8* blockaddress(" FUNC("thunk_%d") ", %%eval), i8* blockaddress(" FUNC("thunk_%d") ", %%cache)], align 8",
			   thunkid, thunkid, thunkid);
		POP_BLOCK;

		PUSH_NEW_BLOCK;
		BEGIN_FUNC("%%object", "thunk_%d", "%%thunk*", thunkid);
		int thunk = sm_code_get_temp(code);
		LABEL("entry");
		int jmpptr = EMIT ("getelementptr %%thunk* %%%d, i32 0, i32 %d", thunk, THUNK_JUMP);
		int jmp = EMIT ("load i32* %%%d", jmpptr);
		int labelptr = EMIT("getelementptr inbounds [2 x i8*]* @thunklabel_%d, i32 0, i32 %%%d", thunkid, jmp);
		int label = EMIT("load i8** %%%d, align 8", labelptr);
		EMIT_("indirectbr i8* %%%d, [label %%eval, label %%cache]", label);
		
		LABEL("eval");
		int ptr = EMIT ("getelementptr [%d x i8]* @.const%d, i32 0, i32 0", len, consttmp);
		int obj = EMIT ("ptrtoint i8* %%%d to %%object", ptr);
		/* int tagged = EMIT ("or i64 %%%d, 2", num); */
		int tagged = obj;

		// cache
		int objptr = EMIT("getelementptr %%thunk* %%%d, i32 0, i32 %d", thunk, THUNK_CACHE);
		EMIT_("store %%object %%%d, %%object* %%%d", tagged, objptr);
		EMIT_("store i32 1, i32* %%%d", jmpptr);
		RET("%%object %%%d", tagged);

		LABEL("cache");
		objptr = EMIT("getelementptr %%thunk* %%%d, i32 0, i32 %d", thunk, THUNK_CACHE);
		obj = EMIT("load %%object* %%%d", objptr);
		RET("%%object %%%d", obj);
		
		END_FUNC;
		POP_BLOCK;

		// build thunk
		thunk = create_thunk (comp, thunkid);
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
	DEFINE_STRUCT ("thunk", "%%thunkfunc, i32, %%object, %%thunk*"); // func, jump label index, cached object, scope
	POP_BLOCK;

	PUSH_NEW_BLOCK;
	BEGIN_FUNC("void", "main", "");
	LABEL("entry");
	SmVarType thunk = VISIT(expr);
	int funcptr = EMIT("getelementptr %%thunk* %%%d, i32 0, i32 %d", thunk.id, THUNK_FUNC);
	int func = EMIT("load %%thunkfunc* %%%d", funcptr);

	// first call
	int object = CALL("%%object %%%d(%%thunk* %%%d)", func, thunk.id);
	int strptr = EMIT("inttoptr %%object %%%d to i8*", object);
	CALL ("i32 (i8*, ...)* @printf(i8* %%%d)", strptr);

	// second call
	object = CALL("%%object %%%d(%%thunk* %%%d)", func, thunk.id);
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

