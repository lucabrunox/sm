#ifndef SM_PARSER_H
#define SM_PARSER_H

#include "lexer.h"
#include "ast.h"

typedef struct _SmParser SmParser;

SmParser* sm_parser_new (void);
SmExpr* sm_parser_parse (SmParser* parser, SmLexer lexer);
void sm_parser_free (SmParser* parser);

#endif