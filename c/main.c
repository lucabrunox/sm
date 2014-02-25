#include <stdio.h>
#include "llvm.h"
#include "code.h"

#define EMIT(s) sm_code_emit(code, s)

int main() {
	sm_jit_init ();
	
	SmCode* code = sm_code_new ();
	EMIT ("@.str = private constant [7 x i8] c\"hello\\0A\\00\"");
	EMIT ("declare i32 @printf(i8*, ...)");
	EMIT ("declare i8* @malloc(i32)");
	
	EMIT ("%struct.thunk = type { i32, i32 }");
	EMIT ("define %struct.thunk* @_sm_thunk_new() {");
	EMIT ("%sizeptr = getelementptr %struct.thunk* null, i64 1, i32 0");
	EMIT ("%size = ptrtoint i32* %sizeptr to i32");
	EMIT ("%ptr = call i8* @malloc(i32 %size)");
	EMIT ("%cast = bitcast i8* %ptr to %struct.thunk*");
	EMIT ("ret %struct.thunk* %cast");
	EMIT ("}");
	
	EMIT ("define void @_smc_main() {");
	EMIT ("%1 = call i32 (i8*, ...)* @printf(i8* getelementptr ([7 x i8]* @.str, i32 0, i32 0))");
	EMIT ("%2 = call %struct.thunk* @_sm_thunk_new()");
	EMIT ("ret void");
	EMIT ("}");
	

	SmJit* mod = sm_jit_compile ("<stdin>", sm_code_get_unref (&code));
	if (!mod) {
		return 0;
	}

	sm_jit_dump_asm (mod);
	/* sm_jit_dump_ir (mod); */
	
	void (*entrypoint)() = (void (*)()) sm_jit_get_function (mod, "_smc_main");
	if (!entrypoint) {
		return 0;
	}
	entrypoint();

	return 0;
}