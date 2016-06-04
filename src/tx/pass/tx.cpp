//===---------- tx.cpp - Transactification pass ---------------------------===//
//
//	 This pass inserts boundaries of transactions in a function:
//
//	   - at func beginning and func exit points:
//         - unconditional for funcs called-from-outside
//         - conditional on dynamic counter for funcs not called-from-outside
//
//	   - at function calls (end before call, start new one after call)
//         - unconditional for calls to ouside-funcs
//         - conditional for calls to local funcs
//
//     - at loop headers based on the dynamic counter
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "Transactify"

// --- uncomment if you'd like to activate some optimization ---

// erase transactions that start and end immediately
// or conditionally start and increment immediately
#define TRANS_OPTIMIZE_EMPTYTX

// Erase conditional start at the header of tight loop
// & corresponding increment at the end of loop;
//   tight loops are those that (1) consist of one BB and
//   (2) do not have calls or stores inside.
// The assumption is that these loops are sufficiently small and
// do not result in aborts.
#define TRANS_OPTIMIZE_TIGHTLOOPS

// Erase transaction ends and starts around pthread_mutex_lock/unlock
// if critical section guarded by them is "tiny" (spans 1-2 BBs and
// has no calls to other functions).
// This optimization substitutes tiny critical sections that produce
// huge overhead due to Tx-end & Tx-start by optimistic HTM.
#define TRANS_OPTIMIZE_CRITICALSECTIONS

// optimization to explicitly check local loop vars before Tx-end
// NOTE: assumes an earlier Swift pass, does not alter behaviour w/o it
#define TRANS_INSERT_CHECKS_ON_LOOP_HEADERS


#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include "llvm/ADT/Statistic.h"
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
#include <llvm/Analysis/LoopIterator.h>
#include <llvm/Support/CommandLine.h>

#include <map>
#include <set>

using namespace llvm;

// note that this option cancels out the next ones
static cl::opt<bool>
	FuncExplicitTrans("func-explicit-trans", cl::Optional, cl::init(false),
	cl::desc("All functions are wrapped in explicit transactions (very conservative)"));

static cl::list<std::string>
	CalledFromOutside("called-from-outside", cl::ZeroOrMore,
	cl::desc("Function (typically event handler) that is called from outside"));

static cl::opt<bool>
	FuncPointersKnown("func-pointers-known", cl::Optional, cl::init(false),
	cl::desc("All func pointers point only to known (defined in module) funcs"));

STATISTIC(TransNum, "Number of transactions inserted");
STATISTIC(CondTransNum, "Number of conditional transactions inserted");

namespace {

// global helper functions
Function *tx_cond_start_func           = nullptr;
Function *tx_start_func                = nullptr;
Function *tx_end_func                  = nullptr;
Function *tx_abort_func                = nullptr;
Function *tx_threshold_exceeded_func   = nullptr;
Function *tx_increment_func            = nullptr;
Function *tx_pthread_mutex_lock_func   = nullptr;
Function *tx_pthread_mutex_unlock_func = nullptr;

bool isSwiftFunc(std::string FuncName) {
	std::string prefix = "SWIFT$";
	if (!FuncName.compare(0, prefix.size(), prefix))
		return true;
	return false;
}

bool isInternalFunc(Function* F) {
	if (F) {
		if (F->isIntrinsic() ||
			F == tx_cond_start_func ||
			F == tx_start_func ||
			F == tx_end_func ||
			F == tx_abort_func ||
			F == tx_threshold_exceeded_func ||
			F == tx_increment_func ||
			F == tx_pthread_mutex_lock_func ||
			F == tx_pthread_mutex_unlock_func)
			return true;

		if (F->getName() != "" && isSwiftFunc(F->getName()))
			return true;
	}
	return false;
}

// helper functions
bool isCallToOutside(Function* F) {
	static std::set<std::string> func_exceptions {
		// math funcs must be simple and no syscalls
		"__log_finite",
		// rands are simple and no syscalls
		"rand",
		"lrand48",
		"__dummy__"
	};

	// if user specified to be very conservative
	if (FuncExplicitTrans)
		return true;
	// function pointer, check pass option (do we have to be conservative?)
	if (!F) {
		if (FuncPointersKnown) return false;
		else return true;
	}
	// function is internal function like Trans/Swift funcs or LLVM intrinsics
	if (isInternalFunc(F))
		return false;
	// function is an outside function, but exception
	if (func_exceptions.count(F->getName()))
		return false;
	// function is an outside function (no definition of F in this module)
	if (F->isDeclaration())
		return true;

	return false;
}

bool isCalledFromOutside(std::string FuncName) {
	// if user specified to be very conservative
	if (FuncExplicitTrans)
		return true;
	// main function is always called from outside
	if (FuncName == "main")
		return true;
	// check user-specified list of funcs
	if (std::find(CalledFromOutside.begin(), CalledFromOutside.end(), FuncName) != CalledFromOutside.end())
		return true;
	return false;
}


class Transactifier {
	LoopInfo* LI;

