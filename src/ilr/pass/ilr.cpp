//===---------- ilr.cpp - ILR-harden pass ---------------------------------===//
//
//	 This pass duplicates all instructions (instruction-level-replication) and
//   inserts checks at synchronization points: stores, branches, calls, etc.
//
//   TODO: previously was called "SWIFT" since the idea is taken from:
//           http://liberty.cs.princeton.edu/Publications/cgo3_swift.pdf
//         change to ILR eveywhere at some point...
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "ILR"

// --- uncomment if you'd like to activate some optimization ---

// remove swift-checks right after swift-moves
#define SWIFT_OPTIMIZE_IMMEDIATE_CHECKS

// distinguish between different types of loads/stores (atomic vs regular);
// if not defined, every mem access is conservatively treated as atomic
#define SWIFT_OPTIMIZE_SHARED_MEMORY_ACCESSES

// optimization to explicitly check local loop vars before Tx-end
// NOTE: assumes a later Trans pass, does not alter behaviour w/o it
#define SWIFT_INSERT_CHECKS_ON_LOOP_HEADERS

// optimization to insert shadow basic blocks for simple control flow
// on the status register; if not defined, branch condition is checked
// via usual swift-check
#define SWIFT_SIMPLE_CONTROL_FLOW

#include <llvm/Pass.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/Casting.h>
#include <llvm/IR/Dominators.h>
#include <llvm/ADT/DepthFirstIterator.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/LoopIterator.h>
#include <llvm/Support/CommandLine.h>

#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <utility>
#include <tr1/memory>
#include <tr1/tuple>

using namespace llvm;

namespace {

static const std::string CLONE_SUFFIX(".swift");
typedef std::map<const Type*, Function*> Type2FunctionMap;

typedef std::map<std::pair<Type*, uint64_t>, GlobalVariable*> GlobalConstMap;
GlobalConstMap globalconsts;

class SwiftHelpers {
	void addFunction(Module& M, Type2FunctionMap& map, const std::string& name, const Type* type) {
		Function *F = M.getFunction(name);
		if (!F)
			errs() << "Definition of function <" << name << "> not found\n";
		assert(F && "swift function not found (requires linked swift-interface");

		bool isnew;
		Type2FunctionMap::iterator it;
		std::tr1::tie (it, isnew) = map.insert(std::make_pair(type, F));
		assert (isnew && "type collision: function for this type exists already");

		helpers.insert(F);
	}

	public:
	Type2FunctionMap movers;
	Type2FunctionMap checkers;
	Function* detectedfunc;
	std::set<Function*> helpers;
	Module* module;

	SwiftHelpers(Module& M) {
		module = &M;
		addFunction(M, checkers, "SWIFT$check_i8",     Type::getInt8Ty(getGlobalContext()));
		addFunction(M, checkers, "SWIFT$check_i16",    Type::getInt16Ty(getGlobalContext()));
		addFunction(M, checkers, "SWIFT$check_i32",    Type::getInt32Ty(getGlobalContext()));
		addFunction(M, checkers, "SWIFT$check_i64",    Type::getInt64Ty(getGlobalContext()));
		addFunction(M, checkers, "SWIFT$check_ptr",    PointerType::getUnqual(Type::getInt8Ty(getGlobalContext())));
		addFunction(M, checkers, "SWIFT$check_double", Type::getDoubleTy(getGlobalContext()));
		addFunction(M, checkers, "SWIFT$check_float",  Type::getFloatTy(getGlobalContext()));
		addFunction(M, checkers, "SWIFT$check_dq",     VectorType::get(Type::getInt64Ty(getGlobalContext()),  2));
		addFunction(M, checkers, "SWIFT$check_pd",     VectorType::get(Type::getDoubleTy(getGlobalContext()), 2));
		addFunction(M, checkers, "SWIFT$check_ps",     VectorType::get(Type::getFloatTy(getGlobalContext()),  4));

		addFunction(M, movers, "SWIFT$move_i8",     Type::getInt8Ty(getGlobalContext()));
		addFunction(M, movers, "SWIFT$move_i16",    Type::getInt16Ty(getGlobalContext()));
		addFunction(M, movers, "SWIFT$move_i32",    Type::getInt32Ty(getGlobalContext()));
		addFunction(M, movers, "SWIFT$move_i64",    Type::getInt64Ty(getGlobalContext()));
		addFunction(M, movers, "SWIFT$move_ptr",    PointerType::getUnqual(Type::getInt8Ty(getGlobalContext())));
		addFunction(M, movers, "SWIFT$move_double", Type::getDoubleTy(getGlobalContext()));
		addFunction(M, movers, "SWIFT$move_float",  Type::getFloatTy(getGlobalContext()));
		addFunction(M, movers, "SWIFT$move_dq",     VectorType::get(Type::getInt64Ty(getGlobalContext()),  2));
		addFunction(M, movers, "SWIFT$move_pd",     VectorType::get(Type::getDoubleTy(getGlobalContext()), 2));
		addFunction(M, movers, "SWIFT$move_ps",     VectorType::get(Type::getFloatTy(getGlobalContext()),  4));

		detectedfunc = M.getFunction("SWIFT$detected");
		assert(detectedfunc && "swift function <detected> not found (requires linked swift-interface");
	}

