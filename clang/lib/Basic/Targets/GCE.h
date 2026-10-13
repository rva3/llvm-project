// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_GCE_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_GCE_H
#include "clang/Basic/MacroBuilder.h"
#include "clang/Basic/TargetInfo.h"
namespace clang::targets {
class GCETargetInfo : public TargetInfo {
public:
  GCETargetInfo(const llvm::Triple &T, const TargetOptions &) : TargetInfo(T) {
    TLSSupported = false;
    LongWidth = LongAlign = PointerWidth = PointerAlign = 32;
    LongLongWidth = LongLongAlign = 64;
    DoubleWidth = DoubleAlign = LongDoubleWidth = LongDoubleAlign = 64;
    LongDoubleFormat = &llvm::APFloat::IEEEdouble();
    SuitableAlign = 64;
    SizeType = UnsignedInt;
    PtrDiffType = SignedInt;
    IntPtrType = SignedInt;
    IntMaxType = SignedLongLong;
    Int64Type = SignedLongLong;
    MaxAtomicPromoteWidth = MaxAtomicInlineWidth = 0;
    resetDataLayout();
  }
  void getTargetDefines(const LangOptions &, MacroBuilder &B) const override {
    B.defineMacro("__gce__");
    B.defineMacro("__GCE__");
    B.defineMacro("__GCE_ABI__", "1");
    B.defineMacro("__SOFTFP__", "1");
  }
  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override {
    return {};
  }
  ArrayRef<const char *> getGCCRegNames() const override { return {}; }
  ArrayRef<GCCRegAlias> getGCCRegAliases() const override { return {}; }
  bool validateAsmConstraint(const char *&, ConstraintInfo &) const override {
    return false;
  }
  std::string_view getClobbers() const override { return ""; }
  BuiltinVaListKind getBuiltinVaListKind() const override {
    return CharPtrBuiltinVaList;
  }
  bool hasFeature(StringRef F) const override { return F == "gce"; }
};
} // namespace clang::targets
#endif