	std::set<BasicBlock*> Visited;
	std::map<BasicBlock*, size_t> LongestPaths;

	std::set<Instruction*> LocksToOptimize;

public:

	Transactifier(LoopInfo* _LI) {
		LI = _LI;
	}

	void insertTxEnd(Instruction* I) {
		IRBuilder<> irBuilder(I);
		irBuilder.CreateCall(tx_end_func);
	}

	void insertTxStart(Instruction* I) {
		TransNum++;  // bump statistic counter
		IRBuilder<> irBuilder(I);
		irBuilder.CreateCall(tx_start_func);
	}

	void insertCondTxStart(Instruction* I) {
		CondTransNum++;  // bump statistic counter
		IRBuilder<> irBuilder(I);
		irBuilder.CreateCall(tx_cond_start_func);
	}

	void assignLongestPath(BasicBlock* BB, size_t N) {
		LongestPaths.erase(BB);
		LongestPaths.insert(std::pair<BasicBlock*, size_t>(BB, N));
	}

	void insertCounterIncrement(Instruction* I, size_t Inc) {
		if (Inc <= 0 || Inc >= 1000000) // sanity check
			return;

		IRBuilder<> irBuilder(I);
		irBuilder.CreateCall(tx_increment_func,
			ConstantInt::get(getGlobalContext(), APInt(64, Inc)));
	}

	void insertCounterIncrement(BasicBlock* BB, size_t Inc) {
		Instruction* I = BB->getTerminator();
		assert(I && "BB has no terminator");
		insertCounterIncrement(I, Inc);
	}

	size_t getNumAsmInstructions(BasicBlock* BB) {
		size_t s = 0;
		for (BasicBlock::reverse_iterator bi = BB->rbegin(), fi = BB->rend(); bi != fi; ++bi) {
			Instruction* I = &*bi;
			// call to start/cond_start of Tx or counter increment -> stop incrementing size
			if (CallInst* call = dyn_cast<CallInst>(I)) {
				Function* func = call->getCalledFunction();
				if (func == tx_start_func || func == tx_cond_start_func || func == tx_increment_func)
					break;
			}

			// do not count no-op casts
			if (CastInst* ci = dyn_cast<CastInst>(I)) {
				// assuming 64-bit platform
				static Type* IntPtrTy = Type::getInt64Ty(getGlobalContext());
				if (ci->isNoopCast(IntPtrTy))
					continue;
			}

			// do not count phies and unreachables
			if (isa<PHINode>(I) || isa<UnreachableInst>(I))
				continue;

			s += 1;
		}
		return s;
	}

	void initLongestPath(BasicBlock* BB) {
		size_t LongestPath = 0;
		for (auto it = pred_begin(BB), et = pred_end(BB); it != et; ++it) {
				BasicBlock* PredBB = *it;
				// find a previously (due to toposort) calculated longest path
				// of PredBB; if there is no entry for this PredBB, then there
				// was a cycle that broke toposort -- just ignore it
				if (LongestPaths.count(PredBB) == 0)  continue;
				size_t PredPath = LongestPaths.find(PredBB)->second;
				if (LongestPath < PredPath)  LongestPath = PredPath;
		}
		assignLongestPath(BB, LongestPath);
	}