	// some functions-llvm intrinsics can be treated by swift as
	// normal instructions w/o any need to check arguments/copy return value
	bool isDuplicatedFunc(Function* F) {
		if (!F)
			return false;
		StringRef fname = F->getName();

		if (
			// floating point arithmetic
			fname.startswith("llvm.sqrt.") ||
			fname.startswith("llvm.powi.") ||
			fname.startswith("llvm.sin.") ||
			fname.startswith("llvm.cos.") ||
			fname.startswith("llvm.pow.") ||
			fname.startswith("llvm.exp.") ||
			fname.startswith("llvm.exp2.") ||
			fname.startswith("llvm.log.") ||
			fname.startswith("llvm.log10.") ||
			fname.startswith("llvm.log2.") ||
			fname.startswith("llvm.fma.") ||
			fname.startswith("llvm.fabs.") ||
			fname.startswith("llvm.minnum.") ||
			fname.startswith("llvm.maxnum.") ||
			fname.startswith("llvm.copysign.") ||
			fname.startswith("llvm.floor.") ||
			fname.startswith("llvm.ceil.") ||
			fname.startswith("llvm.trunc.") ||
			fname.startswith("llvm.rint.") ||
			fname.startswith("llvm.nearbyint.") ||
			fname.startswith("llvm.round.") ||
			// bitwise
			fname.startswith("llvm.bswap.") ||
			fname.startswith("llvm.ctpop.") ||
			fname.startswith("llvm.ctlz.") ||
			fname.startswith("llvm.cttz.") ||
			// overflow arithmetic
			fname.startswith("llvm.sadd.with.overflow") ||
			fname.startswith("llvm.uadd.with.overflow") ||
			fname.startswith("llvm.ssub.with.overflow") ||
			fname.startswith("llvm.usub.with.overflow") ||
			fname.startswith("llvm.smul.with.overflow") ||
			fname.startswith("llvm.umul.with.overflow") ||
			// misc
			fname.startswith("llvm.canonicalize.") ||
			fname.startswith("llvm.fmuladd.") ||
			fname.startswith("llvm.convert.") ||

			fname.startswith("__dummy__"))
			return true;

		return false;
	}

	bool isIgnoredFunc(Function* F) {
		static std::set<std::string> ignored {
			// Transactifier functions
			"tx_cond_start",
			"tx_start",
			"tx_end",
			"tx_abort",
			"tx_increment",
			"tx_pthread_mutex_lock",
			"tx_pthread_mutex_unlock",

			// Intel TSX intrinsics
			"llvm.x86.xtest",
			"llvm.x86.xbegin",
			"llvm.x86.xend",
			"llvm.x86.xabort",

			// LLVM intrinsics -- most of them must be ignored
			"llvm.dbg.value",
			"llvm.dbg.declare",

			"llvm.returnaddress",
			"llvm.frameaddress",
			"llvm.frameescape",
			"llvm.framerecover",

			"llvm.read_register.i32",
			"llvm.read_register.i64",
			"llvm.write_register.i32",
			"llvm.write_register.i64",

//			"llvm.stacksave",
//			"llvm.stackrestore",
			"llvm.prefetch",
			"llvm.pcmarker",
			"llvm.readcyclecounter",
			"llvm.clear_cache",
			"llvm.instrprof_increment",

			"llvm.lifetime.start",
			"llvm.lifetime.end",
			"llvm.invariant.start",
			"llvm.invariant.end",

			"llvm.var.annotation",
			"llvm.ptr.annotation.p0",
			"llvm.annotation.i8",
			"llvm.annotation.i16",
			"llvm.annotation.i32",
			"llvm.annotation.i64",
			"llvm.annotation.i256",

			"llvm.stackprotector",
			"llvm.stackprotectorcheck",

			"llvm.llvm.objectsize.i32",
			"llvm.llvm.objectsize.i64",
			"llvm.expect.i1",
			"llvm.expect.i32",
			"llvm.expect.i64",

			"llvm.assume",
			"llvm.bitset.test",

			"__dummy__"
		};

		if (!F) {
			// conservative about function pointers
			return false;
		}

		if (helpers.count(F) || ignored.count(F->getName())) {
			// function is Swift helper function or in a list of ignored
			return true;
		}
		return false;
	}

};

class ValueShadowMap{
	typedef std::map<Value*, Value*> ValueShadowMapType;
	ValueShadowMapType vsm;

	public:
	ValueShadowMap(): vsm() {}

	void add(Value* v, Value* shadow) {
		bool isnew;
		ValueShadowMapType::iterator it;

		std::tr1::tie(it, isnew) = vsm.insert(std::make_pair(v, shadow));
		assert (isnew && "value already has a shadow");
	}

	Value* getShadow(Value *v, Value *inst_debug) {
		// no checks for constants, BBs (labels), function declarations, inline asm and metadata
		if (isa<Constant>(v) || isa<BasicBlock>(v) || isa<Function>(v) ||
			isa<InlineAsm>(v) || isa<MetadataAsValue>(v) ||
			isa<InvokeInst>(v) || isa<LandingPadInst>(v))
			return nullptr;

		ValueShadowMapType::iterator it = vsm.find(v);

		if (it == vsm.end()){
			errs() << "Value '" << *v << "' has no shadow (for Instr '" << *inst_debug << "')\n";
		}
		assert(it != vsm.end() && "value has no shadow");

		return it->second;
	}

	bool hasShadow(Value *v) {
		return vsm.end() != vsm.find(v);
	}
};

class SwiftTransformer {
	SwiftHelpers* swiftHelpers;
	ValueShadowMap shadows;
	std::vector<PHINode*> phis;
	std::vector<BranchInst*> brs;
	BasicBlock* detectedBB = nullptr;

	unsigned long next_id = 0;

