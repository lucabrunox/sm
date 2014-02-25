#include <stdio.h>
#include "llvm.h"
#include "code.h"

int main() {
	sm_jit_init ();
	
	SmCode* code = sm_code_new ();
	sm_code_emit (code, "@.str = private constant [7 x i8] c\"hello\\0A\\00\"");
	sm_code_emit (code, "define void @_smc_main() {\n");
	sm_code_emit (code, "%1 = call i32 (i8*, ...)* @printf(i8* getelementptr ([7 x i8]* @.str, i32 0, i32 0))\n");
	sm_code_emit (code, "ret void\n");
	sm_code_emit (code, "}\n");
	sm_code_emit (code, "declare i32 @printf(i8*, ...) #1\n");

	SmJit* mod = sm_jit_compile ("<stdin>", sm_code_get_unref (&code));
	if (!mod) {
		return 0;
	}
	void (*entrypoint)() = (void (*)()) sm_jit_get_function (mod, "_smc_main");
	if (!entrypoint) {
		return 0;
	}
	
	entrypoint();
	return 0;
}