	void visitInst(Instruction* I, size_t instIdx) {
		// ----- logic to count instructions -----
		// do not count no-op casts
		if (CastInst* ci = dyn_cast<CastInst>(I)) {
			// assuming 64-bit platform
			static Type* IntPtrTy = Type::getInt64Ty(getGlobalContext());
			if (ci->isNoopCast(IntPtrTy))
				return;
		}
		// do not count phies and unreachables
		if (isa<PHINode>(I) || isa<UnreachableInst>(I))
			return;
		// I's BasicBlock must have been initialized with LongestPath
		// increment its Path by one instruction
		assert(LongestPaths.count(I->getParent()) > 0);
		size_t BBPath = LongestPaths.find(I->getParent())->second;
		assignLongestPath(I->getParent(), BBPath + 1);

		// ----- logic to insert Tx boundaries for invokes/calls and returns -----
		unsigned Opcode = I->getOpcode();
		switch (Opcode) {
			case Instruction::Invoke:
			case Instruction::Call: {
				Function* func = nullptr;
				if (Opcode == Instruction::Invoke) {
					func = cast<InvokeInst>(I)->getCalledFunction();
				} else if (Opcode == Instruction::Call) {
					func = cast<CallInst>(I)->getCalledFunction();
				}

				if (isInternalFunc(func))
					return;

				// update the counter to inform callee
				// NOTE: this increment is erased by BB optimization if redundant
				size_t BBPath = LongestPaths.find(I->getParent())->second;
				insertCounterIncrement(I, BBPath);

				BasicBlock::iterator instIt(I);
				if (isCallToOutside(func)) {
					// callee is an outside func and cannot be inside Tx
					// end transaction before call and start a new one after it
					insertTxEnd(I);

					if (InvokeInst* invoke = dyn_cast<InvokeInst>(I)) {
						// cannot insert after Invokes, so insert into succ normal BB
						Instruction* normalI = invoke->getNormalDest()->getFirstNonPHI();
						if (invoke->getNormalDest()->getSinglePredecessor() == nullptr)
							insertTxEnd(normalI);
						insertTxStart(normalI);
						// NOTE: do not insert into unwind BB because it
						//       hopefully never executes anyway
					} else {
						insertTxStart(&*std::next(instIt));
					}
				} else {
					// callee is local and thus inside Tx
					// start new Tx if needed after call
					if (InvokeInst* invoke = dyn_cast<InvokeInst>(I)) {
						// cannot insert after Invokes, so insert into succ normal BB
						Instruction* normalI = invoke->getNormalDest()->getFirstNonPHI();
						insertCondTxStart(normalI);
						// NOTE: do not insert into unwind BB because it
						//       hopefully never executes anyway
					} else {
						insertCondTxStart(&*std::next(instIt));
					}
				}

				assignLongestPath(I->getParent(), 0);
				return;
			}

			case Instruction::Resume:
			case Instruction::Ret: {
				if (isCalledFromOutside(I->getParent()->getParent()->getName())) {
					// caller cannot be inside Tx, so end Tx right before return
					insertTxEnd(I);
				} else {
					// caller is local and thus inside Tx, no need to end Tx
					// just update the counter to inform caller
					size_t BBPath = LongestPaths.find(I->getParent())->second;
					insertCounterIncrement(I, BBPath-1); // ignore Return
				}
				// for sanity
				assignLongestPath(I->getParent(), 0);
				return;
			}

			default: {
				return;
			}
		}
	}

	void visitBasicBlock(BasicBlock* BB) {
		size_t instIdx = 0;
		for (BasicBlock::iterator bi = BB->begin(); bi != BB->end(); ) {
			// Transactifier can add instructions after current instruction,
			// so we first memorize the next original instruction and after
			// modifications we jump to it, skipping trans-added ones
			BasicBlock::iterator nextbi = std::next(bi);
			visitInst(&*bi, instIdx);
			bi = nextbi;
			instIdx++;
		}
	}

