#ifndef SM_CODE_H
#define SM_CODE_H

#include <stdarg.h>

#define SM_CODE_MAXCHUNK 1024

typedef struct _SmCode SmCode;
typedef struct _SmCodeBlock SmCodeBlock;

SmCode* sm_code_new (void);
void sm_code_emit (SmCode* code, const char* fmt, ...) __attribute__ ((format (printf, 2, 3)));
void sm_code_emitv (SmCode* code, const char* fmt, va_list ap);
int sm_code_emit_temp (SmCode* code, const char* fmt, ...) __attribute__ ((format (printf, 2, 3)));
int sm_code_emit_tempv (SmCode* code, const char* fmt, va_list ap);
void sm_code_emit_raw (SmCode* code, const char* fmt, ...) __attribute__ ((format (printf, 2, 3)));
void sm_code_emit_rawv (SmCode* code, const char* fmt, va_list ap);
void sm_code_emit_char (SmCode* code, char ch);

SmCodeBlock* sm_code_new_block (SmCode* code);
void sm_code_push_block (SmCode* code, SmCodeBlock* block);
void sm_code_pop_block (SmCode* code);
char* sm_code_link (SmCode* code);

char* sm_code_get_unref (SmCode** code);

SmCode* sm_code_ref (SmCode* code);
void sm_code_unref (SmCode* code);

#endif