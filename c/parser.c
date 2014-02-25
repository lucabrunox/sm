#include <stdlib.h>

#include "lexer.h"
#include "parser.h"

struct _SmParser {
	SmLexer lexer;
	SmToken cur;
};

SmParser* sm_parser_new (void) {
	SmParser* parser = (SmParser*) calloc (1, sizeof (SmParser));
	return parser;
}

void sm_parser_parse (SmParser* parser, SmLexer lexer) {
	parser->lexer = lexer;
	parser->cur = {0};
}
	
void sm_parser_free (SmParser* parser) {
	sm_token_destroy (&parser->cur);
	free (parser);
}