	Value* castToSupportedType(IRBuilder<>& irBuilder, Value* v) {
		Type *Ty = v->getType();

		switch (Ty->getTypeID()) {
			case Type::IntegerTyID: {
				Type* TargetType = Type::getInt64Ty(getGlobalContext());
				if (Ty->getPrimitiveSizeInBits() <= 8)
					TargetType = Type::getInt8Ty(getGlobalContext());
				else if (Ty->getPrimitiveSizeInBits() <= 16)
					TargetType = Type::getInt16Ty(getGlobalContext());
				else if (Ty->getPrimitiveSizeInBits() <= 32)
					TargetType = Type::getInt32Ty(getGlobalContext());

				if (Ty->getPrimitiveSizeInBits() < TargetType->getPrimitiveSizeInBits())
					v = irBuilder.CreateZExt(v, TargetType, "swift.intcast");
				}
				break;

			case Type::PointerTyID: {
				Type* TyPtrToI8 = PointerType::getUnqual(Type::getInt8Ty(getGlobalContext()));
				if (Ty != TyPtrToI8)
					v = irBuilder.CreateBitCast(v, TyPtrToI8, "swift.ptrcast");
				}
				break;

			case Type::DoubleTyID:
			case Type::FloatTyID:
				break;

			case Type::HalfTyID: {
					// TODO: this can change the precision, ignore?
					v = irBuilder.CreateFPExt(v, Type::getFloatTy(getGlobalContext()), "swift.halfcast");
				}
				break;

			case Type::X86_FP80TyID: {
					// TODO: this can change the precision, ignore?
					v = irBuilder.CreateFPTrunc(v, Type::getDoubleTy(getGlobalContext()), "swift.fp80cast");
				}
				break;

			case Type::VectorTyID: {
				Type* TyVecInt64  = VectorType::get(Type::getInt64Ty(getGlobalContext()), 2);
				Type* TyVecInt32  = VectorType::get(Type::getInt32Ty(getGlobalContext()), 4);
				Type* TyVecInt16  = VectorType::get(Type::getInt16Ty(getGlobalContext()), 8);
				Type* TyVecInt8  = VectorType::get(Type::getInt8Ty(getGlobalContext()), 16);
				Type* TyVecFloat  = VectorType::get(Type::getFloatTy(getGlobalContext()), 4);
				Type* TyVecDouble = VectorType::get(Type::getDoubleTy(getGlobalContext()), 2);

				Type* VecTy = Ty->getVectorElementType();
				if (VecTy->isIntegerTy() && Ty != TyVecInt64) {
					unsigned NumEl = cast<VectorType>(Ty)->getNumElements();
					if (NumEl == 2) {
						// it's <2 x iX> but not <2 x i64>, zero-extend
						v = irBuilder.CreateZExt(v, TyVecInt64);
					} else if (NumEl == 4 && Ty != TyVecInt32) {
						// it's <4 x iX> but not <4 x i32>, zero-extend
						v = irBuilder.CreateZExt(v, TyVecInt32);
					} else if (NumEl == 8 && Ty != TyVecInt16) {
						// it's <8 x iX> but not <8 x i16>, zero-extend
						v = irBuilder.CreateZExt(v, TyVecInt16);
					} else if (NumEl == 16 && Ty != TyVecInt8) {
						// it's <16 x iX> but not <16 x i8>, zero-extend
						v = irBuilder.CreateZExt(v, TyVecInt8);
					}
					// now it's <4 x i32> or <8 x i16> or <16 x i8> or <2 x i64>
					v = irBuilder.CreateBitCast(v, TyVecInt64, "swift.intveccast");
				} else
				if (VecTy->isFloatTy()   && Ty != TyVecFloat) {
					// TODO: <2 x float> FP-extended to <2 x double>, can change
					//        results of computation
					v = irBuilder.CreateFPExt(v, TyVecDouble, "swift.floatveccast");
				} else
				if (VecTy->isPointerTy()) {
					// assuming pointers are always 64-bit wide and coming in pairs
					assert(cast<VectorType>(Ty)->getNumElements() == 2 && "we support only <2 x iX*>");
					v = irBuilder.CreatePtrToInt(v, TyVecInt64, "swift.ptrveccast");
				}
				}
				break;

			default:
			    errs() << "don't know how to handle type " << *Ty << "\n";
				assert(!"cannot create check for this type");
				break;
		}
		return v;
	}

	void createCheckerCall(IRBuilder<>& irBuilder, Value* v1, Value* v2, bool movev2) {
		if (v1->getType() != v2->getType()) {
			errs() << "types of v1 (" << *v1 << ") and v2 (" << *v2 << ") must be the same";
		}
		assert(v1->getType() == v2->getType() && "types of v1 and v2 must be the same");

		if (StructType *st = dyn_cast<StructType>(v1->getType())) {
			// complex struct type, need to compare each struct's field
			for (unsigned i = 0; i < st->getNumElements(); ++i) {
				Value *f1 = irBuilder.CreateExtractValue(v1, ArrayRef<unsigned>(i));
				Value *f2 = irBuilder.CreateExtractValue(v2, ArrayRef<unsigned>(i));
				createCheckerCall(irBuilder, f1, f2, movev2);
			}
			return;
		}

#ifdef SWIFT_OPTIMIZE_IMMEDIATE_CHECKS
		if (CallInst *call = dyn_cast<CallInst>(v2))
			if (call->getCalledFunction() && swiftHelpers->helpers.count(call->getCalledFunction())) {
				// requested check for v2 (shadow) is immediately after v2's swift-move
				// -> no need to make a check because: (1) it's not a result of execution and
				//    (2) we assume that a very short time passed before move and check
				return;
			}
#endif

		v1 = castToSupportedType(irBuilder, v1);
		v2 = castToSupportedType(irBuilder, v2);

		Type2FunctionMap::iterator it = swiftHelpers->checkers.find(v1->getType());
		if (it == swiftHelpers->checkers.end())
			errs() << "don't know how to handle type " << *(v1->getType()) << "\n";
		assert (it != swiftHelpers->checkers.end() && "no checker function found for specified type");

		// store checks do not need to move v2 (because they do volatile reload),
		// so for them movev2 = false; but other checks need a swift-move
		// such that codegen does not move the check for optimization
		if (movev2) {
			Type2FunctionMap::iterator it2 = swiftHelpers->movers.find(v2->getType());
			if (it2 == swiftHelpers->movers.end())
				errs() << "don't know how to handle type " << *(v2->getType()) << "\n";
			assert (it2 != swiftHelpers->movers.end() && "no mover function found for specified type");

			v2 = irBuilder.CreateCall(it2->second, v2, "swift.movetocheck");
		}

		Value* id = ConstantInt::get(Type::getInt32Ty(getGlobalContext()), next_id++);

		std::vector<Value*> argsVec;
		argsVec.push_back(v1);
		argsVec.push_back(v2);
		argsVec.push_back(id);
		ArrayRef<Value*> argsRef(argsVec);

		irBuilder.CreateCall(it->second, argsRef);
	}

