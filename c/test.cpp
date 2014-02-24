#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/FormattedStream.h>

using namespace llvm;

int main() {
	LLVMContext &context = getGlobalContext();

	SMDiagnostic err;
	StringRef s("asd");
    Module *mod = ParseIR(MemoryBuffer::getMemBuffer(s), err, context);

    if (!mod) {
        err.print("asd", errs());
        return 1;
    }
	
	mod->dump( );
}