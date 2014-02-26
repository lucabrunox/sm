#ifndef SM_CODE_H
#define SM_CODE_H

#include <stdarg.h>

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
#define THUNK_NEW() sm_code_emit_new_thunk(code)
#define SIZEOF(t,...) sizeptr_tmp=EMIT("getelementptr " t " null, i64 1, i32 0",##__VA_ARGS__); \
                  size_tmp=EMIT("ptrtoint i32* %%%d to i32", sizeptr_tmp)
#define RET(t,v,...) EMIT_("ret " t v, ##__VA_ARGS__)

typedef struct _SmCode SmCode;
typedef struct _SmCodeBlock SmCodeBlock;

SmCode* sm_code_new (void);
void sm_code_emit (SmCode* code, const char* fmt, ...) __attribute__ ((format (printf, 2, 3)));
void sm_code_emitv (SmCode* code, const char* fmt, va_list ap);
int sm_code_get_temp (SmCode* code);
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