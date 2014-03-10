#include <stdio.h>
#include <stdlib.h>
#include <glib.h>

#include "llvm.h"
#include "code.h"
#include "lexer.h"
#include "parser.h"
#include "compile.h"

char* read_all (FILE* f) {
	size_t len = 0;
	size_t size = 4096;
	char* buf = (char*) malloc (size);
	size_t r;
	while ((r = fread (buf+len, 1, size-len-1, f))) {
		len += r;
		if (len >= size-1) {
			size *= 2;
			buf = (char*) realloc (buf, size);
		}
	}

	buf[len] = '\0';
	return buf;
}

int main() {
	
	SmLexer lexer;
	char* code = read_all (stdin);
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