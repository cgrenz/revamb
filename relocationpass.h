#ifndef _RELOCATIONPASS_H
#define _RELOCATIONPASS_H

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// Standard includes
#include <cstdint>
#include <vector>

// LLVM includes
#include "llvm/IR/InstVisitor.h"
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

/// \brief Finds and tags accesses to dynamic linking relocations
///
/// This pass looks for loads from relocation addresses and tags them with
/// "revamb.relocation" metadata
class LocateRelocationAccessesPass final : public llvm::BasicBlockPass {
public:
  static char ID;

  LocateRelocationAccessesPass() : llvm::BasicBlockPass(ID),
    MDKindId(0),
    Relocations(nullptr) { }

  LocateRelocationAccessesPass(const std::vector<RelocationInfo> *Relocations) :
    BasicBlockPass(ID),
    MDKindId(0),
    Relocations(Relocations) { }

  void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;

  bool runOnBasicBlock(llvm::BasicBlock &BB) override;

private:
  bool handleLoad(llvm::LoadInst &Load);

  unsigned int MDKindId;
  const std::vector<RelocationInfo> *Relocations;
};

/// \brief Adds calls for PC writes from dynamic linking relocations
///
/// This pass looks for writes to the PC that originate from relocation loads
/// and adds a call to "dl.{symbol name}".
class AddRelocationCallsPass final : public llvm::ModulePass, public llvm::InstVisitor<AddRelocationCallsPass> {
public:
  static char ID;

  AddRelocationCallsPass() : llvm::ModulePass(ID),
    MDKindId(0),
    JTM(nullptr) { }

  AddRelocationCallsPass(JumpTargetManager *JTM) :
    llvm::ModulePass(ID),
    MDKindId(0),
    JTM(JTM) {}

  void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;

  bool runOnModule(llvm::Module &M) override;
  void visitStoreInst(llvm::StoreInst &I);

private:
  void buildCall(llvm::StoreInst &I, llvm::StringRef Name);
  bool succeededByCall(const llvm::Instruction &I, llvm::StringRef Function);
  llvm::StringRef getRelocName(const llvm::Instruction &I, bool needsZeroAddend);
  
  unsigned int MDKindId;
  bool MadeChange;
  JumpTargetManager *JTM;
};

/// \brief Adds global metadata for all linked dynamic libraries
///
/// This pass adds llvm.linker.options metadata to list
/// all referenced dynamic libraries.
class AddLibraryMetadataPass final : public llvm::ModulePass {
public:
  static char ID;

  AddLibraryMetadataPass() : llvm::ModulePass(ID),
    Libraries(nullptr) { }

  AddLibraryMetadataPass(const std::vector<llvm::StringRef> *Libraries) :
    llvm::ModulePass(ID),
    Libraries(Libraries) {}

  void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;

  bool runOnModule(llvm::Module &M) override;

private:
  const std::vector<llvm::StringRef> *Libraries;
};


#endif // _RELOCATIONPASS_H
