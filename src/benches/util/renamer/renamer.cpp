//===----------- renamer.cpp - Rename Certain Functions pass --------------===//
//
//	 This pass renames certain libc/libm functions so that the reimplemented
//   functions (that can be ILRed and TXed) are called.
//	 
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "Renamer"

#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/Casting.h>
#include <llvm/Analysis/LoopInfo.h>

#include <set>

using namespace llvm;

namespace {

StringRef renameFunction(StringRef fname) {
	// memory intrinsics and libc funcs
	if (fname.startswith("llvm.memcpy.") || fname.equals("memcpy"))
		return "my_memcpy";
	if (fname.startswith("llvm.memmove.") || fname.equals("memmove"))
		return "my_memmove";
	if (fname.startswith("llvm.memset.") || fname.equals("memset"))
		return "my_memset";
	if (fname.equals("memcmp"))
		return "my_memcmp";
	if (fname.equals("memchr"))
		return "my_memchr";
	if (fname.equals("bzero"))
		return "my_bzero";

	// string libc funcs
	if (fname.equals("strcmp"))
		return "my_strcmp";
	if (fname.equals("strncmp"))
		return "my_strncmp";
	if (fname.equals("strcasecmp"))
		return "my_strcasecmp";
	if (fname.equals("strncasecmp"))
		return "my_strncasecmp";
	if (fname.equals("strlen"))
		return "my_strlen";
	if (fname.equals("strcpy"))
		return "my_strcpy";
	if (fname.equals("strncpy"))
		return "my_strncpy";
	if (fname.equals("strcat"))
		return "my_strcat";
	if (fname.equals("strstr"))
		return "my_strstr";
	if (fname.equals("strchr"))
		return "my_strchr";
	if (fname.equals("strrchr"))
		return "my_strrchr";
	if (fname.equals("strspn"))
		return "my_strspn";
	if (fname.equals("strcspn"))
		return "my_strcspn";
	if (fname.equals("strchrnul"))
		return "my_strchrnul";
	if (fname.equals("strpbrk"))
		return "my_strpbrk";

	// ctype libc funcs
	if (fname.equals("toupper"))
		return "my_toupper";
	if (fname.equals("tolower"))
		return "my_tolower";
	if (fname.equals("isdigit"))
		return "my_isdigit";
	if (fname.equals("islower"))
		return "my_islower";
	if (fname.equals("isspace"))
		return "my_isspace";
	if (fname.equals("isupper"))
		return "my_isupper";

	// libm funcs
	if (fname.equals("nan"))
		return "my_nan";
	if (fname.equals("finite"))
		return "my_finite";
	if (fname.equals("exp"))
		return "my_exp";
	if (fname.equals("exp2"))
		return "my_exp2";
	if (fname.equals("scalbn"))
		return "my_scalbn";
	if (fname.equals("scalbnf"))
		return "my_scalbnf";
	if (fname.equals("log"))
		return "my_log";
	if (fname.equals("log10"))
		return "my_log10";
	if (fname.equals("sqrt"))
		return "my_sqrt";
	if (fname.equals("sqrtf"))
		return "my_sqrtf";
	if (fname.equals("fabs"))
		return "my_fabs";
	if (fname.equals("fabsf"))
		return "my_fabsf";
	if (fname.equals("pow"))
		return "my_pow";
	if (fname.equals("powf"))
		return "my_powf";
	if (fname.equals("modf"))
		return "my_modf";
	if (fname.equals("modff"))
		return "my_modff";
	if (fname.equals("modfl"))
		return "my_modfl";
	if (fname.equals("ceil"))
		return "my_ceil";
	if (fname.equals("ceilf"))
		return "my_ceilf";
	if (fname.equals("floor"))
		return "my_floor";
	if (fname.equals("floorf"))
		return "my_floorf";
	if (fname.equals("cbrt"))
		return "my_cbrt";
	if (fname.equals("cbrtf"))
		return "my_cbrtf";
	if (fname.equals("ldexp"))
		return "my_ldexp";
	if (fname.equals("ldexpf"))
		return "my_ldexpf";
	if (fname.equals("frexp"))
		return "my_frexp";
	if (fname.equals("hypot"))
		return "my_hypot";

	return fname;
}

class RenamerPass : public FunctionPass {
	Module* module;

	public:
	static char ID; // Pass identification, replacement for typeid

	RenamerPass(): FunctionPass(ID) { }

	virtual bool doInitialization(Module& M) {
		module = &M;
		return false;
	}

	virtual bool runOnFunction(Function &F) {
		F.getParent();

		for (Function::iterator fi = F.begin(), fe = F.end(); fi != fe; ++fi) {
			BasicBlock* BB = fi;

			for (BasicBlock::iterator bi = BB->begin(); bi != BB->end(); ) {
				Instruction* I = bi;
				bi = std::next(bi);

				if (CallInst* call = dyn_cast<CallInst>(I)) {
					Function* func = call->getCalledFunction();
					if (!func) continue;

					StringRef renamed = renameFunction(func->getName());
					if (renamed.equals(func->getName())) continue;

					Function* renamedfunc = module->getFunction(renamed);
					if (!renamedfunc)
						errs() << "could not find substitute for function: " << func->getName() << "\n";
					assert(renamedfunc && "could not find substitute for function");

//					errs() << "renaming: " << *func << "\n";
//					errs() << "      to: " << *renamedfunc << "\n";					

					IRBuilder<> irBuilder(I);

					std::vector<Value*> argsVec;
					for (unsigned i = 0; i < renamedfunc->arg_size(); i++) {
						Value* arg = call->getArgOperand(i);
						// corner-case: llvm.memset and libc memset differ in 
						// type of second arg -- i8 and i32 respectively;
						// we fix it here
						if (i == 1 && func->getName().startswith("llvm.memset.")) {
							arg = irBuilder.CreateZExt(arg, Type::getInt32Ty(getGlobalContext()));
						}

						argsVec.push_back(arg);
					}
					ArrayRef<Value*> argsRef(argsVec);

					Value* V = irBuilder.CreateCall(renamedfunc, argsRef);
					if (!func->getReturnType()->isVoidTy())
						I->replaceAllUsesWith(V);
					I->eraseFromParent();
				}
			}
		}

		// inform that we always modify a function
		return true;
	}
};

char RenamerPass::ID = 0;
static RegisterPass<RenamerPass> X("rename", "RenamerPass");

}
