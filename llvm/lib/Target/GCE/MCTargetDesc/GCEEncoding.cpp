// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "GCEEncoding.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/MathExtras.h"
using namespace llvm;

unsigned GCE::findOpcode(const MCInstrInfo &MII, StringRef Name) {
  for (unsigned I = 0; I < MII.getNumOpcodes(); ++I)
    if (MII.getName(I) == Name)
      return I;
  return ~0u;
}

MCRegister GCE::findRegister(const MCRegisterInfo &MRI, StringRef Name) {
  std::string N = Name.lower();
  if (StringRef(N).starts_with("gpr"))
    N = "r" + N.substr(3);
  else if (N.size() == 2 && N[0] == 's' && N[1] >= '0' && N[1] <= '3')
    N = "spr" + N.substr(1);
  for (unsigned I = 1; I < MRI.getNumRegs(); ++I)
    if (StringRef(MRI.getName(I)).equals_insensitive(N))
      return MCRegister(I);
  return MCRegister();
}

StringRef GCE::mnemonic(uint64_t F) {
  static const char *Logic[] = {"ldi", "add", "sub", "mul", "",    "",   "", "",
                                "xor", "not", "orr", "and", "lsl", "lsr"};
  static const char *Cond[] = {"jeq", "jne", "jge", "jle", "jgt", "jlt"};
  unsigned Op = F & 255, Sub = (F >> 8) & 31, Form = (F >> 16) & 15;
  switch (Form) {
  case Binary:
  case Assign:
  case Not:
    return Logic[Sub];
  case Read:
    return "read";
  case Write:
    return "write";
  case MaskedWrite:
    return "mwrite";
  case ReadS:
    return "read_s";
  case WriteS:
    return Op == 0x91 ? "mwrite_s" : "write_s";
  case Jump:
    return "jump";
  case JumpC:
    return Cond[Sub];
  case Mask:
    return "mask";
  case Move:
    return "move";
  case Eoc:
    return "eoc";
  case Marker:
    return "marker";
  }
  llvm_unreachable("invalid GCE instruction format");
}

