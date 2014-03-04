#include <assert.h>
#include <stdio.h>
#include <glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "compile.h"
#include "codegen.h"
#include "ast.h"
#include "llvm.h"
#include "code.h"
#include "astdumper.h"
#include "scope.h"

#define DEFUNC(n,x) static SmVar n (SmCodegen* gen, x* expr, int prealloc)
#define RETVAL(x,y,z) SmVar _res_var={.x, .y, .z}; return _res_var
#define VISIT(x) call_compile_table (gen, EXPR(x), -1)
#define RUNDBG(f,x,c) sm_codegen_debug(gen, f, x, c)

/* Currently favoring doubles, will change in the future to favor either lists or functions */
#define DBL_qNAN 0x7FF8000000000000ULL
#define TAG_MASK 0x7FFF000000000000ULL
#define OBJ_MASK 0x0000FFFFFFFFFFFFULL
#define TAG_FUN DBL_qNAN|(1ULL << 48)
#define TAG_LST DBL_qNAN|(2ULL << 48)
#define TAG_INT DBL_qNAN|(3ULL << 48)
#define TAG_CHR DBL_qNAN|(4ULL << 48)
#define TAG_STR DBL_qNAN|(5ULL << 48) // constant string
#define TAG_EXC DBL_qNAN|(6ULL << 48) // exception, carries an object

typedef enum {
	TYPE_FUN,
	TYPE_LST,
	TYPE_EOS,
	TYPE_INT,
	TYPE_CHR,
	TYPE_STR,
	TYPE_NIL,
	TYPE_UNK // unknown at compile time
} SmVarType;

typedef struct {
	int id;
	int isthunk;
	SmVarType type;
} SmVar;


static SmVar call_compile_table (SmCodegen* gen, SmExpr* expr, int prealloc);

static long long unsigned int tagmap[] = {
	[TYPE_FUN] = TAG_FUN,
	[TYPE_LST] = TAG_LST,
	[TYPE_EOS] = 0,
	[TYPE_INT] = TAG_INT,
	[TYPE_CHR] = TAG_CHR,
	[TYPE_STR] = TAG_STR,
	[TYPE_NIL] = 0
};

int try_var (SmCodegen* gen, SmVar var, SmVarType type) {
	GET_CODE;
	COMMENT("try %%%d, expect %d", var.id, type);
	
	int object = var.id;
	RUNDBG("try var %p\n", object, "%tagged");
	if (var.type != TYPE_UNK) {
		if (var.type != type) {
			printf ("compile-time expected %d, got %d\n", type, var.type);
			exit(0);
		} else {
			return object;
		}
	} else {
		int tag = EMIT("and %%tagged %%%d, %llu", object, TAG_MASK);
		int faillabel = sm_code_get_label (code);
		int ok = sm_code_get_label (code);
		SWITCH("%%tagged %%%d", "label %%fail%d", "i64 %llu, label %%ok%d", tag, faillabel, tagmap[type], ok);

		static int consttmp = -1;
		static const char* str = "runtime expected %llu, got %llu\n";
		int len = strlen(str)+1;
		if (consttmp < 0) {
			PUSH_BLOCK(sm_codegen_get_decls_block (gen));
			consttmp = sm_code_get_temp (code);
			EMIT_ ("@.const%d = private constant [%d x i8] c\"%s\\00\", align 8", consttmp, len, str);
			POP_BLOCK;
		}
		
		LABEL("fail%d", faillabel);
		int strptr = BITCAST("[%d x i8]* @.const%d", "i8*", len, consttmp);
		CALL ("i32 (i8*, ...)* @printf(i8* %%%d, i64 %llu, i64 %%%d)", strptr, tagmap[type], tag);
		RET("void");

		LABEL("ok%d", ok);
		object = EMIT("and %%tagged %%%d, %llu", object, OBJ_MASK);
		object = EMIT("shl nuw %%tagged %%%d, 3", object);
		if (type == TYPE_STR) {
			object = TOPTR("%%tagged %%%d", "i8*", object);
		} else if (type == TYPE_FUN) {
			object = TOPTR("%%tagged %%%d", "%%closure*", object);
		} else {
			assert(FALSE);
		}
		return object;
	}
}

