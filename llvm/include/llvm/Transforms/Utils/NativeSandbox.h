//===-- NativeSandbox.h -----------------------------------------*- C++ -*-===//
//
// Encapsulates all native sandbox LLVM bitcode manipulations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_NATIVESANDBOX_H
#define LLVM_TRANSFORMS_UTILS_NATIVESANDBOX_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class NativeSandboxPass : public PassInfoMixin<NativeSandboxPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  PreservedAnalyses run(Module &F, ModuleAnalysisManager &AM);

  static bool isRequired() { return true; }

};

} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_NATIVESANDBOX_H