	// TODO: most probably this doesn't couple with Swift, since Swift
	//       adds additional "shadow" BB in the loop; remove or make smarter?
	void optimizeLoop(Loop* L) {
#ifdef TRANS_OPTIMIZE_TIGHTLOOPS
		if (L->getNumBlocks() != 1) {
			// only optimize tight loops with one BB
			return;
		}

		CallInst* txcondstartcall = nullptr;
		CallInst* txincrementcall = nullptr;

		BasicBlock* BB = L->getBlocks()[0];
		size_t BBPath = BB->size();

		if (BBPath > 20) {
			// consider >20 instructions not a tight loop
			return;
		}

		for (BasicBlock::iterator bi = BB->begin(); bi != BB->end(); ) {
			Instruction* I = &*bi;
			bi = std::next(bi);

			if (CallInst* call = dyn_cast<CallInst>(I)) {
				Function* F = call->getCalledFunction();
				if (F == tx_cond_start_func) {
					txcondstartcall = call;
					continue;
				}
				if (F == tx_increment_func) {
					txincrementcall = call;
					continue;
				}
				// there is some function call, loop is not tight
				return;
			}

			if (isa<InvokeInst>(I) || isa<StoreInst>(I) || isa<AtomicCmpXchgInst>(I) || isa<AtomicRMWInst>(I)) {
				// loop is not simple enough for us
				return;
			}
		}

		if (!txcondstartcall)
			return;

		// optimize away conditional start & increment if it was a tight loop
		assert(txincrementcall && "impossible: conditional start without counter increment in a tight loop");

		txcondstartcall->eraseFromParent();
		txincrementcall->eraseFromParent();

		// increment dynamic counter in the preheader of this tight loop
		// we don't know the real trip count in loop, so use some constant
		if (BasicBlock* preheader = L->getLoopPreheader()) {
			size_t AVERAGE_TRIP_COUNT = 4;  // TODO: 4 is taken from top of my head
			TerminatorInst* terminator = preheader->getTerminator();
			insertCounterIncrement(terminator, BBPath * AVERAGE_TRIP_COUNT);
		}
#endif
	}

	void insertChecksOnLoopHeaders(Loop* L) {
#ifdef TRANS_INSERT_CHECKS_ON_LOOP_HEADERS
		// earlier Swift pass must have inserted this code snippet:
		//     loop_header:
		//       if (0) { block-with-checks }
		//       .. continue loop header ..
		// if we find it, must substitute "0" by "tx_threshold_exceeded"
		BasicBlock* header = L->getHeader();

		BasicBlock::iterator firstNonPhi(header->getFirstNonPHI());
		for (BasicBlock::iterator I = firstNonPhi; I != header->end(); I++) {
			if (BranchInst* br = dyn_cast<BranchInst>(I)) {
				if (!br->isConditional()) continue;
				Value* cond = br->getCondition();
				if (!isa<ConstantInt>(cond)) continue;
				if (!(cast<ConstantInt>(cond))->isZero()) continue;

				// --- found branch with zero-constant condition! ---
				// remove conditional Tx start inserted in visitLoop()
				Instruction* firstI = header->getFirstNonPHI();
				assert((cast<CallInst>(firstI))->getCalledFunction() == tx_cond_start_func);
				firstI->eraseFromParent();

				// substitute zero-condition with threshold_exceeded func
				IRBuilder<> irBuilder(br);
				Value* flag = irBuilder.CreateCall(tx_threshold_exceeded_func);
				Value* flag_i1 = irBuilder.CreateTrunc(flag, Type::getInt1Ty(getGlobalContext()));
				br->setCondition(flag_i1);

				// add Tx end and Tx start after the checks (which is a True branch)
				Instruction* IAfterChecks = br->getSuccessor(0)->getTerminator();
				insertTxEnd(IAfterChecks);
				insertTxStart(IAfterChecks);
				return;
			}
		}
#endif
	}