DEFUNC(compile_member_expr, SmMemberExpr) {
	GET_CODE;
	if (expr->inner) {
		printf("unsupported inner member\n");
		exit(0);
	}

	printf("member %p\n", sm_codegen_get_scope(gen));
	int varid = sm_scope_lookup (sm_codegen_get_scope (gen), expr->name);
	printf("member %p\n", sm_codegen_get_scope(gen));
	if (varid < 0) {
		printf("not in scope %s\n", expr->name);
		exit(0);
	}

	int parent_size = sm_scope_get_size (sm_scope_get_parent (sm_codegen_get_scope (gen)));

	{
		int sp = LOADSP;
		RUNDBG(g_strdup_printf("-> member %s, sp=%%p\n", expr->name), sp, "i64*");
	}
	int obj;
	if (sm_codegen_get_use_temps (gen)) {
		if (varid < parent_size) {
			COMMENT("member %s(%d) from closure", expr->name, varid);
			// 0 = closure param
			int objptr = GETPTR("%%closure* %%0, i32 0, i32 %d, i32 %d", CLOSURE_SCOPE, varid);
			obj = LOAD("%%closure** %%%d", objptr);
			RUNDBG("use temps, closure member %p\n", obj, "%closure*");
		} else {
			// from the stack
			COMMENT("member %s(%d) from stack", expr->name, varid);
			int sp = LOADSP;
			obj = SPGET(sp, varid-parent_size, "%closure*");
			RUNDBG("stack member %p\n", obj, "%closure*");
		}
	} else {
		// 0 = closure param
		int objptr = GETPTR("%%closure* %%0, i32 0, i32 %d, i32 %d", CLOSURE_SCOPE, varid);
		obj = LOAD("%%closure** %%%d", objptr);
		RUNDBG("no temps, closure member %p\n", obj, "%closure*");
	}
	RETVAL(id=obj, isthunk=TRUE, type=TYPE_UNK);
}

DEFUNC(compile_seq_expr, SmSeqExpr) {
	GET_CODE;

	SmFuncExpr* func = (expr->base.parent && expr->base.parent->type == SM_FUNC_EXPR) ? (SmFuncExpr*) expr->base.parent : NULL;

	SmScope* scope = sm_scope_new (sm_codegen_get_scope (gen));
	sm_codegen_set_scope (gen, scope);

	int nparams = func ? func->params->len : 0;
	
	int closureid = sm_codegen_begin_closure_func (gen);
	sm_codegen_set_use_temps (gen, TRUE);
	COMMENT("seq/func closure");
	int sp = LOADSP;
	RUNDBG("-> seq, sp=%p\n", sp, "i64*");
	
	int varid = 0;
	/* assign ids to locals */
	for (int i=0; i < expr->assigns->len; i++) {
		SmAssignExpr* assign = (SmAssignExpr*) expr->assigns->pdata[i];
		GPtrArray* names = assign->names;
		for (int i=0; i < names->len; i++) {
			const char* name = (const char*) names->pdata[i];

			int existing = sm_scope_lookup (scope, name);
			if (existing >= 0) {
				printf("shadowing %s\n", name);
				exit(0);
			}

			sm_scope_set (scope, name, varid++);
		}
	}

	/* assign ids to arguments */
	for (int i=0; i < nparams; i++) {
		const char* name = (const char*) func->params->pdata[i];
		
		int existing = sm_scope_lookup (scope, name);
		if (existing >= 0) {
			printf("shadowing %s\n", name);
			exit(0);
		}

		sm_scope_set (scope, name, varid++);
	}
	
	// make room for locals
	sp = VARSP(sp, -expr->assigns->len);

	/* preallocate closures */
	/* as a big lazy hack, keep track of the number of temporaries necessary to allocate a closure */
	int start_alloc = -1;
	int cur_alloc = 0;
	int temp_diff = 0; // keep track of the number of temporaries necessary to allocate a closure
	for (int i=0; i < expr->assigns->len; i++) {
		SmAssignExpr* assign = (SmAssignExpr*) expr->assigns->pdata[i];
		GPtrArray* names = assign->names;
		if (names->len == 1) {
			const char* name = (const char*) names->pdata[0];
			COMMENT("allocate for %s(%d)", name, i);
			int alloc = sm_codegen_allocate_closure (gen);
			temp_diff = alloc-cur_alloc;
			cur_alloc = alloc;
			if (start_alloc < 0) {
				start_alloc = alloc;
			}
			SPSET(sp, i, alloc, "%closure*");
		} else {
			printf("unsupported pattern match\n");
			exit(0);
		}
	}

	/* visit assignments */
	for (int i=0; i < expr->assigns->len; i++) {
		SmAssignExpr* assign = (SmAssignExpr*) expr->assigns->pdata[i];
		GPtrArray* names = assign->names;
		if (names->len == 1) {
			const char* name = (const char*) names->pdata[0];
			COMMENT("assign for %s(%d)", name, i);
			RUNDBG("assign %p\n", start_alloc, "%closure*");
			call_compile_table (gen, EXPR(assign->value), start_alloc);
			start_alloc += temp_diff;
		} else {
			printf("unsupported pattern match\n");
			exit(0);
		}
	}

	COMMENT("visit seq result");
	SmVar result = VISIT(expr->result);
	COMMENT("pop parameters and locals");
	VARSP(sp, nparams+expr->assigns->len);
	COMMENT("enter result");
	RUNDBG("enter %p\n", result.id, "%closure*");
	ENTER(result.id);
	sm_codegen_end_closure_func (gen);

	sm_codegen_set_scope (gen, sm_scope_get_parent (scope));
	sm_scope_free (scope);

	COMMENT("create seq closure");
	COMMENT("ast: %s", g_strescape (sm_ast_dump(EXPR(expr)), NULL));
	int closure = sm_codegen_create_closure (gen, closureid, prealloc);

	if (!func) {
		RETVAL(id=closure, isthunk=TRUE, type=result.type);
	} else {
		// closure object
		COMMENT("tag closure");
		closure = TOINT("%%closure* %%%d", "%%tagged", closure);
		closure = EMIT("lshr exact %%tagged %%%d, 3", closure);
		closure = EMIT("or %%tagged %%%d, %llu", closure, TAG_FUN);
		RETVAL(id=closure, isthunk=FALSE, type=TYPE_FUN);
	}
}

