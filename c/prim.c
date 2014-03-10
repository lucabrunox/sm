#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "prim.h"
#include "codegen.h"

void sm_prim_print (uint64_t object) {
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
}

void sm_prim_init_print (SmCodegen* gen) {
	static int closure = -1;
	if (closure >= 0) {
		return;
	}
	
	GET_CODE;
	
	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("real print func");
	COMMENT("get string");
	int object = SPGET(0, NULL);
	RUNDBG("-> prim print, object=%p\n", object, NULL);
	RUNDBG("sp=%p\n", LOADSP, "i64*");

	COMMENT("get cont");
	int cont = SPGET(1, "%closure*");

	CALL_("void @sm_prim_print(i64 %%%d)", object);
	
	COMMENT("put object back in the stack");
	FINSP(1, object, NULL);
	RUNDBG("enter %p\n", cont, "%closure*");
	ENTER(cont);
	sm_codegen_end_closure_func (gen);

	PUSH_BLOCK(sm_codegen_get_decls_block (gen));
	EMIT_ ("@primPrint = global %%closure* null, align 8");
	DECLARE ("void @sm_prim_print(i64)");
	POP_BLOCK;

	closure = sm_codegen_create_custom_closure (gen, 0, closureid);
	STORE("%%closure* %%%d", "%%closure** @primPrint", closure);
}
