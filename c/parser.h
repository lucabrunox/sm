#ifndef SM_PARSER_H
#define SM_PARSER_H

#include "lexer.h"

typedef struct _SmParser SmParser;

SmParser* sm_parser_new (void);
void sm_parser_parse (SmParser* parser, SmLexer lexer);
void sm_parser_free (SmParser* parser);

#endif