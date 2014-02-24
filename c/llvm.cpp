#include <stdlib.h>
#include <iostream>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/Support/raw_ostream.h>
#include "llvm.h"

using namespace llvm;

void sm_jit_init (void) {
	InitializeNativeTarget();
    llvm_start_multithreaded();
}

SmJit* sm_jit_compile (const char* name, const char* code) {
	LLVMContext &context = getGlobalContext();

	SMDiagnostic err;
    Module *mod = ParseIR(MemoryBuffer::getMemBuffer(code), err, context);

    if (!mod) {
        err.print(name, errs());
        return NULL;
    }

	return (SmJit*) mod;
}

void* sm_jit_get_function (SmJit* jit, const char* name) {
	Module* mod = (Module*) jit;

	std::string error;
	ExecutionEngine* engine = ExecutionEngine::createJIT(mod, &error);
    if (!engine) {
		std::cout << error << std::endl;
        return NULL;
    }

	Function *entry_point = mod->getFunction("_smc_main");
	return engine->getPointerToFunction(entry_point);
}