	Instruction* createMoveCall(IRBuilder<>& irBuilder, Value* v) {
		if (StructType *st = dyn_cast<StructType>(v->getType())) {
			// complex struct type, need to move each struct's field
			Value *newstruct = UndefValue::get(st);

			for (unsigned i = 0; i < st->getNumElements(); ++i) {
				Value *f = irBuilder.CreateExtractValue(v, ArrayRef<unsigned>(i));
				Value *shadow = createMoveCall(irBuilder, f);
				newstruct = irBuilder.CreateInsertValue(newstruct, shadow, ArrayRef<unsigned>(i));
			}
			newstruct->setName(v->getName() + CLONE_SUFFIX);
			return cast<Instruction>(newstruct);
		}

		Type* origType = v->getType();
		v = castToSupportedType(irBuilder, v);

		Type2FunctionMap::iterator it = swiftHelpers->movers.find(v->getType());
		if (it == swiftHelpers->movers.end())
			errs() << "don't know how to handle type " << *(v->getType()) << "\n";
		assert (it != swiftHelpers->movers.end() && "no mover function found for specified type");

		Instruction *move = irBuilder.CreateCall(it->second, v, v->getName() + CLONE_SUFFIX);

		if (v->getType() != origType) {
			// we could cast from original ptr type to i8*, need to cast back
			if (v->getType()->isPointerTy())
					move = cast<Instruction>(irBuilder.CreateBitCast(move, origType, v->getName() + CLONE_SUFFIX));

			// we could zero-extend original int to bigger int, need to trunc back
			if (v->getType()->isIntegerTy())
					move = cast<Instruction>(irBuilder.CreateTrunc(move, origType, v->getName() + CLONE_SUFFIX));

			// we could extend from half-float to float, need to trunc back
			if (v->getType()->isFloatTy() && origType->isHalfTy())
					move = cast<Instruction>(irBuilder.CreateFPTrunc(move, origType, v->getName() + CLONE_SUFFIX));

			// we could trunc from fp80 to double, need to extend back
			if (v->getType()->isDoubleTy() && origType->isX86_FP80Ty())
					move = cast<Instruction>(irBuilder.CreateFPExt(move, origType, v->getName() + CLONE_SUFFIX));

			// we could have a vector of non-64-bit integers, need to cast back
			if (v->getType()->isVectorTy() && origType->getVectorElementType()->isIntegerTy()) {
					Type* TyVecInt64  = VectorType::get(Type::getInt64Ty(getGlobalContext()), 2);
					Type* TyVecInt32  = VectorType::get(Type::getInt32Ty(getGlobalContext()), 4);
					Type* TyVecInt16  = VectorType::get(Type::getInt16Ty(getGlobalContext()), 8);
					Type* TyVecInt8  = VectorType::get(Type::getInt8Ty(getGlobalContext()), 16);

					unsigned NumEl = cast<VectorType>(origType)->getNumElements();
					switch (NumEl) {
						case 2: move = cast<Instruction>(irBuilder.CreateBitCast(move, TyVecInt64)); break;
						case 4: move = cast<Instruction>(irBuilder.CreateBitCast(move, TyVecInt32)); break;
						case 8: move = cast<Instruction>(irBuilder.CreateBitCast(move, TyVecInt16)); break;
						case 16: move = cast<Instruction>(irBuilder.CreateBitCast(move, TyVecInt8)); break;
					}
					if ((NumEl == 2 && origType != TyVecInt64) ||
						(NumEl == 4 && origType != TyVecInt32) ||
						(NumEl == 8 && origType != TyVecInt16) ||
						(NumEl == 16 && origType != TyVecInt8)) {
						move = cast<Instruction>(irBuilder.CreateTrunc(move, origType));
					}
					move->setName(v->getName() + CLONE_SUFFIX);
			}

			// we could have a <2 x float> FP-extended to <2 x double>, need to trunc back
			if (v->getType()->isVectorTy() && origType->getVectorElementType()->isFloatTy()) {
					move = cast<Instruction>(irBuilder.CreateFPTrunc(move, origType, v->getName() + CLONE_SUFFIX));
			}

			// we could have a <2 x iX*> (pair of pointers) casted to <2 x i64>, need to cast back
			if (v->getType()->isVectorTy() && origType->getVectorElementType()->isPointerTy()) {
					move = cast<Instruction>(irBuilder.CreateIntToPtr(move, origType, v->getName() + CLONE_SUFFIX));
			}
		}
		return move;
	}

	void shadowInstOperands(Instruction* I, Instruction* shadow) {
		for (unsigned i = 0; i < shadow->getNumOperands(); ++i) {
			Value *op = I->getOperand(i);
			Value *shadowOp = shadows.getShadow(op, I);
			if (shadowOp)
				shadow->setOperand(i, shadowOp);
		}
	}