	void visitLoop(Loop* L) {
		// at the beginning of loop, insert conditional Tx start
		insertCondTxStart(L->getHeader()->getFirstNonPHI());

		// insert check on loop header (assumes earlier Swift pass)
		insertChecksOnLoopHeaders(L);

		// first visit all subloops
		for (auto li = L->begin(), le = L->end(); li != le; ++li) {
			visitLoop(*li);
		}

		// next calculate & insert longest path in each latch in this loop
		SmallVector<BasicBlock*, 8> LoopLatches;
		L->getLoopLatches(LoopLatches);

		// we need a reverse topological order on BBs inside the loop
		LoopBlocksDFS DFS(L);
	    DFS.perform(LI);

	    // go through toposorted BBs inside loop, saving their longest paths
		LongestPaths.clear();
		for (auto bi = DFS.beginRPO(); bi != DFS.endRPO(); ++bi) {
			BasicBlock* BB = *bi;

			// if BB was visited in subloop, ignore it -- it was already counted
			if (Visited.count(BB))  continue;

			initLongestPath(BB);
			visitBasicBlock(BB);
			Visited.insert(BB);

			size_t LongestPath = LongestPaths.find(BB)->second;
			for (auto li = LoopLatches.begin(), le = LoopLatches.end(); li != le; ++li) {
				if (*li == BB) {
					// this BB is a Latch, increment dynamic counter at its end
					insertCounterIncrement(BB, LongestPath);
					assignLongestPath(BB, 0);
					break;
				}
			}
		}

		optimizeLoop(L);
	}

	bool isCallToFunc(Instruction* I, Function* F) {
		if (!isa<CallInst>(I))
			return false;
		CallInst* call = cast<CallInst>(I);
		if (call->getCalledFunction() == F)
			return true;
		return false;
	}

	bool isCallToFunc(Instruction* I, std::string FuncName) {
		if (!isa<CallInst>(I))
			return false;
		CallInst* call = cast<CallInst>(I);
		if (!call->getCalledFunction() || call->getCalledFunction()->getName() == "")
			return false;
		if (call->getCalledFunction()->getName() == FuncName)
			return true;
		return false;
	}

	int checkInstructionsInCriticalSection(std::set<Instruction*> &CSEndInsts, BasicBlock::iterator II, BasicBlock::iterator IE) {
		for (; II != IE; ++II) {
			if (CallInst *call = dyn_cast<CallInst>(II)) {
				if (isCallToFunc(call, "pthread_mutex_unlock")) {
					// found end of critical section, stop searching in this BB
					CSEndInsts.insert(call);
					return 1;
				}
				if (isInternalFunc(call->getCalledFunction())) {
					// this call is to internal function, just ignore it
					continue;
				}
				// found some unidentified function call -- consider
				// critical section as complex and do not optimize it
				return 2;
			}
			if (isa<UnreachableInst>(II)) {
				// Unreachable signifies program termination, benign case (for Swift)
				return 1;
			}
			if (isa<InvokeInst>(II)) {
				// found some bad instruction -- critical section is too complex
				return 2;
			}
		}
		// didn't find unlock func, but also critical section is not complex
		return 0;
	}

	// TODO produce better code, this is ugly as hell
	void analyzeCriticalSection(Instruction* CSStartInst) {
		// starting from CSStartInst (which is a call to pthread_mutex_lock),
		// find a corresponding call to pthread_mutex_unlock in this BB
		// or in both immediate successors -- thus we restrict search
		// only to "tiny" critical sections;
		// it is ad-hoc solution, but hopefully enough for majority of cases
		std::set<Instruction*> CSEndInsts;
		BasicBlock *BB = CSStartInst->getParent();
		BasicBlock::iterator II(CSStartInst);
		BasicBlock::iterator IE;

		// search for end of critical section in this BB
		int st = checkInstructionsInCriticalSection(CSEndInsts, std::next(II), BB->end());
		if (st == 2) return;

		if (st == 0) {
			// still didn't find, search in immediate successors
			if (succ_empty(BB)) return;
			for (succ_iterator BI = succ_begin(BB), BE = succ_end(BB); BI != BE; ++BI) {
				st = checkInstructionsInCriticalSection(CSEndInsts, BI->begin(), BI->end());
				if (st == 2) return;

				if (st == 0) {
					// didn't find in immediate successors, Ok, last try --
					// search in succs of succs (useful for Swift)
					if (succ_empty(*BI)) return;
					for (succ_iterator BBI = succ_begin(*BI), BBE = succ_end(*BI); BBI != BBE; ++BBI) {
						st = checkInstructionsInCriticalSection(CSEndInsts, BBI->begin(), BBI->end());
						if (st != 1) return;
					}
				}
			}
		}

		// finally we have found region of critical section, memorize it
		// for further optimization
		assert(!CSEndInsts.empty());
		LocksToOptimize.insert(CSStartInst);
		LocksToOptimize.insert(CSEndInsts.begin(), CSEndInsts.end());
	}

