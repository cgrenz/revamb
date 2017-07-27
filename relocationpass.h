#ifndef _RELOCATIONPASS_H
#define _RELOCATIONPASS_H

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// Standard includes
#include <cstdint>
#include <vector>

// LLVM includes
#include "llvm/ADT/Optional.h"

// Local includes
#include "binaryfile.h"
#include "datastructures.h"
#include "ir-helpers.h"
#include "revamb.h"

// Forward declarations
namespace llvm {
class BasicBlock;
class Function;
class Instruction;
class LLVMContext;
class Module;
class SwitchInst;
class StoreInst;
class Value;
}

class JumpTargetManager;

/// \brief Transform writes from relocations to the PC in jumps
///
/// This pass looks for all writes to the PC that originate from a 
/// load from a relocation and replaces it with *something*.
class TranslateRelocationCallsPass : public llvm::BasicBlockPass {
public:
  static char ID;

  TranslateRelocationCallsPass() : llvm::BasicBlockPass(ID),
    MDKindId(0),
    JTM(nullptr), Relocations(nullptr) { }

  TranslateRelocationCallsPass(JumpTargetManager *JTM, const std::vector<RelocationInfo> *Relocations) :
    BasicBlockPass(ID),
    MDKindId(0),
    JTM(JTM),
    Relocations(Relocations) { }

  void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;

  bool runOnBasicBlock(llvm::BasicBlock &BB) override;

private:
  bool handleLoad(llvm::LoadInst &Load);
  bool handlePCStore(llvm::StoreInst &Store);
  void buildCall(llvm::StoreInst &Store, llvm::StringRef Name);
  
  unsigned int MDKindId;
  JumpTargetManager *JTM;
  const std::vector<RelocationInfo> *Relocations;
};


#endif // _JUMPTARGETMANAGER_H
