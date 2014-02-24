#ifndef SM_LLVM_H
#define SM_LLVM_H

typedef struct {
	void* llvm_module;
} SmJit;

extern "C" {
	
void sm_jit_init (void);
SmJit* sm_jit_compile (const char* name, const char* code);
void* sm_jit_get_function (SmJit* jit, const char* name);

}

#endif