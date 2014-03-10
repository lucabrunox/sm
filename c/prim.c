#include <stdio.h>
#include <string.h>

#include "prim.h"
#include "codegen.h"

void sm_prim_print (SmCodegen* gen) {
	static int closure = -1;
	if (closure >= 0) {
		return;
	}
	
	GET_CODE;
	
	int true_str = -1;
	int true_len = strlen("true")+1;
	int false_str = -1;
	int false_len = strlen("false")+1;
	int eos_str = -1;
	int eos_len = strlen("eos")+1;
	int int_str = -1;
	int int_len = strlen("%llu")+1;
	int chr_str = -1;
	int chr_len = strlen("%c")+1;
	int list_str = -1;
	int list_len = strlen("[%llu]")+1;
	int unk_str = -1;
	const char* unk_fmt = "unknown object type: %llu\n";
	int unk_len = strlen(unk_fmt)+1;

	PUSH_BLOCK(sm_codegen_get_decls_block (gen));
	true_str = sm_code_get_temp (code);
	EMIT_ ("@.const%d = private constant [%d x i8] c\"true\\00\", align 8", true_str, true_len);
	
	false_str = sm_code_get_temp (code);
	EMIT_ ("@.const%d = private constant [%d x i8] c\"false\\00\", align 8", false_str, false_len);

	eos_str = sm_code_get_temp (code);
	EMIT_ ("@.const%d = private constant [%d x i8] c\"eos\\00\", align 8", eos_str, eos_len);

	int_str = sm_code_get_temp (code);
	EMIT_ ("@.const%d = private constant [%d x i8] c\"%%llu\\00\", align 8", int_str, int_len);

	chr_str = sm_code_get_temp (code);
	EMIT_ ("@.const%d = private constant [%d x i8] c\"%%c\\00\", align 8", chr_str, chr_len);

	list_str = sm_code_get_temp (code);
	EMIT_ ("@.const%d = private constant [%d x i8] c\"[%%llu]\\00\", align 8", list_str, list_len);

	unk_str = sm_code_get_temp (code);
	EMIT_ ("@.const%d = private constant [%d x i8] c\"%s\\00\", align 8", unk_str, unk_len, unk_fmt);
	POP_BLOCK;
	
	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("real print func");
	COMMENT("get string");
	int object = SPGET(0, NULL);
	RUNDBG("-> prim print, object=%p\n", object, NULL);
	RUNDBG("sp=%p\n", LOADSP, "i64*");

	COMMENT("get cont");
	int cont = SPGET(1, "%closure*");
	RUNDBG("cont=%p\n", cont, "%closure*");

	int tag = EMIT("and %%tagged %%%d, %llu", object, TAG_MASK);
	SWITCH("i64 %%%d", "label %%unknown", "i64 %llu, label %%bint i64 %llu, label %%bstring i64 %llu, label %%bobject i64 %llu, label %%blist i64 %llu, label %%bchr",
		   tag, TAG_INT, TAG_STR, TAG_OBJ, TAG_LST, TAG_CHR);

	LABEL("bint");
	int ptr = BITCAST("[%d x i8]* @.const%d", "i8*", int_len, int_str);
	int num = EMIT("and %%tagged %%%d, %llu", object, OBJ_MASK);
	RUNDBG("print int=%llu\n", num, NULL);
	CALL ("i32 (i8*, ...)* @printf(i8* %%%d, i64 %%%d)", ptr, num);
	BR ("label %%continue");

	LABEL("bchr");
	ptr = BITCAST("[%d x i8]* @.const%d", "i8*", chr_len, chr_str);
	num = EMIT("and %%tagged %%%d, %llu", object, OBJ_MASK);
	RUNDBG("print chr=%x\n", num, NULL);
	num = EMIT("trunc %%tagged %%%d to i8", num);
	CALL ("i32 (i8*, ...)* @printf(i8* %%%d, i8 %%%d)", ptr, num);
	BR ("label %%continue");

	LABEL("bstring");
	ptr = EMIT("and %%tagged %%%d, %llu", object, OBJ_MASK);
	ptr = TOPTR("i64 %%%d", "i8*", ptr);
	RUNDBG("print string=%p\n", ptr, "i8*");
	CALL ("i32 (i8*, ...)* @printf(i8* %%%d)", ptr);
	BR ("label %%continue");

	LABEL("bobject");
	SWITCH("i64 %%%d", "label %%unknown", "i64 %llu, label %%btrue i64 %llu, label %%bfalse i64 %llu, label %%beos",
		   object, OBJ_TRUE, OBJ_FALSE, OBJ_EOS);

	LABEL("btrue");
	ptr = BITCAST("[%d x i8]* @.const%d", "i8*", true_len, true_str);
	CALL ("i32 (i8*, ...)* @printf(i8* %%%d)", ptr);
	BR ("label %%continue");

	LABEL("bfalse");
	ptr = BITCAST("[%d x i8]* @.const%d", "i8*", false_len, false_str);
	CALL ("i32 (i8*, ...)* @printf(i8* %%%d)", ptr);
	BR ("label %%continue");

	LABEL("beos");
	ptr = BITCAST("[%d x i8]* @.const%d", "i8*", eos_len, eos_str);
	CALL ("i32 (i8*, ...)* @printf(i8* %%%d)", ptr);
	BR ("label %%continue");

	LABEL("blist");
	ptr = BITCAST("[%d x i8]* @.const%d", "i8*", list_len, list_str);
	int list = EMIT("and %%tagged %%%d, %llu", object, OBJ_MASK);
	list = TOPTR("i64 %%%d", "%%list*", list);
	int listsize = GETPTR("%%list* %%%d, i32 0, i32 0", list);
	listsize = LOAD("i64* %%%d", listsize);
	CALL ("i32 (i8*, ...)* @printf(i8* %%%d, i64 %%%d)", ptr, listsize);
	BR ("label %%continue");

	LABEL("unknown");
	ptr = BITCAST("[%d x i8]* @.const%d", "i8*", unk_len, unk_str);
	CALL ("i32 (i8*, ...)* @printf(i8* %%%d, i64 %%%d)", ptr, tag);
	RET("void");

	LABEL("continue");
	COMMENT("put object back in the stack");
	FINSP(1, object, NULL);
	RUNDBG("enter %p\n", cont, "%closure*");
	ENTER(cont);
	sm_codegen_end_closure_func (gen);

	PUSH_BLOCK(sm_codegen_get_decls_block (gen));
	EMIT_ ("@primPrint = global %%closure* null, align 8");
	POP_BLOCK;

	closure = sm_codegen_create_custom_closure (gen, 0, closureid);
	STORE("%%closure* %%%d", "%%closure** @primPrint", closure);
}