	void optimizeCriticalSections(Function &F) {
#ifdef TRANS_OPTIMIZE_CRITICALSECTIONS
		// first analyze critical sections and memorize only "tiny" ones
		for (Function::iterator BB = F.begin(), BE = F.end(); BB != BE; ++BB)
			for (auto II = BB->begin(), IE = BB->end(); II != IE; ++II) {
				if (isCallToFunc(&*II, "pthread_mutex_lock"))
					analyzeCriticalSection(&*II);
			}

		// now substitute "tiny" critical sections with HTM implementation,
		// this includes (a) removing Tx-end & Tx-start around lock/unlock, and
		// (b) substituting lock/unlock with wrappers provided by us
		for (auto lockIt = LocksToOptimize.begin(); lockIt != LocksToOptimize.end(); ++lockIt) {
			Instruction *LI = *lockIt;

			// optimize only locks surrounded by Tx-end & Tx-start
			BasicBlock::iterator II(LI);
			Instruction *prevInst = &*std::prev(II);
			if (!prevInst || !isCallToFunc(prevInst, tx_end_func))
				continue;
			Instruction *nextInst = &*std::next(II);
			if (!nextInst || !isCallToFunc(nextInst, tx_start_func))
				continue;

			// remove Tx-end and Tx-start
			prevInst->eraseFromParent();
			nextInst->eraseFromParent();

			// substitute pthread lock/unlock with our wrappers
			CallInst *CI = cast<CallInst>(LI);
			if (isCallToFunc(CI, "pthread_mutex_lock")) {
				CI->setCalledFunction(tx_pthread_mutex_lock_func);
			} else if (isCallToFunc(CI, "pthread_mutex_unlock")) {
				CI->setCalledFunction(tx_pthread_mutex_unlock_func);
			} else {
				assert(0 && "Tried to substitute function which is not lock/unlock while optimizing critical sections!");
			}
		}
#endif
	}

	void optimizeBasicBlocks(Function &F) {
#ifdef TRANS_OPTIMIZE_EMPTYTX
		for (Function::iterator BB = F.begin(), BE = F.end(); BB != BE; ++BB)
			for (BasicBlock::iterator bi = BB->begin(); bi != BB->end(); ) {
				BasicBlock::iterator nextbi = std::next(bi);

				// TX start followed by TX end? -- erase both instructions
				if (CallInst* call1 = dyn_cast<CallInst>(bi))
					if (CallInst* call2 = dyn_cast<CallInst>(nextbi))
						if (call1->getCalledFunction() == tx_start_func &&
							call2->getCalledFunction() == tx_end_func) {
							TransNum--;  // decrease statistic counter
							BasicBlock::iterator nextnextbi = std::next(nextbi);
							bi->eraseFromParent();
							nextbi->eraseFromParent();
							bi = nextnextbi;
							continue;
						}

				// TX cond start followed by TX end? -- erase cond start
				if (CallInst* call1 = dyn_cast<CallInst>(bi))
					if (CallInst* call2 = dyn_cast<CallInst>(nextbi))
						if (call1->getCalledFunction() == tx_cond_start_func &&
							call2->getCalledFunction() == tx_end_func) {
							TransNum--;  // decrease statistic counter
							BasicBlock::iterator nextnextbi = std::next(nextbi);
							bi->eraseFromParent();
							bi = nextnextbi;
							continue;
						}

				// TX start/cond_start followed by TX increment? -- erase TX increment
				if (CallInst* call1 = dyn_cast<CallInst>(bi))
					if (CallInst* call2 = dyn_cast<CallInst>(nextbi))
						if (call1->getCalledFunction() == tx_start_func ||
							call1->getCalledFunction() == tx_cond_start_func)
							if (call2->getCalledFunction() == tx_increment_func) {
								BasicBlock::iterator nextnextbi = std::next(nextbi);
								nextbi->eraseFromParent();
								bi = nextnextbi;
								continue;
							}

				// TX increment followed by TX end? -- erase TX increment
				if (CallInst* call1 = dyn_cast<CallInst>(bi))
					if (CallInst* call2 = dyn_cast<CallInst>(nextbi))
						if (call1->getCalledFunction() == tx_increment_func)
							if (call2->getCalledFunction() == tx_end_func) {
								BasicBlock::iterator nextnextbi = std::next(nextbi);
								bi->eraseFromParent();
								bi = nextnextbi;
								continue;
							}

				bi = nextbi;
			}
#endif
	}

