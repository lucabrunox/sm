#include <stdio.h>
#include "llvm.h"

int main() {
	sm_jit_init ();
	
	const char* code = "\n\
; ModuleID = 'hello.c'\n\
\n\
@.str = private constant [7 x i8] c\"hello\\0A\\00\"\n\
\n\
define void @_smc_main() {\n\
  %1 = call i32 (i8*, ...)* @printf(i8* getelementptr ([7 x i8]* @.str, i32 0, i32 0))\n\
  ret void\n\
}\n\
\n\
declare i32 @printf(i8*, ...) #1\n\
\n\
	";

	SmJit* mod = sm_jit_compile ("<stdin>", code);
	void (*entrypoint)() = (void (*)()) sm_jit_get_function (mod, "_smc_main");
	entrypoint();
}