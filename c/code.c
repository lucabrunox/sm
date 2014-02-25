#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "code.h"

struct _SmCodeBlock {
	int allocsize;
	int varcount;
	
	int len;
	char* buf;

	SmCodeBlock* parent;
};

struct _SmCode {
	int refcount;
	int len;
	SmCodeBlock* blocks;
	SmCodeBlock* current;
};

SmCode* sm_code_new (void) {
	SmCode* ret = (SmCode*) calloc (1, sizeof (SmCode));
	sm_code_ref (ret);
	return ret;
}

void sm_code_emit (SmCode* code, const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	sm_code_emitv (code, fmt, ap);
	va_end(ap);
}

void sm_code_emitv (SmCode* code, const char* fmt, va_list ap) {
	sm_code_emit_rawv (code, fmt, ap);
	sm_code_emit_char (code, '\n');
}

int sm_code_emit_temp (SmCode* code, const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int res = sm_code_emit_tempv (code, fmt, ap);
	va_end(ap);
	return res;
}

int sm_code_emit_tempv (SmCode* code, const char* fmt, va_list ap) {
	code->current->varcount++;
	sm_code_emit_raw (code, "%%%d = ", code->current->varcount);
	sm_code_emit_rawv (code, fmt, ap);
	sm_code_emit_char (code, '\n');
	return code->current->varcount;
}

void sm_code_emit_char (SmCode* code, char ch) {
	SmCodeBlock* block = code->current;
	
	while (block->allocsize - block->len <= 2) {
		block->allocsize += block->allocsize*2+2;
		block->buf = (char*) realloc (block->buf, block->allocsize);
	}

	block->buf[block->len++] = ch;
	block->buf[block->len] = '\0';
}

void sm_code_emit_raw (SmCode* code, const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	sm_code_emit_rawv (code, fmt, ap);
	va_end(ap);
}

void sm_code_emit_rawv (SmCode* code, const char* fmt, va_list ap) {
	char* content = NULL;
	int clen = vasprintf(&content, fmt, ap);
	
	SmCodeBlock* block = code->current;
	
	if (clen+1 > (block->allocsize - block->len)) {
		while (clen+1 > (block->allocsize - block->len)) {
			block->allocsize = block->allocsize*2+1;
		}
		block->buf = (char*) realloc (block->buf, block->allocsize);
	}
	
	strncpy (block->buf+block->len, content, clen);
	block->len += clen;
}

int sm_code_emit_new_thunk (SmCode* code) {
	int thunk_size = sizeof(void*)*(3); // function pointer + cached value + captured scope
	int ptr = CALL("i8* @malloc(i32 %d)", thunk_size);
	return ptr;
}

/* link all the blocks */
char* sm_code_link (SmCode* code) {
	int size = 0;
	for (int i=0; i < code->len; i++) {
		size += code->blocks[i].len;
	}
	size++;
	
	char* res = (char*) malloc (size);
	res[0] = '\0';

	char* p = res;
	for (int i=0; i < code->len; i++) {
		strncpy (p, code->blocks[i].buf, code->blocks[i].len);
		p += code->blocks[i].len;
	}

	return res;
}

SmCodeBlock* sm_code_new_block (SmCode* code) {
	code->len++;
	code->blocks = (SmCodeBlock*) realloc (code->blocks, sizeof(SmCodeBlock)*code->len);
	memset (&code->blocks[code->len-1], '\0', sizeof (SmCodeBlock));
	return &code->blocks[code->len-1];
}

void sm_code_push_block (SmCode* code, SmCodeBlock* block) {
	block->parent = code->current;
	code->current = block;
}

void sm_code_pop_block (SmCode* code) {
	code->current = code->current->parent;
}

/* parser is not multi threaded */
SmCode* sm_code_ref (SmCode* code) {
	code->refcount++;
	return code;
}

void sm_code_unref (SmCode* code) {
	if (!--code->refcount) {
		for (int i=0; i < code->len; i++) {
			free (code->blocks[i].buf);
		}
		if (code->blocks != NULL) {
			free (code->blocks);
		}
		free (code);
	}
}
