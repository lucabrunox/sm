#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "code.h"

SmCode* sm_code_new (void) {
	SmCode* ret = (SmCode*) calloc (1, sizeof (SmCode));
	sm_code_ref (ret);
	return ret;
}

void sm_code_emit (SmCode* code, const char* content) {
	sm_code_emit_raw (code, content);
	sm_code_emit_char (code, '\n');
}

void sm_code_emit_char (SmCode* code, char ch) {
	if (code->allocsize - code->len <= 2) {
		code->allocsize *= 2;
		code->buf = (char*) realloc (code->buf, code->allocsize);
	}

	code->buf[code->len++] = ch;
	code->buf[code->len] = '\0';
}

void sm_code_emit_raw (SmCode* code, const char* content) {
	int clen = strnlen(content, SM_CODE_MAXCHUNK);
	if (clen+1 > (code->allocsize - code->len)) {
		while (clen+1 > (code->allocsize - code->len)) {
			code->allocsize = code->allocsize*2+1;
		}
		code->buf = (char*) realloc (code->buf, code->allocsize);
	}
	
	strncat (code->buf, content, clen);
	code->len += clen;
}

/* return buffer ownership and unref */
char* sm_code_get_unref (SmCode** code) {
	char* res = (*code)->buf;
	(*code)->buf = NULL;
	sm_code_unref (*code);
	*code = NULL;
	return res;
}

/* parser is not multi threaded */
SmCode* sm_code_ref (SmCode* code) {
	code->refcount++;
	return code;
}

void sm_code_unref (SmCode* code) {
	if (!--code->refcount) {
		if (code->buf) {
			free (code->buf);
		}
		free (code);
	}
}
