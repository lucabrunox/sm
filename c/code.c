#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "code.h"

struct _SmCodeBlock {
	int allocsize;

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

void sm_code_emit (SmCode* code, const char* content) {
	sm_code_emit_raw (code, content);
	sm_code_emit_char (code, '\n');
}

void sm_code_emit_char (SmCode* code, char ch) {
	SmCodeBlock* block = code->current;
	
	if (block->allocsize - block->len <= 2) {
		block->allocsize *= 2;
		block->buf = (char*) realloc (block->buf, block->allocsize);
	}

	block->buf[block->len++] = ch;
	block->buf[block->len] = '\0';
}

void sm_code_emit_raw (SmCode* code, const char* content) {
	int clen = strnlen(content, SM_CODE_MAXCHUNK);
	SmCodeBlock* block = code->current;
	
	if (clen+1 > (block->allocsize - block->len)) {
		while (clen+1 > (block->allocsize - block->len)) {
			block->allocsize = block->allocsize*2+1;
		}
		block->buf = (char*) realloc (block->buf, block->allocsize);
	}
	
	strncat (block->buf, content, clen);
	block->len += clen;
}

/* link all the blocks */
char* sm_code_link (SmCode* code) {
	int size = 0;
	for (int i=0; i < code->len; i++) {
		size += code->blocks[i].len;
	}
	size++;
	
	char* res = (char*) malloc (size);
	for (int i=0; i < code->len; i++) {
		strncat (res, code->blocks[i].buf, code->blocks[i].len);
	}

	return res;
}

SmCodeBlock* sm_code_new_block (SmCode* code) {
	code->len++;
	code->blocks = (SmCodeBlock*) realloc (code->blocks, sizeof(SmCodeBlock)*code->len);
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
