/// \file jumptargetmanager.cpp
/// \brief This file handles the possible jump targets encountered during
///        translation and the creation and management of the respective
///        BasicBlock.

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

char TranslateRelocationCallsPass::ID = 0;

static RegisterPass<TranslateRelocationCallsPass> X("translate-reloc",
                                                   "Translate Relocation Calls"
                                                   " Pass",
                                                   false,
                                                   false);


void TranslateRelocationCallsPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addUsedIfAvailable<SETPass>();
  AU.setPreservesAll();
}

bool TranslateRelocationCallsPass::runOnBasicBlock(llvm::BasicBlock &BB) {
  bool Result = false;
  MDKindId = JTM->getContext().getMDKindID("revamb.relocation");
  
  for (auto &I: BB) {
    if (auto *Load = dyn_cast<LoadInst>(&I)) {
      Result |= handleLoad(*Load);
    }
  }

  return Result;
}

bool TranslateRelocationCallsPass::handleLoad(llvm::LoadInst &Load) {
  LLVMContext &Ctx = JTM->getContext();
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
        
        for (auto *User : Load.users()) {
          auto *Store = dyn_cast<StoreInst>(User);
          if (Store && JTM->isPCReg(Store->getPointerOperand())) {
            handlePCStore(*Store);
          }
        }
      
        return true;
      }
    }
  }
  return false;
}

bool TranslateRelocationCallsPass::handlePCStore(llvm::StoreInst &Store) {
  if (auto *Load = dyn_cast<LoadInst>(Store.getValueOperand())) {
    auto *MDNode = Load->getMetadata(MDKindId);
    if (MDNode) {
      StringRef RelocName = dyn_cast<MDString>(MDNode->getOperand(0))->getString();
      auto *RelocAddend = dyn_cast<ConstantAsMetadata>(MDNode->getOperand(1));
      bool ZeroAddend = dyn_cast<ConstantInt>(RelocAddend->getValue())->isZero();
      
      if (ZeroAddend) {
        buildCall(Store, RelocName);
      }
      
    }
  }
  return false;
}

void TranslateRelocationCallsPass::buildCall(llvm::StoreInst &Store, StringRef Name) {
  LLVMContext &Ctx = JTM->getContext();
  Module *M = Store.getParent()->getModule();
  
  auto *FType = FunctionType::get(Type::getVoidTy(Ctx), true);
  
  IRBuilder<> Builder(&Store);
  auto *Callee = M->getOrInsertFunction(Name, FType);
  
  Builder.CreateCall(Callee, {});
  
}