const char *GCE::validate(const MCInst &MI, const MCInstrInfo &MII,
                          const MCRegisterInfo &MRI) {
  uint64_t F = MII.get(MI.getOpcode()).TSFlags;
  unsigned Form = (F >> 16) & 15;
  bool L = F & (1 << 20), R = F & (1 << 21), A = F & (1 << 22);
  auto Reg = [&](unsigned I, unsigned Kind) {
    if (!MI.getOperand(I).isReg())
      return false;
    unsigned E = MRI.getEncodingValue(MI.getOperand(I).getReg());
    if (Kind == 1)
      return E >= 32 && E < 48;
    if (Kind == 2)
      return E < 4;
    if (Kind == 3)
      return (E >= 32 && E < 48) || (E >= 272 && E < 280);
    return E < 4 || (E >= 32 && E < 48) || E == 56 || E == 59 ||
           (E >= 0x8000 && E < 0x8040);
  };
  auto Imm = [&](unsigned I, unsigned Bits) {
    return MI.getOperand(I).isImm() && isUIntN(Bits, MI.getOperand(I).getImm());
  };
  auto U32 = [&](unsigned I) {
    return MI.getOperand(I).isExpr() ||
           (MI.getOperand(I).isImm() &&
            (isUInt<32>(MI.getOperand(I).getImm()) ||
             isInt<32>(MI.getOperand(I).getImm())));
  };
  auto Value = [&](unsigned I, bool IsReg) {
    return IsReg ? Reg(I, 0) : Imm(I, 16);
  };
  auto Relative = [&](unsigned I, unsigned Bits) {
    return MI.getOperand(I).isExpr() ||
           (MI.getOperand(I).isImm() &&
            isIntN(Bits, MI.getOperand(I).getImm()) &&
            !(MI.getOperand(I).getImm() & 7));
  };
  unsigned Count = Form == Binary || Form == Read || Form == Write ||
                           Form == MaskedWrite || Form == ReadS ||
                           Form == WriteS || Form == JumpC
                       ? 3
                   : Form == Assign || Form == Not || Form == Move ? 2
                   : Form == Eoc                                   ? 0
                                                                   : 1;
  if (MI.getNumOperands() != Count)
    return "incorrect operand count";
  switch (Form) {
  case Binary:
    if (!Reg(0, 0) || !Value(1, L) || !Value(2, R))
      return "expected unified registers or unsigned 16-bit operands";
    break;
  case Assign:
    if (!Reg(0, 0) || !U32(1))
      return "expected unified destination and 32-bit immediate";
    break;
  case Not:
    if (!Reg(0, 0) || !Value(1, L))
      return "invalid NOT operands";
    break;
  case Read:
    if (!Reg(0, 3) || !(A ? Reg(1, 3) : Imm(1, 5)) || !Imm(2, 16) ||
        (A && MI.getOperand(2).getImm() != 0))
      return "invalid OLD READ operands";
    break;
  case Write:
  case MaskedWrite:
    if (!(A ? Reg(0, 1) : Imm(0, 5)) || !Imm(1, 16) ||
        (A && MI.getOperand(1).getImm() != 0) ||
        (MI.getOperand(1).getImm() & 1) || !(R ? Reg(2, 1) : U32(2)) ||
        MI.getOperand(2).isExpr())
      return "invalid OLD WRITE operands";
    break;
  case ReadS:
    if (!Reg(0, 0) || !(A ? Reg(1, 2) : Imm(1, 5)) || !Imm(2, 16) ||
        (MI.getOperand(2).getImm() & 3))
      return "READ_S requires SPR address high and aligned imm16 low";
    break;
  case WriteS:
    if (!(A ? Reg(0, 2) : Imm(0, 5)) || !Imm(1, 16) ||
        (MI.getOperand(1).getImm() & 3) || !(R ? Reg(2, 0) : U32(2)) ||
        MI.getOperand(2).isExpr())
      return "WRITE_S requires SPR address high and aligned imm16 low";
    break;
  case Jump:
    if ((F >> 8) & 31) {
      if (!U32(0) ||
          (MI.getOperand(0).isImm() && (MI.getOperand(0).getImm() & 7)))
        return "invalid absolute jump address";
    } else if (!Relative(0, 32))
      return "relative jump must be an aligned signed 32-bit displacement";
    break;
  case JumpC:
    if (!(A ? Reg(0, 0) : Relative(0, 16)) || !Value(1, L) || !Value(2, R))
      return "invalid conditional jump (relative displacement must fit signed "
             "16 bits)";
    break;
  case Mask:
    if (!U32(0) || MI.getOperand(0).isExpr())
      return "mask must be a 32-bit immediate";
    break;
  case Move:
    if (!Reg(0, 3) || !Imm(1, 48))
      return "move requires OLD GPR and unsigned 48-bit immediate";
    break;
  case Marker:
    if (!Imm(0, 3))
      return "invalid marker flags";
    break;
  }
  return nullptr;
}

