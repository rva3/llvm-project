// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "GCETargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
using namespace llvm;
Target &llvm::getTheGCETarget() {
  static Target T;
  return T;
}
extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeGCETargetInfo() {
  RegisterTarget<Triple::gce> X(getTheGCETarget(), "gce", "MediaTek GCE v3",
                                "GCE");
}
