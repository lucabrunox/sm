#include <stdio.h>
#include "llvm.h"
#include "code.h"

#define EMIT(s) sm_code_emit(code, s)
#define DEFINE_STRUCT(s,fields) EMIT("%struct." s " = type { " fields " }")
#define STRUCT(s) "%struct." s
#define FUNC(s) "@_smc_" s
#define DEFINE_FUNC(ret,name,params) EMIT("define " ret " @_smc_" name "(" params ") {")
#define END_FUNC() EMIT("}")

int main() {
	sm_jit_init ();
	
	SmCode* code = sm_code_new ();
	EMIT ("@.str = private constant [7 x i8] c\"hello\\0A\\00\"");
	EMIT ("declare i32 @printf(i8*, ...)");
	EMIT ("declare i8* @malloc(i32)");

	DEFINE_STRUCT("thunk", "i32, i32");
	DEFINE_FUNC(STRUCT("thunk*"), "thunk_new", "");
	EMIT ("%sizeptr = getelementptr %struct.thunk* null, i64 1, i32 0");
	EMIT ("%size = ptrtoint i32* %sizeptr to i32");
	EMIT ("%ptr = call i8* @malloc(i32 %size)");
	EMIT ("%cast = bitcast i8* %ptr to %struct.thunk*");
	EMIT ("ret %struct.thunk* %cast");
	EMIT ("}");
	
	EMIT ("define void @_smc_main() {");
	EMIT ("%1 = call i32 (i8*, ...)* @printf(i8* getelementptr ([7 x i8]* @.str, i32 0, i32 0))");
	EMIT ("%2 = call %struct.thunk* " FUNC("thunk_new") "()");
	EMIT ("ret void");
	END_FUNC();
	

	SmJit* mod = sm_jit_compile ("<stdin>", sm_code_get_unref (&code));
	if (!mod) {
		return 0;
	}

	/* sm_jit_dump_asm (mod); */
	sm_jit_dump_ir (mod);
	
	void (*entrypoint)() = (void (*)()) sm_jit_get_function (mod, "_smc_main");
	if (!entrypoint) {
		return 0;
	}
	entrypoint();

	return 0;
}