	void checkInstOperands(Instruction* I, IRBuilder<> irBuilder) {
		for (User::op_iterator op = I->op_begin(); op != I->op_end(); ++op) {
			Value *shadowop = shadows.getShadow(*op, I);
			if (shadowop)
				createCheckerCall(irBuilder, *op, shadowop, false);
		}
	}


	public:
	void shadowInst(Instruction* I) {

		if (I->use_empty())
			return;

#if 0
		if (I->isTerminator())
			errs() << I->getParent()->getParent()->getName() << "::  cannot shadow terminator instruction " << *I << "\n";
		assert (!I->isTerminator() && "cannot shadow terminator instruction");
#endif

		// add shadow instruction(s) after I
		BasicBlock::iterator instIt(I);
		IRBuilder<> irBuilder(instIt->getParent(), ++instIt);

		switch (I->getOpcode()) {
		/* Standard binary operators */
		case Instruction::Add:
		case Instruction::FAdd:
		case Instruction::Sub:
		case Instruction::FSub:
		case Instruction::Mul:
		case Instruction::FMul:
		case Instruction::UDiv:
		case Instruction::SDiv:
		case Instruction::FDiv:
		case Instruction::URem:
		case Instruction::SRem:
		case Instruction::FRem:
		/* Logical operators */
		case Instruction::And:
		case Instruction::Or:
		case Instruction::Xor:
		/* Memory instructions */
		case Instruction::GetElementPtr: /* other insts are with checks */
		/* Convert instructions */
		case Instruction::Trunc:
		case Instruction::ZExt:
		case Instruction::SExt:
		case Instruction::FPTrunc:
		case Instruction::FPExt:
		case Instruction::FPToUI:
		case Instruction::FPToSI:
		case Instruction::UIToFP:
		case Instruction::SIToFP:
		case Instruction::IntToPtr:
		case Instruction::PtrToInt:
		case Instruction::BitCast:
		/* Other instructions */
		case Instruction::ICmp:
		case Instruction::FCmp:
		case Instruction::Select:
		case Instruction::Shl:
		case Instruction::LShr:
		case Instruction::AShr:
		case Instruction::ExtractElement:
		case Instruction::InsertElement:
		case Instruction::ShuffleVector:
		case Instruction::ExtractValue:
		case Instruction::InsertValue:
		/* Loads */
		case Instruction::Load: {
			if (LoadInst* load = dyn_cast<LoadInst>(I)) {
#ifdef SWIFT_OPTIMIZE_SHARED_MEMORY_ACCESSES
				if (isa<GlobalVariable>(load->getPointerOperand())) {
					// optimization: if load global variable, we know that
					// address is constant and no need to load twice
					// NOTE: this optimization is also ad-hoc solution to
					//       race conditions on synchronization flags --
					//       they are usually declared as globals
					Instruction *shadow = createMoveCall(irBuilder, I);
					shadows.add(I, shadow);
					break;
				}
				if (load->isAtomic()) {
					// atomic loads work on shared non-locked data, we must not
					// load twice, otherwise can load two inconsistent copies
					// (see also checkInst)
					Instruction *shadow = createMoveCall(irBuilder, I);
					shadows.add(I, shadow);
					break;
				}
#else
				// conservatively treat all loads as atomics
				Instruction *shadow = createMoveCall(irBuilder, I);
				shadows.add(I, shadow);
				break;
#endif
			}

			// shadow instruction, substituting all operands with shadow operands
			Instruction* shadow = I->clone();
			shadowInstOperands(I, shadow);

			if (LoadInst* load = dyn_cast<LoadInst>(shadow)) {
				// make load volatile so not to be optimized away
				load->setVolatile(true);
			}

			shadows.add(I, shadow);
			irBuilder.Insert(shadow, I->getName() + CLONE_SUFFIX);
			}
			break;

		case Instruction::PHI: {
			// shadow PHI instruction, but delay shadowing its operands after
			// all function modifications (because PHI uses Value before it's
			// declared and thus shadowed)
			PHINode* shadow = cast<PHINode>( I->clone() );
			shadows.add(I, shadow);
			irBuilder.Insert(shadow, I->getName() + CLONE_SUFFIX);
			phis.push_back(shadow);
			}
			break;

		/* allocators, function calls */
		case Instruction::Alloca:
		case Instruction::Call:
		case Instruction::VAArg:
		case Instruction::AtomicCmpXchg:	// we treat cmpxchg as a load-store instruction
		case Instruction::AtomicRMW:        // we treat atomicrmw as a load-store instruction
		{
			if (CallInst* call = dyn_cast<CallInst>(I)) {
				// treat duplicated (llvm-intrinsic) functions as normal instructions
				if (swiftHelpers->isDuplicatedFunc(call->getCalledFunction())) {
					Instruction* shadow = I->clone();
					shadowInstOperands(I, shadow);
					shadows.add(I, shadow);
					irBuilder.Insert(shadow, I->getName() + CLONE_SUFFIX);
				 	break;
				}

				// do not shadow calls to "ignored" functions
				if (swiftHelpers->isIgnoredFunc(call->getCalledFunction()))
				 	break;
			}

			// make a shadow for allocated/returned value using swift-move
			Instruction *shadow = createMoveCall(irBuilder, I);
			shadows.add(I, shadow);
			}
			break;

		case Instruction::Invoke:
		case Instruction::LandingPad:
		case Instruction::Resume:
			/* TODO: handle these instructions */
			break;

		default:
			errs() << "unknown instruction " << *I << "\n";
			assert(!"cannot handle unknown instruction");
			break;
		}
	}

