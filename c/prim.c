#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "prim.h"
#include "codegen.h"

uint64_t sm_prim_print (uint64_t object) {
	uint64_t tag = object & TAG_MASK;
	switch (tag) {
		case TAG_INT:
			printf("%d\n", (int)(object & OBJ_MASK));
			break;
		case TAG_STR:
			printf("%s\n", (const char*)(object & OBJ_MASK));
			break;
		case TAG_CHR:
			printf("%c\n", (char)(object & OBJ_MASK));
			break;
		default:
			printf("(cannot print object with tag: %lx)", tag);
	}
	
	return object;
}

uint64_t sm_prim_is_eos (uint64_t object) {
	if (object == OBJ_EOS) {
		return OBJ_TRUE;
	} else {
		return OBJ_FALSE;
	}
}

void sm_prim_init_op (SmCodegen* gen, const char* prim_name, const char* prim_func) {
	GET_CODE;
	
	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("%s func", prim_name);
	int object = SPGET(0, NULL);
	RUNDBG("-> prim, sp=%p\n", LOADSP, "i64*");
	RUNDBG("arg=%p\n", object, NULL);

	COMMENT("get cont");
	int cont = SPGET(1, "%closure*");

	object = CALL("i64 @%s(i64 %%%d)", prim_func, object);
	
	COMMENT("put result back in the stack");
	FINSP(1, object, NULL);
	RUNDBG("enter %p\n", cont, "%closure*");
	ENTER(cont);
	sm_codegen_end_closure_func (gen);

	PUSH_BLOCK(sm_codegen_get_decls_block (gen));
	EMIT_ ("@%s = global %%closure* null, align 8", prim_name);
	DECLARE ("i64 @%s(i64)", prim_func);
	POP_BLOCK;

	int closure = sm_codegen_create_custom_closure (gen, 0, closureid);
	STORE("%%closure* %%%d", "%%closure** @%s", closure, prim_name);
}

void sm_prim_init_eos (SmCodegen* gen) {
	GET_CODE;
	
	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("eos closure");
	RUNDBG("-> eos object, sp=%p\n", LOADSP, "i64*");
	int cont = SPGET(0, "%closure*");

	COMMENT("set eos object on the stack");
	STORE("i64 %llu", "i64* %%%d", OBJ_EOS, LOADSP);
	
	COMMENT("enter cont");
	ENTER(cont);
	sm_codegen_end_closure_func (gen);

	PUSH_BLOCK(sm_codegen_get_decls_block (gen));
	EMIT_ ("@eos = global %%closure* null, align 8");
	POP_BLOCK;

	int closure = sm_codegen_create_custom_closure (gen, 0, closureid);
	STORE("%%closure* %%%d", "%%closure** @eos", closure);
}
