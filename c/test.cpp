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

using namespace llvm;

int main() {
	InitializeNativeTarget();
    /* llvm_start_multithreaded(); */
	
	LLVMContext &context = getGlobalContext();

	SMDiagnostic err;
	const char* s = "\n\
; ModuleID = 'hello.c'\n\
target datalayout = \"e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128\"\n\
target triple = \"x86_64-pc-linux-gnu\"\n\
\n\
@.str = private unnamed_addr constant [7 x i8] c\"hello\\0A\\00\", align 1\n\
\n\
; Function Attrs: nounwind uwtable\n\
define void @_smc_main() #0 {\n\
  %1 = call i32 (i8*, ...)* @printf(i8* getelementptr inbounds ([7 x i8]* @.str, i32 0, i32 0))\n\
  ret void\n\
}\n\
\n\
declare i32 @printf(i8*, ...) #1\n\
\n\
attributes #0 = { nounwind uwtable \"less-precise-fpmad\"=\"false\" \"no-frame-pointer-elim\"=\"true\" \"no-frame-pointer-elim-non-leaf\"=\"true\" \"no-infs-fp-math\"=\"false\" \"no-nans-fp-math\"=\"false\" \"unsafe-fp-math\"=\"false\" \"use-soft-float\"=\"false\" }\n\
attributes #1 = { \"less-precise-fpmad\"=\"false\" \"no-frame-pointer-elim\"=\"true\" \"no-frame-pointer-elim-non-leaf\"=\"true\" \"no-infs-fp-math\"=\"false\" \"no-nans-fp-math\"=\"false\" \"unsafe-fp-math\"=\"false\" \"use-soft-float\"=\"false\" }\n\
				";
    Module *jit = ParseIR(MemoryBuffer::getMemBufferCopy(s), err, context);
    /* Module *jit = ParseIRFile("hello.s", err, context); */

    if (!jit) {
        err.print("jit", errs());
        return 1;
    }

	Function *entry_point = jit->getFunction("_smc_main");

	std::string error;
	ExecutionEngine* engine = ExecutionEngine::createJIT(jit, &error);
    if (!engine) {
		std::cout << error << std::endl;
        return 1;
    }

	void (*fp)() = (void (*)())engine->getPointerToFunction(entry_point);
	fp();
}