	void checkInst(Instruction* I) {
		BasicBlock::iterator instIt(I);

		switch (I->getOpcode()) {
			case Instruction::AtomicCmpXchg:	// we treat cmpxchg as a load-store instruction
			case Instruction::AtomicRMW:        // we treat atomicrmw as a load-store instruction
			case Instruction::Call:
			case Instruction::Ret:
			case Instruction::Switch:
			case Instruction::Invoke: {
				if (CallInst* call = dyn_cast<CallInst>(I)) {
					// do not do anything with duplicated (llvm-intrinsic) functions
					if (swiftHelpers->isDuplicatedFunc(call->getCalledFunction()))
					 	break;
					// do not check calls to "ignored" functions
					if (swiftHelpers->isIgnoredFunc(call->getCalledFunction()))
					 	break;
				}

				// add check instruction(s) before I
				IRBuilder<> irBuilder(instIt->getParent(), instIt);
				checkInstOperands(I, irBuilder);
				}
				break;

			case Instruction::Br: {
					BranchInst *BI = cast<BranchInst>(I);
					if (BI->isUnconditional())
						break;

					// delay adding control-flow checks on conditional branches
					brs.push_back(BI);
				}
				break;

			case Instruction::Load: {
				LoadInst *LI = cast<LoadInst>(I);
#ifdef SWIFT_OPTIMIZE_SHARED_MEMORY_ACCESSES
				if (LI->isAtomic()) {
					// atomic loads work on shared non-locked data, we must not
					// load twice; that is why we check (address) operand here
					IRBuilder<> irBuilder(instIt->getParent(), instIt);
					checkInstOperands(I, irBuilder);
				}
#else
				// conservatively check operands on each load
				IRBuilder<> irBuilder(instIt->getParent(), instIt);
				checkInstOperands(I, irBuilder);
#endif
				}
				break;

			case Instruction::Store: {
				StoreInst *SI = cast<StoreInst>(I);

#ifdef SWIFT_OPTIMIZE_SHARED_MEMORY_ACCESSES
				if (isa<GlobalVariable>(SI->getPointerOperand())) {
					// optimization: if store global variable, we know that
					// address is constant and no need to load after store
					// NOTE: this optimization is also ad-hoc solution to
					//       race conditions on synchronization flags --
					//       they are usually declared as globals
					//       (see also code for Instruction::Load)
					IRBuilder<> irBuilder(instIt->getParent(), instIt);
					checkInstOperands(I, irBuilder);
					break;
				}

				if (SI->isAtomic()) {
					// atomic stores work on shared non-locked data, we must not
					// load after store, otherwise can see inconsistent copies;
					// so we check operands before store
					IRBuilder<> irBuilder(instIt->getParent(), instIt);
					checkInstOperands(I, irBuilder);
					break;
				}

				// add check instruction(s) after I
				IRBuilder<> irBuilder(instIt->getParent(), std::next(instIt));

				// get a shadow value for this store
				Value *val = SI->getValueOperand();
				Value *shadowval = shadows.getShadow(val, SI);
				if (!shadowval) {
					// no meaningful shadow value, nothing to check against
					break;
				}

				// get shadow address (or reuse master address) to load a freshly stored value
				Value *ptr = SI->getPointerOperand();
				Value *shadowptr = shadows.getShadow(ptr, SI);
				if (!shadowptr)	shadowptr = ptr;

				// alignment
				unsigned align = SI->getAlignment();

				LoadInst *loadedval = irBuilder.CreateLoad(shadowptr, true, "swift.loadtocheckstore"); // volatile load
				loadedval->setAlignment(align);

				// compare freshly stored value (reloaded for comparison) and shadow value
				createCheckerCall(irBuilder, loadedval, shadowval, false);

#else
				// conservatively check all stores' operands
				IRBuilder<> irBuilder(instIt->getParent(), instIt);
				checkInstOperands(I, irBuilder);
#endif
				}
				break;

			default:
				break;
		}
	}

	void shadowArgs(Function& F, Instruction* firstI) {
		// add shadow args' definitions before firstI
		BasicBlock::iterator instIt(firstI);
		IRBuilder<> irBuilder(instIt->getParent(), instIt);

		// make a shadow for each function arg (using swift-move)
		for (auto arg = F.arg_begin(); arg != F.arg_end(); ++arg){
			Value *shadow = createMoveCall(irBuilder, &*arg);
			shadows.add(&*arg, shadow);
		}
	}

	void rewireShadowPhis() {
		// substitute all incoming values in all collected phis with shadows
		for (auto phiIt = phis.begin(); phiIt != phis.end(); ++phiIt) {
			PHINode* phi = *phiIt;
			for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i){
				// check if any previous operand of PHI has the same incoming BB;
			    // this happens when incoming BB has a switch statement
			    bool sameIncomingValue = false;
			    BasicBlock *IncomingBB = phi->getIncomingBlock(i);
			    for (unsigned j = 0; j < i; ++j) {
			      	if (phi->getIncomingBlock(j) == IncomingBB) {
			        	Value *IncomingVal = phi->getIncomingValue(j);
						phi->setIncomingValue(i, IncomingVal);
			        	sameIncomingValue = true;
			      	}
			    }
			    if (sameIncomingValue) continue;

				Value *v = phi->getIncomingValue(i);
				Value *shadow = shadows.getShadow(v, phi);
				if (shadow)
					phi->setIncomingValue(i, shadow);
				else if (ConstantInt *c = dyn_cast<ConstantInt>(v)) {
					// if incoming value is a constant, this PHI is most likely
					// an induction var in loop, and codegen optimizes it away;
					// to prevent it, we use a hack:
					//   - create a global constant variable set to constant
					//   - insert a load of this global in PHI's incoming block
					//   - replace incoming value-constant with load's result
					if (c->getBitWidth() > 64)
						continue;

					// create a global constant variable set to constant
					GlobalVariable* gv = nullptr;
					std::pair<Type*, uint64_t> keypair = std::make_pair(c->getType(), c->getValue().getLimitedValue());
					GlobalConstMap::iterator it = globalconsts.find(keypair);
					if (it == globalconsts.end()) {
						// need to create GlobalVariable
						static int cnt = 0;
						gv = new GlobalVariable(*swiftHelpers->module,
								c->getType(),
								true, // constant
								GlobalValue::InternalLinkage,
								c,	  // ConstantInt
								"SWIFT$global" + std::to_string(cnt++));
						globalconsts.insert(std::make_pair(keypair, gv));
					} else {
						// reuse already existing GlobalVariable
						gv = it->second;
					}
					assert(gv && "could not create/find global variable for PHI node");

					// insert a load of global var in PHI's incoming block
					IRBuilder<> irBuilder(phi->getIncomingBlock(i)->getTerminator());
					Value* loaded = irBuilder.CreateLoad(gv, true); // volatile load
					// replace incoming value-constant with load's result
					phi->setIncomingValue(i, loaded);
				}
			}
		}
	}

