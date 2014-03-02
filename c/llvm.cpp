#include <stdlib.h>
#include <iostream>
#include <llvm/Config/llvm-config.h>

#if LLVM_VERSION_MINOR == 2
#include <llvm/LLVMContext.h>
#include <llvm/Support/IRReader.h>
#include <llvm/Module.h>
#else
#include <llvm/IRReader/IRReader.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#endif

#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Assembly/AssemblyAnnotationWriter.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/PassManager.h>
#include <llvm/Assembly/PrintModulePass.h>

#include "llvm.h"

using namespace llvm;

struct _SmJit {
	void* llvm_module;
	void* llvm_engine;
};

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

	PassManager pm;
	
	// Set up the optimizer pipeline.  Start with registering info about how the
	// target lays out data structures.
	/* pm.add(new DataLayout(*engine->getDataLayout())); */
	// Provide basic AliasAnalysis support for GVN.
	pm.add(createBasicAliasAnalysisPass());
	// Do simple "peephole" optimizations and bit-twiddling optzns.
	pm.add(createInstructionCombiningPass());
	// Reassociate expressions.
	pm.add(createReassociatePass());
	// Eliminate Common SubExpressions.
	pm.add(createGVNPass());
	// Simplify the control flow graph (deleting unreachable blocks, etc).
	pm.add(createCFGSimplificationPass());
	
	pm.run (*mod);
	
	return (SmJit*) mod;
}

void* sm_jit_get_function (SmJit* jit, const char* name) {
	Module* mod = (Module*) jit;

	std::string error;
	ExecutionEngine *engine = ExecutionEngine::createJIT(mod, &error);
	if (!engine) {
		std::cout << error << std::endl;
		return NULL;
	}

	Function *entry_point = mod->getFunction("_smc_main");
	
	return engine->getPointerToFunction(entry_point);
}

void sm_jit_dump_ir (SmJit* jit) {
	Module* mod = (Module*) jit;
	mod->dump();
}

void sm_jit_dump_asm (SmJit* jit) {
	Module* mod = (Module*) jit;
	EngineBuilder builder (mod);
	TargetMachine *tm = builder.selectTarget();
	tm->Options.PrintMachineCode = 1;
	ExecutionEngine *engine = builder.create(tm);

	Function *entry_point = mod->getFunction("_smc_main");
	engine->getPointerToFunction(entry_point);
}