DEFUNC(compile_func_expr, SmFuncExpr) {
	GET_CODE;
	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("func thunk");
	COMMENT("get cont");
	int sp = LOADSP;
	int cont = SPGET(sp, 0, "%closure*");
	RUNDBG("-> func, sp=%p\n", sp, "i64*");
	
	COMMENT("visit body");
	SmVar result = VISIT(expr->body);
	COMMENT("push func object");
	SPSET(sp, 0, result.id, NULL);
	
	COMMENT("enter cont");
	RUNDBG("enter %p\n", cont, "%closure*");
	ENTER(cont);
	sm_codegen_end_closure_func (gen);
	
	int closure = sm_codegen_create_closure (gen, closureid, prealloc);
	RETVAL(id=closure, isthunk=TRUE, type=TYPE_FUN);
}

DEFUNC(compile_literal, SmLiteral) {
	GET_CODE;
	if (expr->str) {
		// define constant string
		// FIXME: do not create a thunk
		
		PUSH_BLOCK(sm_codegen_get_decls_block (gen));
		int consttmp = sm_code_get_temp (code);
		int len = strlen(expr->str)+1;
		// FIXME:
		EMIT_ ("@.const%d = private constant [%d x i8] c\"%s\\00\", align 8", consttmp, len, expr->str);
		POP_BLOCK;

		// expression code
		int closureid = sm_codegen_begin_closure_func (gen);
		COMMENT("string thunk code for '%s' string", expr->str);
		int sp = LOADSP;
		RUNDBG("-> literal, sp=%p\n", sp, "i64*");
		
		int cont = SPGET(sp, 0, "%closure*");
		int obj = GETPTR("[%d x i8]* @.const%d, i32 0, i32 0", len, consttmp);
		obj = EMIT ("ptrtoint i8* %%%d to %%tagged", obj);
		obj = EMIT ("lshr exact %%tagged %%%d, 3", obj);
		obj = EMIT ("or %%tagged %%%d, %llu", obj, TAG_STR);
		SPSET(sp, 0, obj, NULL);

		RUNDBG("enter %p\n", cont, "%closure*");
		ENTER(cont);
		sm_codegen_end_closure_func (gen);

		// build thunk
		COMMENT("create string thunk");
		int closure = sm_codegen_create_closure (gen, closureid, prealloc);
		RETVAL(id=closure, isthunk=TRUE, type=TYPE_STR);
	} else {
		// TODO: 
		printf("only string literals supported\n");
		exit(0);
	}
}