	// detect if V already requires check inside loop L:
	// transitively follow all uses of V that are still inside loop L
	// and if at least one of these uses requires a check, return true
	bool requiresCheckTransitive(Loop* L, Value* V) {
//		errs() << "requiresCheckTransitive: Loop " << L << " Value " << *V << "\n";
		for (User *U : V->users()) {
			if (!isa<Instruction>(U))
				continue;
			Instruction* I = cast<Instruction>(U);

			if (!L->contains(I))
				continue;

			if (isa<PHINode>(I) && I->getParent() == L->getHeader()) {
				// to break cycle dep, stop on the loop header's phi nodes
				continue;
			}

			if (isa<StoreInst>(I) || isa<BranchInst>(I) ||
				isa<AtomicCmpXchgInst>(I) || isa<AtomicRMWInst>(I) ||
				isa<ReturnInst>(I) || isa<SwitchInst>(I) || isa<InvokeInst>(I))
				return true;

			if (LoadInst* load = dyn_cast<LoadInst>(I))
				if (load->isAtomic())
					return true;

			if (CallInst* call = dyn_cast<CallInst>(I)) {
				if (!swiftHelpers->isDuplicatedFunc(call->getCalledFunction()) &&
					!swiftHelpers->isIgnoredFunc(call->getCalledFunction()))
			 	return true;
			}

			if (requiresCheckTransitive(L, I))
				return true;
		}
		return false;
	}

	void insertChecksOnLoopHeader(Loop* L, DominatorTree* DT) {
		for (auto li = L->begin(), le = L->end(); li != le; ++li) {
			insertChecksOnLoopHeader(*li, DT);
		}

		if (!L->empty()) {
			// we are only interested in most-inner loops
			return;
		}

		std::vector<PHINode*> phiesToCheck;
		BasicBlock* header = L->getHeader();
		for (BasicBlock::iterator I = header->begin(); I != header->end(); I++) {
			if (!isa<PHINode>(I)) {
				// if instr is not phi, then we know this header has no more phis
				break;
			}

			PHINode* phi = cast<PHINode>(I);
			if (!shadows.hasShadow(phi)) {
				// this is already a shadow phi, ignore
				continue;
			}

			// we located original phi, examine its transitive def-use chain:
			// if it is used in some loop instr that requires a check, then
			// this phi is assumed to be already checked inside the loop,
			// if there are no such instrs, this phi must be explicitly checked
			if (!requiresCheckTransitive(L, phi))
				phiesToCheck.push_back(phi);
		}

		if (!phiesToCheck.empty()) {
			// insert BB with explicit checks for selected phis,
			// this BB will be subsequently transformed by Trans and
			// executed before conditional Tx-end
			ConstantInt* CondZero = ConstantInt::get(Type::getInt1Ty(getGlobalContext()), 0);
			TerminatorInst* CheckTerm = SplitBlockAndInsertIfThen(CondZero, header->getFirstNonPHI(), false, nullptr, DT);

			IRBuilder<> irBuilder(CheckTerm);
			for (auto phiIt = phiesToCheck.begin(); phiIt != phiesToCheck.end(); ++phiIt) {
				PHINode* phi = *phiIt;
				Value *shadowphi = shadows.getShadow(phi, phi);
				createCheckerCall(irBuilder, phi, shadowphi, false);
			}
		}
	}

	void insertChecksOnLoopHeaders(LoopInfo &LI, DominatorTree* DT) {
#ifdef SWIFT_INSERT_CHECKS_ON_LOOP_HEADERS
		for (auto li = LI.begin(), le = LI.end(); li != le; ++li) {
			insertChecksOnLoopHeader(*li, DT);
		}
#endif
	}

