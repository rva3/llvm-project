// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "ABIInfoImpl.h"
#include "TargetInfo.h"
#include "clang/AST/RecursiveASTVisitor.h"
using namespace clang;
using namespace clang::CodeGen;
namespace {
bool unsupportedFloat(QualType T) {
  return T->isRealFloatingType() &&
         !T->isSpecificBuiltinType(BuiltinType::Float) &&
         !T->isSpecificBuiltinType(BuiltinType::Double) &&
         !T->isSpecificBuiltinType(BuiltinType::LongDouble);
}
class GCEABIInfo : public DefaultABIInfo {
public:
  explicit GCEABIInfo(CodeGenTypes &T) : DefaultABIInfo(T) {}

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override {
    // Unnamed arguments live entirely on the stack, with four-byte slots
    // and eight-byte alignment for i64/f64. No register save area is needed.
    return emitVoidPtrVAArg(CGF, VAListAddr, Ty, /*IsIndirect=*/false,
                            getContext().getTypeInfoInChars(Ty),
                            CharUnits::fromQuantity(4),
                            /*AllowHigherAlign=*/true, Slot);
  }
};
class GCEUnsupportedVisitor
    : public RecursiveASTVisitor<GCEUnsupportedVisitor> {
  DiagnosticsEngine &Diags;
  const LangOptions &LangOpts;
  bool Failed = false;
  bool reject(SourceLocation L, StringRef Text) {
    if (!Failed) {
      unsigned ID = Diags.getCustomDiagID(DiagnosticsEngine::Error, "GCE: %0");
      Diags.Report(L, ID) << Text;
      Failed = true;
    }
    return false;
  }

public:
  explicit GCEUnsupportedVisitor(DiagnosticsEngine &D, const LangOptions &LO)
      : Diags(D), LangOpts(LO) {}
  bool VisitAtomicExpr(AtomicExpr *E) {
    return reject(E->getExprLoc(), "atomics are unsupported");
  }
  bool VisitVAArgExpr(VAArgExpr *E) {
    if (E->getType()->isRecordType() || E->getType()->isAnyComplexType())
      return reject(E->getExprLoc(),
                    "aggregate va_arg values are unsupported; use pointers or "
                    "standard scalar promotions (float varargs use double)");
    return true;
  }
  bool VisitExpr(Expr *E) {
    auto FP = E->getFPFeaturesInEffect(LangOpts);
    if (FP.getAllowFEnvAccess() || FP.getRoundingMath() ||
        FP.getExceptionMode() != LangOptions::FPE_Ignore)
      return reject(E->getExprLoc(),
                    "strict floating-point environment is unsupported");
    if (E->getType()->isAtomicType())
      return reject(E->getExprLoc(), "atomics are unsupported");
    if (unsupportedFloat(E->getType()))
      return reject(E->getExprLoc(),
                    "only float, double and long double floating types are supported");
    if (E->getType()->isAnyComplexType())
      return reject(E->getExprLoc(), "complex operations are unsupported");
    return true;
  }
  bool VisitGCCAsmStmt(GCCAsmStmt *S) {
    return reject(S->getBeginLoc(), "user inline assembly is unsupported");
  }
  bool VisitMSAsmStmt(MSAsmStmt *S) {
    return reject(S->getBeginLoc(), "user inline assembly is unsupported");
  }
  bool VisitVarDecl(VarDecl *D) {
    if (D->getType()->isVariablyModifiedType())
      return reject(D->getLocation(),
                    "VLA/dynamic stack allocation is unsupported");
    return true;
  }
  bool VisitCallExpr(CallExpr *E) {
    // A call through a function pointer has no FunctionDecl on which to run
    // setTargetAttributes. Check its signature before backend ABI lowering.
    auto CheckType = [&](QualType T) {
      if (T->isRecordType() || T->isAnyComplexType())
        return reject(
            E->getExprLoc(),
            "aggregate arguments and returns by value are unsupported; "
            "use pointers");
      if (unsupportedFloat(T))
        return reject(E->getExprLoc(),
                      "only float, double and long double floating types are supported");
      return true;
    };
    if (!CheckType(E->getType()))
      return false;
    QualType CalleeTy = E->getCallee()->getType();
    if (CalleeTy->isPointerType())
      CalleeTy = CalleeTy->getPointeeType();
    if (const auto *Proto = CalleeTy->getAs<FunctionProtoType>()) {
      for (QualType T : Proto->param_types())
        if (!CheckType(T))
          return false;
    }
    // Also cover arguments to calls without a prototype.
    for (const Expr *A : E->arguments())
      if (!CheckType(A->getType()))
        return false;
    if (const FunctionDecl *F = E->getDirectCallee()) {
      StringRef Name = F->getName();
      if (Name.consume_front("__builtin_")) {
        for (StringRef Base : {"fma", "sqrt", "sin", "cos", "pow", "exp",
                               "exp2", "log", "log2", "log10", "floor", "ceil",
                               "trunc", "round", "roundeven", "nearbyint",
                               "rint", "fmod", "fmin", "fmax"})
          if (Name == Base ||
              (Name.size() == Base.size() + 1 && Name.starts_with(Base) &&
               (Name.ends_with("f") || Name.ends_with("l"))))
            return reject(E->getExprLoc(),
                          "math builtins require an external math library; call its functions directly");
      }
      if (F->getName() == "__builtin_alloca" ||
          F->getName() == "__builtin_alloca_with_align")
        return reject(E->getExprLoc(),
                      "dynamic stack allocation is unsupported");
    }
    return true;
  }
};
class GCETargetCodeGenInfo : public TargetCodeGenInfo {
public:
  explicit GCETargetCodeGenInfo(CodeGenTypes &T)
      : TargetCodeGenInfo(std::make_unique<GCEABIInfo>(T)) {}
  void setTargetAttributes(const Decl *D, llvm::GlobalValue *G,
                           CodeGenModule &M) const override {
    if (!D)
      return;
    auto Error = [&](StringRef Text) {
      unsigned ID =
          M.getDiags().getCustomDiagID(DiagnosticsEngine::Error, "GCE: %0");
      M.getDiags().Report(D->getLocation(), ID) << Text;
    };
    if (M.getLangOpts().CPlusPlus)
      Error("C++ is not supported by the initial ABI");
    if (const auto *F = dyn_cast<FunctionDecl>(D)) {
      auto Check = [&](QualType T) {
        if (T->isRecordType() || T->isAnyComplexType())
          Error("aggregate arguments and returns by value are unsupported; use "
                "pointers");
        if (unsupportedFloat(T))
          Error("only float, double and long double floating types are supported");
      };
      Check(F->getReturnType());
      for (auto *P : F->parameters())
        Check(P->getType());
      if (F->doesThisDeclarationHaveABody())
        GCEUnsupportedVisitor(M.getDiags(), M.getLangOpts())
            .TraverseStmt(F->getBody());
    }
  }
};
} // namespace
std::unique_ptr<TargetCodeGenInfo>
CodeGen::createGCETargetCodeGenInfo(CodeGenModule &M) {
  return std::make_unique<GCETargetCodeGenInfo>(M.getTypes());
}