static int create_real_call_closure (SmCodegen* gen, SmCallExpr* expr) {
	GET_CODE;

	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("real call func");

	int sp = LOADSP;
	COMMENT("get func");
	int func = SPGET(sp, 0, "%tagged");
	RUNDBG("-> real call, sp=%p\n", sp, "i64*");

	SmVar funcvar = { .id=func, .isthunk=FALSE, .type=TYPE_UNK };
	func = try_var (gen, funcvar, TYPE_FUN);
	
	COMMENT("set arguments");
	for (int i=0; i < expr->args->len; i++) {
		COMMENT("visit arg %d", i);
		SmVar arg = VISIT(EXPR(expr->args->pdata[i]));
		SPSET(sp, i-expr->args->len+1, arg.id, "%closure*");
	}
	
	COMMENT("push args onto the stack");
	VARSP(sp, -expr->args->len+1);
	COMMENT("enter real func");
	RUNDBG("enter %p\n", func, "%closure*");
	ENTER(func);

	sm_codegen_end_closure_func (gen);

	COMMENT("create real call closure");
	int closure = sm_codegen_create_closure (gen, closureid, -1);
	return closure;
}

DEFUNC(compile_call_expr, SmCallExpr) {
	GET_CODE;

	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("call thunk func");
	int sp = LOADSP;
	RUNDBG("-> call, sp=%p\n", sp, "i64*");
	
	COMMENT("visit func");
	SmVar func = VISIT(expr->func);

	int realfunc = create_real_call_closure (gen, expr);
	FINSP(sp, -1, realfunc, "%closure*");
	
	COMMENT("force func");
	RUNDBG("enter %p\n", func.id, "%closure*");
	ENTER(func.id);
	
	sm_codegen_end_closure_func (gen);
	
	// build thunk
	COMMENT("create call thunk");
	int closure = sm_codegen_create_closure (gen, closureid, prealloc);
	RETVAL(id=closure, isthunk=TRUE, type=TYPE_UNK);
}

#define CAST(x) (SmVar (*)(SmCodegen*, SmExpr*, int prealloc))(x)
SmVar (*compile_table[])(SmCodegen*, SmExpr*, int prealloc) = {
	[SM_MEMBER_EXPR] = CAST(compile_member_expr),
	[SM_SEQ_EXPR] = CAST(compile_seq_expr),
	[SM_LITERAL] = CAST(compile_literal),
	[SM_FUNC_EXPR] = CAST(compile_func_expr),
	[SM_CALL_EXPR] = CAST(compile_call_expr)
};

static SmVar call_compile_table (SmCodegen* gen, SmExpr* expr, int prealloc) {
	return compile_table[expr->type](gen, expr, prealloc);
}

static int create_nop_closure (SmCodegen* gen) {
	GET_CODE;
	int nopid = sm_codegen_begin_closure_func (gen);
	
	COMMENT("nop func"); // discards one object from the stack
	int sp = LOADSP;
	RUNDBG("nop, sp=%d\n", sp, "i64*");
	VARSP(sp, 1);
	// end of the program
	RET("void");

	sm_codegen_end_closure_func (gen);
	COMMENT("nop closure");
	
	int nopclo = sm_codegen_create_closure (gen, nopid, -1);
	return nopclo;
}

static int create_prim_print (SmCodegen* gen) {
	GET_CODE;
	int directid = sm_codegen_begin_closure_func (gen);
	COMMENT("real print func");
	int sp = LOADSP;
	COMMENT("get string");
	int str = SPGET(sp, 0, NULL);
	RUNDBG("-> real print, string object=%p\n", str, "i64");
	RUNDBG("sp=%p\n", sp, "i64*");

	COMMENT("get continuation");
	int cont = SPGET(sp, 1, "%closure*");
	RUNDBG("cont=%p\n", cont, "%closure*");

	SmVar var = { .id=str, .isthunk=FALSE, .type=TYPE_UNK };
	str = try_var (gen, var, TYPE_STR);
	CALL ("i32 (i8*, ...)* @printf(i8* %%%d)", str);

	COMMENT("put string back in the stack");
	FINSP(sp, 1, str, "i8*");
	RUNDBG("enter %p", cont, "%closure*");
	ENTER(cont);
	sm_codegen_end_closure_func (gen);

	int direct = sm_codegen_create_closure (gen, directid, -1);
	return direct;
}

