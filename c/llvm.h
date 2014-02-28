#ifndef SM_LLVM_H
#define SM_LLVM_H

typedef struct _SmJit SmJit; 

#ifdef __cplusplus
extern "C" {
#endif
	
void sm_jit_init (void);
SmJit* sm_jit_compile (const char* name, const char* code);
void* sm_jit_get_function (SmJit* jit, const char* name);
void sm_jit_dump_ir (SmJit* jit);
void sm_jit_dump_asm (SmJit* jit);

#ifdef __cplusplus
}
#endif

#endif