#include <stdio.h>
#include <stdlib.h>
#include <glib.h>

#include "llvm.h"
#include "code.h"
#include "lexer.h"
#include "parser.h"
#include "compile.h"

int main(int argc, char** argv) {
	if (argc < 2) {
		fprintf(stderr, "Usage: %s script.sm\n", argv[0]);
		return 1;
	}

	char* code;
	GError* err = NULL;
	if (!g_file_get_contents (argv[1], &code, NULL, &err)) {
		g_error (err->message);
	}
	
	SmLexer lexer;
	sm_lexer_init (&lexer, code);
	SmParser* parser = sm_parser_new ();
	SmExpr* expr = sm_parser_parse (parser, lexer);
	sm_parser_free (parser);
	free (code);
	
	if (expr) {
		/* char* dump = sm_ast_dump (expr); */
		/* if (dump) { */
			/* puts (dump); */
		/* } */
		/* free (dump); */

		SmCodegenOpts opts = { .debug=FALSE };
		SmJit* mod = sm_compile (opts, "<stdin>", expr);
		if (mod) {
			/* sm_jit_dump_asm (mod); */
			/* sm_jit_dump_ir (mod); */
			sm_run (mod);
		}
	}

	return 0;
}