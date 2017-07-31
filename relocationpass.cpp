/// \file relocationpass.cpp
/// \brief This file handles relocations for dynamically linked symbols

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// Standard includes
#include <cassert>
#include <cstdint>
#include <fstream>
#include <queue>
#include <sstream>

// Boost includes
#include <boost/icl/interval_set.hpp>
#include <boost/type_traits/is_same.hpp>
#include <boost/icl/right_open_interval.hpp>

// LLVM includes
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Endian.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/Cloning.h"

// Local includes
#include "datastructures.h"
#include "debug.h"
#include "generatedcodebasicinfo.h"
#include "ir-helpers.h"
#include "jumptargetmanager.h"
#include "revamb.h"
#include "set.h"
#include "simplifycomparisons.h"
#include "subgraph.h"

using namespace llvm;

char LocateRelocationAccessesPass::ID = 0;

char AddRelocationCallsPass::ID = 0;

static RegisterPass<LocateRelocationAccessesPass> X1("locate-relocations",
                                                   "Locate Relocation Accesses"
                                                   " Pass",
                                                   false,
                                                   false);

static RegisterPass<AddRelocationCallsPass> X2("add-relocation-calls",
                                                   "Add Relocation Calls"
                                                   " Pass",
                                                   false,
                                                   false);


void LocateRelocationAccessesPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.setPreservesAll();
}

bool LocateRelocationAccessesPass::runOnBasicBlock(llvm::BasicBlock &BB) {
  bool Result = false;
  MDKindId = BB.getContext().getMDKindID("revamb.relocation");

  for (auto &I: BB) {
    if (auto *Load = dyn_cast<LoadInst>(&I)) {
      Result |= handleLoad(*Load);
    }
  }

  return Result;
}

bool LocateRelocationAccessesPass::handleLoad(llvm::LoadInst &Load) {
  LLVMContext &Ctx = Load.getParent()->getContext();
  auto *ConstExpr = dyn_cast<llvm::ConstantExpr>(Load.getPointerOperand());
  if (!ConstExpr || ConstExpr->getOpcode() != Instruction::IntToPtr) {
    return false;
  }
  auto *ConstInt = dyn_cast<ConstantInt>(ConstExpr->getOperand(0));
  if (!ConstInt) {
    return false;
  }
  for (const auto &Reloc : *Relocations) {
    if (ConstInt->equalsInt(Reloc.Address)) {
      if (!Load.getMetadata(MDKindId)) {

        // Tag relocation loads with metadata
        auto *Int64 = IntegerType::get(Ctx, 64);
        auto *Meta = MDNode::get(Ctx, {
          MDString::get(Ctx, Reloc.Name),
          ConstantAsMetadata::get(ConstantInt::get(Int64, Reloc.Addend))
        });
        Load.setMetadata(MDKindId, Meta);

        return true;
      }
    }
  }
  return false;
}

void AddRelocationCallsPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addUsedIfAvailable<LocateRelocationAccessesPass>();
  AU.setPreservesAll();
}

bool AddRelocationCallsPass::runOnModule(llvm::Module &M) {
  MadeChange = false;
  MDKindId = M.getContext().getMDKindID("revamb.relocation");

  visit(M);

  return MadeChange;
}

void AddRelocationCallsPass::visitStoreInst(llvm::StoreInst &I) {
  if (!JTM->isPCReg(I.getPointerOperand())) {
    return;
  }
  if (auto *Load = dyn_cast<LoadInst>(I.getValueOperand())) {
    StringRef RelocName = getRelocName(*Load, true);
    if (!RelocName.empty()) {
      std::string Name = "dl." + RelocName.str();
      if (!succeededByCall(I, Name)) {
        buildCall(I, Name);
        MadeChange = true;
      }
    }
  }
}

StringRef AddRelocationCallsPass::getRelocName(const llvm::Instruction &I, bool needsZeroAddend) {
  const auto *MDNode = I.getMetadata(MDKindId);
  if (MDNode) {
    StringRef RelocName = dyn_cast<MDString>(MDNode->getOperand(0))->getString();
    const auto *RelocAddend = dyn_cast<ConstantAsMetadata>(MDNode->getOperand(1));

    if (!needsZeroAddend || dyn_cast<ConstantInt>(RelocAddend->getValue())->isZero()) {
      return RelocName;
    }
  }
  return {};
}

bool AddRelocationCallsPass::succeededByCall(const llvm::Instruction &I, llvm::StringRef Function) {
  const auto *Call = dyn_cast<CallInst>(I.getNextNode());
  if (Call) {
    const auto *F = Call->getCalledFunction();
    return (F && (Function.equals(F->getName())));
  } else {
    return false;
  }
}

void AddRelocationCallsPass::buildCall(llvm::StoreInst &I, StringRef Name) {
  Module *M = I.getParent()->getModule();
  LLVMContext &Ctx = M->getContext();
  auto *FType = FunctionType::get(Type::getVoidTy(Ctx), false);

  // Get or create function
  auto *Value = M->getOrInsertFunction(Name, FType);
  auto *Func = dyn_cast<Function>(Value);
  if (Func && Func->isDeclaration()) {
    BasicBlock *Entry = BasicBlock::Create(Ctx, "", Func);
    IRBuilder<> Builder(Entry);
    Builder.CreateRetVoid();
  }

  // Insert call
  auto *NextNode = I.getNextNode();
  assert(NextNode != nullptr);
  IRBuilder<> Builder(I.getNextNode());
  std::string FName = "dl." + Name.str();
  Builder.CreateCall(Value, {});
}
