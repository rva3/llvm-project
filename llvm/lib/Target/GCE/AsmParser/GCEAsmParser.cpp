// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "MCTargetDesc/GCEEncoding.h"
#include "TargetInfo/GCETargetInfo.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
using namespace llvm;

namespace {
class GCEAsmParser : public MCTargetAsmParser {
  MCAsmParser &P;
  const MCInstrInfo &MII;
  const MCRegisterInfo &MRI;
  MCInst Inst;

  bool parseValue(SmallVectorImpl<MCOperand> &Values) {
    if (P.getTok().is(AsmToken::Hash))
      P.Lex();
    if (P.getTok().is(AsmToken::Identifier)) {
      MCRegister R = GCE::findRegister(MRI, P.getTok().getString());
      if (R) {
        Values.push_back(MCOperand::createReg(R));
        P.Lex();
        return false;
      }
    }
    const MCExpr *E;
    if (P.parseExpression(E))
      return true;
    int64_t V;
    if (E->evaluateAsAbsolute(V))
      Values.push_back(MCOperand::createImm(V));
    else
      Values.push_back(MCOperand::createExpr(E));
    return false;
  }

  bool parseMemory(SmallVectorImpl<MCOperand> &Values) {
    P.Lex(); // [
    if (P.getTok().is(AsmToken::Identifier) &&
        P.getTok().getString().equals_insensitive("subsys")) {
      P.Lex();
      if (P.parseToken(AsmToken::Equal, "expected '=' after subsys"))
        return true;
    }
    if (parseValue(Values))
      return true;
    if (P.getTok().is(AsmToken::Comma)) {
      P.Lex();
      if (parseValue(Values))
        return true;
    } else
      Values.push_back(MCOperand::createImm(0));
    return P.parseToken(AsmToken::RBrac, "expected ']' after address");
  }

