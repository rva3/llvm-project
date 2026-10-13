// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "GCE.h"
#include "TargetInfo/GCETargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
using namespace llvm;
namespace {
class GCEAsmPrinter : public AsmPrinter {
  MCOperand reg(unsigned R) { return MCOperand::createReg(R); }
  MCOperand imm(int64_t V) { return MCOperand::createImm(V); }
  MCOperand expr(MCSymbol *S, int64_t Offset = 0) {
    const MCExpr *E = MCSymbolRefExpr::create(S, OutContext);
    if (Offset)
      E = MCBinaryExpr::createAdd(E, MCConstantExpr::create(Offset, OutContext),
                                  OutContext);
    return MCOperand::createExpr(E);
  }
  MCOperand lower(const MachineOperand &O) {
    if (O.isReg())
      return reg(O.getReg());
    if (O.isImm())
      return imm(O.getImm());
    if (O.isMBB())
      return expr(O.getMBB()->getSymbol());
    if (O.isGlobal())
      return expr(getSymbol(O.getGlobal()), O.getOffset());
    if (O.isSymbol())
      return expr(GetExternalSymbolSymbol(O.getSymbolName()), O.getOffset());
    if (O.isBlockAddress())
      return expr(GetBlockAddressSymbol(O.getBlockAddress()), O.getOffset());
    if (O.isCPI())
      return expr(GetCPISymbol(O.getIndex()), O.getOffset());
    report_fatal_error("unsupported GCE machine operand");
  }
  void emit(unsigned Op, std::initializer_list<MCOperand> Operands) {
    MCInst M;
    M.setOpcode(Op);
    for (auto &O : Operands)
      M.addOperand(O);
    EmitToStreamer(*OutStreamer, M);
  }
  void copy(unsigned D, unsigned S) {
    emit(GCE::ADDri, {reg(D), reg(S), imm(0)});
  }

public:
  GCEAsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> S)
      : AsmPrinter(TM, std::move(S)) {}
  StringRef getPassName() const override { return "GCE assembly printer"; }
  void emitInstruction(const MachineInstr *M) override {
    auto R = [&](unsigned I) { return M->getOperand(I).getReg().id(); };
    auto O = [&](unsigned I) { return lower(M->getOperand(I)); };
    switch (M->getOpcode()) {
    case GCE::STOREN: {
      auto Saved = [&](unsigned I) {
        return reg(R(I) == GCE::R0 ? GCE::SPR1 : R(I));
      };
      copy(GCE::SPR1, GCE::R0);
      emit(GCE::ANDri, {reg(GCE::SPR0), Saved(1), imm(3)});
      emit(GCE::LSLri, {reg(GCE::SPR0), reg(GCE::SPR0), imm(3)});
      emit(GCE::LDI, {reg(GCE::R13), imm(0xfffffffc)});
      emit(GCE::ANDrr, {reg(GCE::R13), Saved(1), reg(GCE::R13)});
      emit(GCE::READr, {reg(GCE::R0), reg(GCE::R13), imm(0)});
      emit(GCE::LDI, {reg(GCE::R13), O(2)});
      emit(GCE::LSLrr, {reg(GCE::R13), reg(GCE::R13), reg(GCE::SPR0)});
      emit(GCE::NOTr, {reg(GCE::R13), reg(GCE::R13)});
      emit(GCE::ANDrr, {reg(GCE::R0), reg(GCE::R0), reg(GCE::R13)});
      emit(GCE::ANDri, {reg(GCE::R13), Saved(0), O(2)});
      emit(GCE::LSLrr, {reg(GCE::R13), reg(GCE::R13), reg(GCE::SPR0)});
      emit(GCE::ORRrr, {reg(GCE::R0), reg(GCE::R0), reg(GCE::R13)});
      emit(GCE::LDI, {reg(GCE::R13), imm(0xfffffffc)});
      emit(GCE::ANDrr, {reg(GCE::R13), Saved(1), reg(GCE::R13)});
      emit(GCE::WRITErr, {reg(GCE::R13), imm(0), reg(GCE::R0)});
      copy(GCE::R0, GCE::SPR1);
      return;
    }
    case GCE::FRAMEADDR:
      emit(GCE::LDI, {reg(GCE::SPR0), O(2)});
      emit(GCE::ADDrr, {O(0), O(1), reg(GCE::SPR0)});
      return;
    case GCE::LOAD32:
    case GCE::STORE32:
      emit(GCE::LDI, {reg(GCE::SPR0), O(2)});
      emit(GCE::ADDrr, {reg(GCE::R13), O(1), reg(GCE::SPR0)});
      if (M->getOpcode() == GCE::LOAD32) {
        emit(GCE::READr, {reg(GCE::R13), reg(GCE::R13), imm(0)});
        copy(R(0), GCE::R13);
      } else {
        // OLD WRITE requires two GPRs. Preserve allocatable R0 in reserved
        // SPR1.
        copy(GCE::SPR1, GCE::R0);
        copy(GCE::R0, R(0));
        emit(GCE::WRITErr, {reg(GCE::R13), imm(0), reg(GCE::R0)});
        copy(GCE::R0, GCE::SPR1);
      }
      return;
    case GCE::BCC: {
      // Hardware requires backward transfers to use OLD JUMP. Invert the
      // condition and skip a single absolute OLD JUMP, regardless of layout.
      static const unsigned Inverse[] = {GCE::JNErelrr, GCE::JEQrelrr,
                                         GCE::JLTrelrr, GCE::JGTrelrr,
                                         GCE::JLErelrr, GCE::JGErelrr};
      emit(Inverse[M->getOperand(3).getImm()], {imm(16), O(1), O(2)});
      emit(GCE::JUMPabs, {O(0)});
      return;
    }
    case GCE::CALL:
    case GCE::CALLR: {
      auto *Return = OutContext.createTempSymbol("gce_return");
      if (M->getOpcode() == GCE::CALLR)
        copy(GCE::R13, R(0));
      emit(GCE::LDI, {reg(GCE::R15), expr(Return)});
      if (M->getOpcode() == GCE::CALL)
        emit(GCE::JUMPabs, {O(0)});
      else
        emit(GCE::JUMPabs, {expr(GetExternalSymbolSymbol("__gce_indirect"))});
      OutStreamer->emitLabel(Return);
      return;
    }
    case GCE::RET:
      copy(GCE::R13, GCE::R15);
      emit(GCE::JUMPabs, {expr(GetExternalSymbolSymbol("__gce_indirect"))});
      return;
    default:
      break;
    }
    MCInst Inst;
    Inst.setOpcode(M->getOpcode());
    for (auto &A : M->operands()) {
      if (A.isRegMask() || (A.isReg() && A.isImplicit()))
        continue;
      Inst.addOperand(lower(A));
    }
    EmitToStreamer(*OutStreamer, Inst);
  }
};
} // namespace
extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeGCEAsmPrinter() {
  RegisterAsmPrinter<GCEAsmPrinter> X(getTheGCETarget());
}
