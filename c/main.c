#include <stdio.h>
#include <stdlib.h>
#include "llvm.h"
#include "code.h"
#include "lexer.h"
#include "parser.h"
#include "astdumper.h"
#include "compile.h"

int main() {
	SmLexer lexer;
	/* sm_lexer_init (&lexer, "'foo\\n'"); */
	sm_lexer_init (&lexer, "asd = 'foo'; asd");
	SmParser* parser = sm_parser_new ();
	SmExpr* expr = sm_parser_parse (parser, lexer);
	if (expr) {
		char* dump = sm_ast_dump (expr);
		if (dump) {
			puts (dump);
		}
		free (dump);
	}
	sm_parser_free (parser);

	SmJit* mod = sm_compile ("<stdin>", expr);
	if (mod) {
		sm_jit_dump_asm (mod);
		sm_jit_dump_ir (mod);
		sm_run (mod);
	}

	return 0;
}