bool GCE::decode(uint64_t W, MCInst &MI, const MCInstrInfo &MII,
                 const MCRegisterInfo &MRI) {
  unsigned Op = W >> 56, S = (W >> 48) & 31;
  unsigned A = (W >> 32) & 65535, B = (W >> 16) & 65535, C = W & 65535;
  bool AR = W & (1ULL << 55), LR = W & (1ULL << 54), RR = W & (1ULL << 53);
  std::string Name;
  bool Good = true;
  auto I = [&](int64_t V) { MI.addOperand(MCOperand::createImm(V)); };
  auto R = [&](unsigned V, bool Old = false) {
    if (Old)
      V = V < 16 ? 32 + V : 0x100 + V;
    for (unsigned N = 1; N < MRI.getNumRegs(); ++N)
      if (MRI.getEncodingValue(MCRegister(N)) == V) {
        MI.addOperand(MCOperand::createReg(N));
        return;
      }
    Good = false;
    MI.addOperand(MCOperand::createReg(0));
  };
  auto V = [&](unsigned X, bool IsReg) {
    if (IsReg)
      R(X);
    else
      I(X);
  };
  if (Op == 0xA0) {
    if (!AR)
      return false;
    R(A);
    if (S == 0) {
      if (LR || RR)
        return false;
      Name = "LDI";
      I(uint32_t(W));
    } else if (S == 9) {
      if (RR || C)
        return false;
      Name = LR ? "NOTr" : "NOTi";
      V(B, LR);
    } else {
      static const char *Names[] = {"",    "ADD", "SUB", "MUL", "",
                                    "",    "",    "",    "XOR", "",
                                    "ORR", "AND", "LSL", "LSR"};
      if (S >= 14 || !*Names[S])
        return false;
      Name = std::string(Names[S]) + (LR ? 'r' : 'i') + (RR ? 'r' : 'i');
      V(B, LR);
      V(C, RR);
    }
  } else if (Op == 0x01) {
    if (!LR || RR || B)
      return false;
    Name = AR ? "READr" : "READs";
    R(C, true);
    if (AR)
      R(S, true);
    else
      I(S);
    I(A);
  } else if (Op == 0x04) {
    if (RR)
      return false;
    Name = std::string((A & 1) ? "MWRITE" : "WRITE") + (AR ? 'r' : 's') +
           (LR ? 'r' : 'i');
    if (AR)
      R(S, true);
    else
      I(S);
    I(A & ~1);
    if (LR)
      R(uint32_t(W), true);
    else
      I(uint32_t(W));
  } else if (Op == 0x80) {
    if (AR || !LR || RR || C)
      return false;
    Name = (B & 1) ? "READ_Sr" : "READ_Ss";
    R(A);
    if (B & 1)
      R(S);
    else
      I(S);
    I(B & ~1);
  } else if (Op == 0x90 || Op == 0x91) {
    if (AR || RR)
      return false;
    Name = std::string(Op == 0x91 ? "MWRITE_S" : "WRITE_S") +
           ((A & 1) ? 'r' : 's') + (LR ? 'r' : 'i');
    if (A & 1)
      R(S);
    else
      I(S);
    I(A & ~1);
    if (LR)
      R(uint32_t(W));
    else
      I(uint32_t(W));
  } else if (Op == 0x10) {
    if (AR || LR || RR || S || (A & ~1))
      return false;
    Name = (A & 1) ? "JUMPabs" : "JUMPrel";
    I((A & 1) ? int64_t(uint32_t(W)) : int64_t(int32_t(W)));
  } else if (Op == 0xB0 || Op == 0xB1) {
    if (S >= 6 || AR != (Op == 0xB0))
      return false;
    static const char *Names[] = {"JEQ", "JNE", "JGE", "JLE", "JGT", "JLT"};
    Name = std::string(Names[S]) + (AR ? "abs" : "rel") + (LR ? 'r' : 'i') +
           (RR ? 'r' : 'i');
    if (AR)
      R(A);
    else
      I(int16_t(A));
    V(B, LR);
    V(C, RR);
  } else if (Op == 0x02) {
    if (LR || RR)
      return false;
    if (AR) {
      Name = "MOVE";
      R(S, true);
      I(W & 0xffffffffffffULL);
    } else {
      if (A || S)
        return false;
      Name = "MASK";
      I(int64_t(uint32_t(~uint32_t(W))));
    }
  } else if (Op == 0x40) {
    if (W & 1) {
      if (W != 0x4000000000000001ULL)
        return false;
      Name = "EOC";
    } else {
      constexpr uint64_t Allowed = 0xff21000000130000ULL;
      if (W & ~Allowed)
        return false;
      unsigned Prefetch = (W >> 16) & 3;
      if (Prefetch != 0 && (Prefetch != 3 || !(W & (1 << 20))))
        return false;
      if (!Prefetch && (W & (1 << 20)))
        return false;
      Name = "MARKER";
      I((Prefetch ? 1 : 0) | ((W & (1ULL << 48)) ? 0 : 2) |
        ((W & (1ULL << 53)) ? 0 : 4));
    }
  } else
    return false;
  unsigned Opcode = findOpcode(MII, Name);
  if (!Good || Opcode == ~0u)
    return false;
  MI.setOpcode(Opcode);
  return !validate(MI, MII, MRI);
}
