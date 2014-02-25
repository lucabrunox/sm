#include <stdio.h>
#include <stdlib.h>
#include "llvm.h"
#include "code.h"

#define EMIT_(s,...) sm_code_emit(code, s, ##__VA_ARGS__)
#define EMIT(s,...) sm_code_emit_temp(code, s, ##__VA_ARGS__)
#define DECLARE(s,...) EMIT_("declare " s, ##__VA_ARGS__)
#define DEFINE_STRUCT(s,fields,...) EMIT_("%%struct." s " = type { " fields " }", ##__VA_ARGS__)
#define STRUCT(s) "%%struct." s
#define FUNC(s) "@_smc_" s
#define BEGIN_FUNC(ret,name,params,...) EMIT_("define " ret " @_smc_" name "(" params ") {", ##__VA_ARGS__)
#define END_FUNC() EMIT_("}")
#define CALL(s,...) EMIT("call " s, ##__VA_ARGS__);
#define BITCAST(f,t,...) EMIT("bitcast " f " to " t, ##__VA_ARGS__)
#define SIZEOF(t,...) sizeptr_tmp=EMIT("getelementptr " t " null, i64 1, i32 0",##__VA_ARGS__); \
                  size_tmp=EMIT("ptrtoint i32* %%%d to i32", sizeptr_tmp)
#define RET(t,v,...) EMIT_("ret " t v, ##__VA_ARGS__)

int main() {
	sm_jit_init ();

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
	BEGIN_FUNC(STRUCT("thunk*"), "thunk_new", "");
	SIZEOF(STRUCT("thunk*"));
	int ptr = CALL("i8* @malloc(i32 %%%d)", size_tmp);
	int cast = BITCAST("i8* %%%d", STRUCT("thunk*"), ptr);
	RET(STRUCT("thunk*"), "%%%d", cast);
	END_FUNC();
	sm_code_pop_block (code);
	
	sm_code_push_block (code, sm_code_new_block (code));
	BEGIN_FUNC("void", "main", "");
	CALL ("i32 (i8*, ...)* @printf(i8* getelementptr ([7 x i8]* @.str, i32 0, i32 0))");
	CALL (STRUCT("thunk*") FUNC("thunk_new") "()");
	RET("void", "");
	END_FUNC();
	sm_code_pop_block (code);

	char* unit = sm_code_link (code);
	SmJit* mod = sm_jit_compile ("<stdin>", unit);
	free (unit);
	sm_code_unref (code);
	if (!mod) {
		return 0;
	}

	/* sm_jit_dump_asm (mod); */
	sm_jit_dump_ir (mod);
	
	void (*entrypoint)() = (void (*)()) sm_jit_get_function (mod, FUNC("main"));
	if (!entrypoint) {
		return 0;
	}
	entrypoint();

	return 0;
}