	void visitFunction(Function& F) {
		if (isCalledFromOutside(F.getName())) {
			// caller cannot be inside Tx, so start Tx at beginning of function
			insertTxStart(&F.front().front());
		} else {
			// caller is local and thus inside Tx, start Tx only if needed
			insertCondTxStart(&F.front().front());
		}

		// first visit all loops (starting from top-level loops)
		// to calculate & insert longest path in latches of each loop
		for (auto li = LI->begin(), le = LI->end(); li != le; ++li) {
			visitLoop(*li);
		}

		// next visit all the rest, outside-of-loop BBs of this function
		LongestPaths.clear();
		ReversePostOrderTraversal<Function*> RPOT(&F);
		for (auto bi = RPOT.begin(); bi != RPOT.end(); ++bi) {
			BasicBlock* BB = *bi;
			// if BB was visited in some loop, ignore it -- it was already counted
			if (Visited.count(BB))  continue;

			initLongestPath(BB);
			visitBasicBlock(BB);
			Visited.insert(BB);

			// TODO: we can check BB succs and if one of the is loop header,
			// increment dynamic counter; but do we need such precision?
//			size_t LongestPath = LongestPaths.find(BB)->second;
		}

		optimizeCriticalSections(F);
		optimizeBasicBlocks(F);
		optimizeBasicBlocks(F);	 // in rare cases we need a second round of optimizations
	}

};


class TransactifyPass : public FunctionPass {

	public:
	static char ID; // Pass identification, replacement for typeid

	TransactifyPass(): FunctionPass(ID) { }

	virtual bool doInitialization(Module& M) {
		tx_cond_start_func           = M.getFunction("tx_cond_start");
		tx_start_func                = M.getFunction("tx_start");
		tx_end_func                  = M.getFunction("tx_end");
		tx_abort_func                = M.getFunction("tx_abort");
		tx_threshold_exceeded_func   = M.getFunction("tx_threshold_exceeded");
		tx_increment_func            = M.getFunction("tx_increment");

		tx_pthread_mutex_lock_func   = M.getFunction("tx_pthread_mutex_lock");
		tx_pthread_mutex_unlock_func = M.getFunction("tx_pthread_mutex_unlock");

		assert(tx_cond_start_func && "tx_cond_start() is not declared");
		assert(tx_start_func && "tx_start() is not declared");
		assert(tx_end_func   && "tx_end() is not declared");
		assert(tx_abort_func && "tx_abort() is not declared");
		assert(tx_threshold_exceeded_func && "tx_threshold_exceeded() is not declared");
		assert(tx_increment_func && "tx_increment() is not declared");
		assert(tx_pthread_mutex_lock_func && "tx_pthread_mutex_lock() is not declared");
		assert(tx_pthread_mutex_unlock_func && "tx_pthread_mutex_unlock() is not declared");

		return false;
	}

	virtual bool runOnFunction(Function &F) {
		// skip our helper functions
		if (isInternalFunc(&F))
			return false;

		LoopInfo& LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
		Transactifier Trans(&LI);
		Trans.visitFunction(F);

		// inform that we always modify a function
		return true;
	}

	virtual bool doFinalization(Module& M) {
		return false;
	}

	virtual void getAnalysisUsage(AnalysisUsage& AU) const {
		AU.addRequired<LoopInfoWrapperPass>();
		AU.addPreserved<LoopInfoWrapperPass>();

		FunctionPass::getAnalysisUsage(AU);
	}
};

char TransactifyPass::ID = 0;
static RegisterPass<TransactifyPass> X("tx", "Transactification (Tx) Pass");

}
