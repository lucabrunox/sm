#include <stdio.h>
#include <stdlib.h>
#include "llvm.h"
#include "code.h"
#include "lexer.h"
#include "parser.h"
#include "astdumper.h"

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
	BEGIN_FUNC("void", "main", "");
	CALL ("i32 (i8*, ...)* @printf(i8* getelementptr ([7 x i8]* @.str, i32 0, i32 0))");
	THUNK_NEW ();
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
	/* sm_jit_dump_ir (mod); */
	
	void (*entrypoint)() = (void (*)()) sm_jit_get_function (mod, FUNC("main"));
	if (!entrypoint) {
		return 0;
	}
	entrypoint();

	SmLexer lexer;
	sm_lexer_init (&lexer, "asd");
	SmParser* parser = sm_parser_new ();
	SmExpr* expr = sm_parser_parse (parser, lexer);
	puts (sm_ast_dump (expr));

	return 0;
}