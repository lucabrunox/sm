#include <assert.h>
#include <stdio.h>

#include "compile.h"
#include "ast.h"
#include "llvm.h"
#include "code.h"

typedef struct {
	int id;
	int isconst;
} SmVarType;

typedef struct {
	SmCode* code;
	SmCodeBlock* decls;
	SmVarType ret;
} SmCompile;

#define DEFUNC(n,x) static SmVarType n (SmCompile* comp, x* expr)
#define GET_CODE SmCode* code = comp->code
#define PUSH_BLOCK(x) sm_code_push_block(code, x)
#define POP_BLOCK sm_code_pop_block(code)
#define RETVAL(x) SmVarType _res_var=x; return _res_var
#define VISIT(x) compile_table[x->type](comp, x)

#define TAG(x) (x & 5)
#define SMI_TAG 1
#define STRING_TAG 2
#define CHAR_TAG 4

DEFUNC(compile_member_expr, SmMemberExpr) {
	GET_CODE;
	
}

DEFUNC(compile_seq_expr, SmSeqExpr) {
	GET_CODE;
}

DEFUNC(compile_assign_expr, SmAssignExpr) {
	GET_CODE;
}

DEFUNC(compile_literal, SmLiteral) {
	GET_CODE;
	// FIXME: escape
	if (expr->str) {
		PUSH_BLOCK(comp->decls);
		int consttmp = sm_code_get_temp (code);
		int len = strlen(expr->str)+1;
		EMIT_ ("@.const%d = private constant [%d x i8] c\"%s\\00\"", consttmp, len, expr->str);
		POP_BLOCK;

		int ptr = EMIT ("getelementptr [%d x i8]* @.const%d, i32 0, i32 0", len, consttmp);
		int num = EMIT ("ptrtoint i8* %%%d to i64", ptr);
		int tagged = EMIT ("or i64 %%%d, 2", num);
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
	
	/* int sizeptr_tmp; int size_tmp; */
	PUSH_BLOCK(comp->decls);
	/* EMIT_ ("@.str = private constant [7 x i8] c\"hello\\0A\\00""); */
	DECLARE ("i32 @printf(i8*, ...)");
	POP_BLOCK;
	/* DECLARE ("i8* @malloc(i32)"); */
	/* DEFINE_STRUCT("thunk", "i32, i32"); */
	/* sm_code_pop_block (code); */

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

