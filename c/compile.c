#include "compile.h"
#include "ast.h"
#include "llvm.h"
#include "code.h"

SmJit* sm_compile (SmExpr* expr) {
	static int initialized = 0;
	if (!initialized) {
		initialized = 1;
		sm_jit_init ();
	}

	int sizeptr_tmp; int size_tmp;
	
	SmCode* code = sm_code_new ();
	SmCodeBlock* decls = sm_code_new_block (code);
	sm_code_push_block (code, decls);
	EMIT_ ("@.str = private constant [7 x i8] c\"hello\\0A\\00\"");
	DECLARE ("i32 @printf(i8*, ...)");
	DECLARE ("i8* @malloc(i32)");
	DEFINE_STRUCT("thunk", "i32, i32");
	sm_code_pop_block (code);

	sm_code_push_block (code, sm_code_new_block (code));
	BEGIN_FUNC("void", "main", "");
	CALL ("i32 (i8*, ...)* @printf(i8* getelementptr ([7 x i8]* @.str, i32 0, i32 0))");
	THUNK_NEW ();
	RET("void", "");
	END_FUNC();
	sm_code_pop_block (code);

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

