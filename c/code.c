#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "code.h"
#include "uthash/src/utlist.h"

struct _SmCodeBlock {
	int allocsize;
	int varcount;
	
	int len;
	char* buf;

	SmCodeBlock* parent;
	SmCodeBlock* next;
};

struct _SmCode {
	int refcount;
	int len;
	SmCodeBlock* head;
	SmCodeBlock* tail;
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

int sm_code_get_temp (SmCode* code) {
	return code->current->varcount++;
}

int sm_code_emit_temp (SmCode* code, const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int res = sm_code_emit_tempv (code, fmt, ap);
	va_end(ap);
	return res;
}

int sm_code_emit_tempv (SmCode* code, const char* fmt, va_list ap) {
	SmCodeBlock* block = code->current;
	sm_code_emit_raw (code, "%%%d = ", block->varcount);
	sm_code_emit_rawv (code, fmt, ap);
	sm_code_emit_char (code, '\n');
	return block->varcount++;
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

/* link all the blocks */
char* sm_code_link (SmCode* code) {
	int size = 0;
	SmCodeBlock* cur = code->head;
	while (cur) {
		size += cur->len;
		cur = cur->next;
	}
	size++;
	
	char* res = (char*) malloc (size);
	res[0] = '\0';

	char* p = res;
	cur = code->head;
	while (cur) {
		strncpy (p, cur->buf, cur->len);
		p += cur->len;
		cur = cur->next;
	}

	return res;
}

SmCodeBlock* sm_code_new_block (SmCode* code) {
	SmCodeBlock* block = (SmCodeBlock*) calloc(1, sizeof(SmCodeBlock));
	if (!code->head) {
		code->head = code->tail = block;
	} else {
		code->tail->next = block;
		code->tail = block;
	}
	return block;
}

void sm_code_push_block (SmCode* code, SmCodeBlock* block) {
	block->parent = code->current;
	code->current = block;
}

void sm_code_pop_block (SmCode* code) {
	code->current = code->current->parent;
}

SmCode* sm_code_ref (SmCode* code) {
	code->refcount++;
	return code;
}

void sm_code_unref (SmCode* code) {
	if (!--code->refcount) {
		SmCodeBlock* cur = code->head;
		while (cur) {
			SmCodeBlock* tmp = cur->next;
			free (cur);
			cur = tmp;
		}
		free (code);
	}
}