	void addShadowBasicBlocks(BranchInst* BI) {
		if (CmpInst *cmp1 = dyn_cast<CmpInst>(BI->getCondition())) {
			CmpInst *cmp2 = dyn_cast<CmpInst>( shadows.getShadow(cmp1, BI) );
			if (!cmp2) return;

			// --- create Detected BB once for each function ---
			if (!detectedBB) {
				LLVMContext &C = BI->getContext();
				detectedBB = BasicBlock::Create(C, "Detected", BI->getParent()->getParent());
				IRBuilder<> irBuilder(detectedBB);
				ArrayRef<Value*> args;
				irBuilder.CreateCall(swiftHelpers->detectedfunc, args);
				irBuilder.CreateUnreachable();
			}

			for (unsigned succidx = 0; succidx < BI->getNumSuccessors(); succidx++) {
				BasicBlock *SuccBB = BI->getSuccessor(succidx);

				CmpInst* succcmp2 = cast<CmpInst>(cmp2->clone());
				if (succidx == 0)	// True block
					succcmp2->setPredicate(succcmp2->getInversePredicate());

				// create shadow BB
				LLVMContext &C = SuccBB->getContext();
				BasicBlock *SuccBBShadow = BasicBlock::Create(C, "BBShadow", SuccBB->getParent(), SuccBB);

				// fill shadow BB with comparison and br to "detected"
				IRBuilder<> irBuilder(SuccBBShadow);
				irBuilder.Insert(succcmp2);

				// rewire Branch -> shadow BB -> true BB
				irBuilder.CreateCondBr(succcmp2, detectedBB, SuccBB);
				BI->setSuccessor(succidx, SuccBBShadow);

				// update Phis in succ BB to set shadow-BB as incoming block
				for (BasicBlock::iterator I = SuccBB->begin(); isa<PHINode>(I); ) {
					PHINode *PI = cast<PHINode>(I++);
					for (unsigned i = 0; i < PI->getNumIncomingValues(); ++i){
					    BasicBlock *IncomingBB = PI->getIncomingBlock(i);
					    if (IncomingBB == BI->getParent())
					    	PI->setIncomingBlock(i, SuccBBShadow);
					}
				}
			}

			// Note that the original shadow-comparison becomes redundant
			// and will be optimized away, so we don't remove it explicitly
		}
	}

	void addControlFlowChecks() {
		for (auto brIt = brs.begin(); brIt != brs.end(); ++brIt) {
			BranchInst* BI = *brIt;
#ifdef SWIFT_SIMPLE_CONTROL_FLOW
			// insert additional shadow BBs to protect against status
			// register corruption
			addShadowBasicBlocks(BI);
#else
			// fallback to simple & slow strategy: check branch condition
			IRBuilder<> irBuilder(BI->getParent(), BI);
			Value *val1 = BI->getCondition();
			Value *val2 = shadows.getShadow(val1, BI);
			if (val2)  createCheckerCall(irBuilder, val1, val2, false);
#endif
		}

	}

	SwiftTransformer(SwiftHelpers* inSwiftHelpers) {
		swiftHelpers = inSwiftHelpers;
	}
};

class SwiftPass : public FunctionPass {
	SwiftHelpers* swiftHelpers;

	public:
	static char ID; // Pass identification, replacement for typeid

	SwiftPass(): FunctionPass(ID) { }

	virtual bool doInitialization(Module& M) {
		swiftHelpers = new SwiftHelpers(M);
		return true;
	}

	virtual bool runOnFunction(Function &F) {
		std::set<BasicBlock*> visited;

		if (swiftHelpers->isIgnoredFunc(&F)) return false;

		DominatorTree& DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
		LoopInfo& LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
		SwiftTransformer swifter(swiftHelpers);

		bool shadowedArgs = false;

		// walk through BBs in the dominator tree order
		for (df_iterator<DomTreeNode*> DI = df_begin(DT.getRootNode()),	E = df_end(DT.getRootNode()); DI != E; ++DI){
			BasicBlock *BB = DI->getBlock();
			visited.insert(BB);

			for (BasicBlock::iterator instIt = BB->begin (); instIt != BB->end (); ) {
				// Swift can add instructions after the current instruction,
				// so we first memorize the next original instruction and after
				// modifications we jump to it, skipping swift-added ones
				BasicBlock::iterator nextIt = std::next(instIt);

				if (!shadowedArgs) {
					swifter.shadowArgs(F, &*instIt);
					shadowedArgs = true;
				}

				swifter.checkInst (&*instIt);
				swifter.shadowInst (&*instIt);

				instIt = nextIt;
			}
		}

		// walk through BBs not covered by dominator tree (case for landing pads)
		for (Function::iterator BB = F.begin(), BE = F.end(); BB != BE; ++BB) {
			if (visited.count(&*BB) > 0)
				continue;

			for (BasicBlock::iterator instIt = BB->begin (); instIt != BB->end (); ) {
				BasicBlock::iterator nextIt = std::next(instIt);
				swifter.checkInst (&*instIt);
				swifter.shadowInst (&*instIt);
				instIt = nextIt;
			}
		}

		swifter.rewireShadowPhis();
		swifter.insertChecksOnLoopHeaders(LI, &DT);
		swifter.addControlFlowChecks();

		// some swift-moves can become redundant due to checks optimized away
		// -> find and remove them
		for (Function::iterator BB = F.begin(), BE = F.end(); BB != BE; ++BB)
			for (BasicBlock::iterator instIt = BB->begin (); instIt != BB->end (); ) {
				Instruction *I = &*instIt;
				instIt = std::next(instIt);

				if (!isa<CallInst>(I))
					continue;

				CallInst* call = cast<CallInst>(I);
				if (!call->getCalledFunction() || call->getCalledFunction()->getName() == "")
					continue;

				if (swiftHelpers->helpers.count(call->getCalledFunction()) == 0)
					continue;

				if (call->getCalledFunction()->getName().count("move") == 0)
					continue;

				// it's a swift-move!
				if (I->getNumUses() == 0) {
					BasicBlock::iterator toerase(I);
					toerase->eraseFromParent();
				}
			}

		// inform that we always modify a function
		return true;
	}

	virtual bool doFinalization(Module& M) {
		delete swiftHelpers;
		return false;
	}

	virtual void getAnalysisUsage(AnalysisUsage& UA) const {
		UA.addRequired<LoopInfoWrapperPass>();
		UA.addPreserved<LoopInfoWrapperPass>();
		UA.addRequired<DominatorTreeWrapperPass>();
		FunctionPass::getAnalysisUsage(UA);
	}
};

char SwiftPass::ID = 0;
static RegisterPass<SwiftPass> X("ilr", "ILR Pass");

}
