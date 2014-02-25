#ifndef SM_CODE_H
#define SM_CODE_H

#define SM_CODE_MAXCHUNK 1024

typedef struct {
	int allocsize;
	int refcount;

	int len;
	char* buf;
} SmCode;

SmCode* sm_code_new (void);
void sm_code_emit (SmCode* code, const char* line);
void sm_code_emit_raw (SmCode* code, const char* raw);
void sm_code_emit_char (SmCode* code, char ch);
char* sm_code_get_unref (SmCode** code);

SmCode* sm_code_ref (SmCode* code);
void sm_code_unref (SmCode* code);

#endif