static int create_print_closure (SmCodegen* gen) {
	GET_CODE;
	
	int printid = sm_codegen_begin_closure_func (gen);
	COMMENT("print closure func");
	COMMENT("create direct closure");

	int sp = LOADSP;
	COMMENT("get string thunk");
	int str = SPGET(sp, 0, "%closure*");
	RUNDBG("-> print closure, sp=%p\n", sp, "i64*");

	COMMENT("push direct print closure");
	int direct = create_prim_print (gen);
	FINSP(sp, 0, direct, "%closure*");

	COMMENT("enter string");
	RUNDBG("enter string %p\n", str, "%closure*");
	ENTER(str);
	sm_codegen_end_closure_func (gen);

	COMMENT("create print closure");
	int printclo = sm_codegen_create_closure (gen, printid, -1);
	return printclo;
}

SmJit* sm_compile (SmCodegenOpts opts, const char* name, SmExpr* expr) {
	static int initialized = 0;
	if (!initialized) {
		initialized = 1;
		sm_jit_init ();
	}

	SmCodegen* gen = sm_codegen_new (opts);
	GET_CODE;
	
	PUSH_BLOCK(sm_codegen_get_decls_block (gen));
	DECLARE ("i32 @printf(i8*, ...)");
	DECLARE ("i8* @aligned_alloc(i32, i32)");
	DECLARE ("void @llvm.memcpy.p0i8.p0i8.i32(i8*, i8*, i32, i32, i1)");
	DECLARE ("void @llvm.donothing() nounwind readnone");
	DECLARE ("void @llvm.debugtrap() nounwind");
	EMIT_ ("%%tagged = type i64");
	EMIT_ ("%%closurefunc = type void (%%closure*)*");
	EMIT_ ("@stack = global i64* null, align 8");
	EMIT_ ("@sp = global i64* null, align 8");
	DEFINE_STRUCT ("closure", "%%closurefunc, %%tagged, [0 x %%closure*]"); // func, cached object, scope
	POP_BLOCK;

	PUSH_NEW_BLOCK;
	BEGIN_FUNC_ATTRS("%%tagged", "thunk_cache", "%%closure*", "readonly");
	int thunk = sm_code_get_temp (code); // first param
	LABEL("entry");
	int objptr = GETPTR("%%closure* %%%d, i32 0, i32 %d", thunk, CLOSURE_CACHE);
	int obj = LOAD("%%tagged* %%%d", objptr);
	RET("%%tagged %%%d", obj);
	END_FUNC;
	POP_BLOCK;
	PUSH_NEW_BLOCK;
	BEGIN_FUNC("void", "main", "");
	COMMENT("main");
	LABEL("entry");

	COMMENT("alloc stack");
	int stack = CALL("i8* @aligned_alloc(i32 8, i32 %d)", (int)(4096*sizeof(void*)));
	stack = BITCAST("i8* %%%d", "i64*", stack);
	STORE("i64* %%%d", "i64** @stack", stack);
	
	STORE("i64* %%%d", "i64** @sp", stack);
	int sp = LOADSP;
	sp = VARSP(sp, 4096-8);
	RUNDBG("bottom sp=%p\n", sp, "i64*");

	int nopclo = create_nop_closure (gen);
	int printclo = create_print_closure (gen);
	
	SPSET(sp, 0, nopclo, "%closure*");

	COMMENT("visit root expression");
	SmVar var = VISIT(expr);
	COMMENT("push root expression");
	sp = FINSP(sp, -1, var.id, "%closure*");
	RUNDBG("root expr %p\n", var.id, "%closure*");
	RUNDBG("sp=%p\n", sp, "i64*");

	COMMENT("enter print");
	ENTER(printclo);
	/* RET("void"); */
	END_FUNC;
	POP_BLOCK;

	char* unit = sm_code_link (code);
	puts(unit);
	sm_code_unref (code);
	
	SmJit* mod = sm_jit_compile ("<stdin>", unit);
	/* free (unit); */

	return mod;
}

void sm_run (SmJit* mod) {
	void (*entrypoint)() = (void (*)()) sm_jit_get_function (mod, FUNC("main"));
	if (!entrypoint) {
		return;
	}
	entrypoint();
}

