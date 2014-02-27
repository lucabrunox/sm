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

#define TAG(x) (x & 5)
#define SMI_TAG 1
#define STRING_TAG 2
#define CHAR_TAG 4


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
} SmCompile;

static SmVarType call_compile_table (SmCompile* comp, SmExpr* expr);

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
		char** p = NULL;
		while ( (p=(char**)utarray_next(el->expr->names, p))) {
			const char* name = *p;

			SmScope* entry;
			HASH_FIND_STR(comp->scope, name, entry);
			if (entry) {
				printf("shadowing %s\n", *p);
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
		// FIXME: fuckoff utarray
		char** p = NULL;
		while ( (p=(char**)utarray_next(el->expr->names, p))) {
			const char* name = *p;
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
		PUSH_BLOCK(comp->decls);
		int consttmp = sm_code_get_temp (code);
		int len = strlen(expr->str)+1;
		// FIXME: escape
		EMIT_ ("@.const%d = private constant [%d x i8] c\"%s\\00\"", consttmp, len, expr->str);
		POP_BLOCK;

		int ptr = EMIT ("getelementptr [%d x i8]* @.const%d, i32 0, i32 0", len, consttmp);
		int num = EMIT ("ptrtoint i8* %%%d to i64", ptr);
		/* int tagged = EMIT ("or i64 %%%d, 2", num); */
		int tagged = num;
		int res = EMIT ("inttoptr i64 %%%d to i8*", tagged);
		RETVAL({ .id=res });
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
	POP_BLOCK;

	PUSH_BLOCK (sm_code_new_block (code));
	BEGIN_FUNC("void", "main", "");
	SmVarType tmp = VISIT(expr);
	CALL ("i32 (i8*, ...)* @printf(i8* %%%d)", tmp.id);
	RET("void", "");
	END_FUNC();
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