  bool parseInstruction(ParseInstructionInfo &, StringRef Name, SMLoc Loc,
                        OperandVector &) override {
    std::string N = Name.lower();
    if (N == "jmp")
      N = "jump";
    Inst.clear();
    Inst.setLoc(Loc);
    SmallVector<MCOperand, 4> Values;
    // The reference uses '#' for absolute JUMP; bare numbers are relative.
    bool Relative = !P.getTok().is(AsmToken::Hash);
    if (N == "marker") {
      unsigned Flags = 0;
      while (!P.getTok().is(AsmToken::EndOfStatement)) {
        StringRef Flag = P.getTok().getString();
        unsigned Bit = Flag == "prefetch"  ? 1
                       : Flag == "inc"     ? 2
                       : Flag == "suspend" ? 4
                                           : 0;
        if (!Bit)
          return P.Error(P.getTok().getLoc(), "unknown marker flag");
        Flags |= Bit;
        P.Lex();
        if (P.getTok().is(AsmToken::Comma))
          P.Lex();
        else
          break;
      }
      Values.push_back(MCOperand::createImm(Flags));
    } else {
      while (!P.getTok().is(AsmToken::EndOfStatement)) {
        if (P.getTok().is(AsmToken::LBrac)) {
          if (parseMemory(Values))
            return true;
        } else if (parseValue(Values))
          return true;
        if (!P.getTok().is(AsmToken::Comma))
          break;
        P.Lex();
        if (P.getTok().is(AsmToken::EndOfStatement))
          return P.Error(P.getTok().getLoc(), "expected operand after comma");
      }
    }
    if (P.parseToken(AsmToken::EndOfStatement,
                     "unexpected token after instruction"))
      return true;
    if (N == "nop") {
      if (!Values.empty())
        return P.Error(Loc, "nop takes no operands");
      N = "jump";
      Relative = true;
      Values.push_back(MCOperand::createImm(8));
    }
    if (N == "mov") {
      if (Values.size() != 2)
        return P.Error(Loc, "mov requires two operands");
      if (Values[1].isReg()) {
        N = "add";
        Values.push_back(MCOperand::createImm(0));
      } else {
        N = Values[1].isImm() && uint64_t(Values[1].getImm()) > 0xffffffffULL &&
                    Values[1].getImm() >= 0
                ? "move"
                : "ldi";
      }
    }
    std::string Opcode = StringRef(N).upper();
    if (N == "add" || N == "sub" || N == "mul" || N == "xor" || N == "orr" ||
        N == "and" || N == "lsl" || N == "lsr") {
      if (Values.size() != 3)
        return P.Error(Loc, "binary operation requires three operands");
      Opcode += Values[1].isReg() ? 'r' : 'i';
      Opcode += Values[2].isReg() ? 'r' : 'i';
    } else if (N == "not") {
      if (Values.size() != 2)
        return P.Error(Loc, "not requires two operands");
      Opcode += Values[1].isReg() ? 'r' : 'i';
    } else if (N == "read" || N == "read_s") {
      if (Values.size() != 3)
        return P.Error(Loc, "read requires destination and bracketed address");
      Opcode += Values[1].isReg() ? 'r' : 's';
    } else if (N == "write" || N == "mwrite" || N == "write_s" ||
               N == "mwrite_s") {
      if (Values.size() != 3)
        return P.Error(Loc, "write requires bracketed address and value");
      Opcode += Values[0].isReg() ? 'r' : 's';
      Opcode += Values[2].isReg() ? 'r' : 'i';
    } else if (N == "jump") {
      if (Values.size() != 1)
        return P.Error(Loc, "jump requires one target");
      if (Values[0].isReg())
        return P.Error(Loc, "register JUMP is unreliable; use jeq reg, #0, #0");
      Opcode += Relative ? "rel" : "abs";
    } else if (N == "jeq" || N == "jne" || N == "jge" || N == "jle" ||
               N == "jgt" || N == "jlt") {
      if (Values.size() != 3)
        return P.Error(Loc,
                       "conditional jump requires target and two operands");
      Opcode += Values[0].isReg() ? "abs" : "rel";
      Opcode += Values[1].isReg() ? 'r' : 'i';
      Opcode += Values[2].isReg() ? 'r' : 'i';
    } else if (N != "ldi" && N != "move" && N != "mask" && N != "eoc" &&
               N != "marker") {
      return P.Error(Loc, "unknown GCE instruction");
    }
    unsigned ID = GCE::findOpcode(MII, Opcode);
    if (ID == ~0u)
      return P.Error(Loc, "unknown GCE instruction");
    Inst.setOpcode(ID);
    for (auto V : Values)
      Inst.addOperand(V);
    if (const char *Error = GCE::validate(Inst, MII, MRI))
      return P.Error(Loc, Error);
    return false;
  }
  bool matchAndEmitInstruction(SMLoc, unsigned &Opcode, OperandVector &,
                               MCStreamer &Out, uint64_t &, bool) override {
    Opcode = Inst.getOpcode();
    Out.emitInstruction(Inst, getSTI());
    return false;
  }
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &Start,
                               SMLoc &End) override {
    if (!P.getTok().is(AsmToken::Identifier))
      return ParseStatus::NoMatch;
    Reg = GCE::findRegister(MRI, P.getTok().getString());
    if (!Reg)
      return ParseStatus::NoMatch;
    Start = P.getTok().getLoc();
    End = P.getTok().getEndLoc();
    P.Lex();
    return ParseStatus::Success;
  }
  bool parseRegister(MCRegister &Reg, SMLoc &Start, SMLoc &End) override {
    if (tryParseRegister(Reg, Start, End).isSuccess())
      return false;
    return P.Error(P.getTok().getLoc(), "expected GCE register");
  }
  void convertToMapAndConstraints(unsigned, const OperandVector &) override {
    P.Error(P.getTok().getLoc(),
            "GCE inline-assembly constraints are not implemented");
  }

public:
  GCEAsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
               const MCInstrInfo &Info, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, STI, Info), P(Parser), MII(Info),
        MRI(*Parser.getContext().getRegisterInfo()) {
    MCAsmParserExtension::Initialize(Parser);
    setAvailableFeatures(STI.getFeatureBits());
  }
};
} // namespace
extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeGCEAsmParser() {
  RegisterMCAsmParser<GCEAsmParser> X(getTheGCETarget());
}
