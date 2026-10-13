// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "GCE.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/ErrorHandling.h"
using namespace llvm;
GCETargetLowering::GCETargetLowering(const TargetMachine &TM,
                                     const TargetSubtargetInfo &ST,
                                     const GCERegisterInfo &RI)
    : TargetLowering(TM, ST) {
  addRegisterClass(MVT::i32, &GCE::UREGRegClass);
  setStackPointerRegisterToSaveRestore(GCE::R14);
  setMinStackArgumentAlignment(Align(4));
  setBooleanContents(ZeroOrOneBooleanContent);
  setMinFunctionAlignment(Align(8));
  setPrefFunctionAlignment(Align(8));
  setMaxAtomicSizeInBitsSupported(0);
  for (unsigned Op : {ISD::SDIV, ISD::UDIV, ISD::SREM, ISD::UREM})
    setOperationAction(Op, MVT::i32, LibCall);
  for (unsigned Op : {ISD::SDIVREM,
                      ISD::UDIVREM,
                      ISD::MULHU,
                      ISD::MULHS,
                      ISD::SMUL_LOHI,
                      ISD::UMUL_LOHI,
                      ISD::SIGN_EXTEND_INREG,
                      ISD::ROTL,
                      ISD::ROTR,
                      ISD::BSWAP,
                      ISD::CTPOP,
                      ISD::CTLZ,
                      ISD::CTTZ,
                      ISD::CTLZ_ZERO_UNDEF,
                      ISD::CTTZ_ZERO_UNDEF,
                      ISD::SHL_PARTS,
                      ISD::SRL_PARTS,
                      ISD::SRA_PARTS,
                      ISD::BRCOND,
                      ISD::SELECT})
    setOperationAction(Op, MVT::i32, Expand);
  for (MVT VT : {MVT::i1, MVT::i8, MVT::i16}) {
    setOperationAction(ISD::SIGN_EXTEND_INREG, VT, Expand);
    setLoadExtAction(ISD::EXTLOAD, MVT::i32, VT, Custom);
    setLoadExtAction(ISD::ZEXTLOAD, MVT::i32, VT, Custom);
    setLoadExtAction(ISD::SEXTLOAD, MVT::i32, VT, Custom);
    setTruncStoreAction(MVT::i32, VT, Legal);
  }
  for (unsigned Op :
       {ISD::BR_CC, ISD::SETCC, ISD::SELECT_CC, ISD::SRA, ISD::GlobalAddress,
        ISD::ExternalSymbol, ISD::BlockAddress, ISD::ConstantPool})
    setOperationAction(Op, MVT::i32, Custom);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VAARG, MVT::Other, Expand);
  setOperationAction(ISD::VACOPY, MVT::Other, Expand);
  setOperationAction(ISD::VAEND, MVT::Other, Expand);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32, Custom);
  setOperationAction(ISD::GlobalTLSAddress, MVT::i32, Custom);
  setMinimumJumpTableEntries(UINT_MAX);
  computeRegisterProperties(&RI);
}
const char *GCETargetLowering::getTargetNodeName(unsigned Op) const {
  switch (Op) {
  case GCEISD::RET:
    return "GCEISD::RET";
  case GCEISD::CALL:
    return "GCEISD::CALL";
  case GCEISD::CALLR:
    return "GCEISD::CALLR";
  case GCEISD::BCC:
    return "GCEISD::BCC";
  case GCEISD::SELECT:
    return "GCEISD::SELECT";
  case GCEISD::ADDR:
    return "GCEISD::ADDR";
  default:
    return nullptr;
  }
}
static unsigned condition(ISD::CondCode C, SDValue &L, SDValue &R,
                          const SDLoc &DL, SelectionDAG &D) {
  if (ISD::isSignedIntSetCC(C)) {
    auto Sign = D.getConstant(0x80000000, DL, MVT::i32);
    L = D.getNode(ISD::XOR, DL, MVT::i32, L, Sign);
    R = D.getNode(ISD::XOR, DL, MVT::i32, R, Sign);
  }
  switch (C) {
  case ISD::SETEQ:
    return 0;
  case ISD::SETNE:
    return 1;
  case ISD::SETGE:
  case ISD::SETUGE:
    return 2;
  case ISD::SETLE:
  case ISD::SETULE:
    return 3;
  case ISD::SETGT:
  case ISD::SETUGT:
    return 4;
  case ISD::SETLT:
  case ISD::SETULT:
    return 5;
  default:
    report_fatal_error("GCE supports integer comparisons only");
  }
}
SDValue GCETargetLowering::LowerOperation(SDValue O, SelectionDAG &D) const {
  SDLoc L(O);
  auto C = [&](uint32_t V) { return D.getConstant(V, L, MVT::i32); };
  auto N = [&](unsigned Op, SDValue A, SDValue B) {
    return D.getNode(Op, L, MVT::i32, A, B);
  };
  switch (O.getOpcode()) {
  case ISD::VASTART: {
    auto &MF = D.getMachineFunction();
    int FI = MF.getInfo<GCEMachineFunctionInfo>()->getVarArgsFrameIndex();
    const Value *SV = cast<SrcValueSDNode>(O.getOperand(2))->getValue();
    return D.getStore(O.getOperand(0), L, D.getFrameIndex(FI, MVT::i32),
                      O.getOperand(1), MachinePointerInfo(SV));
  }
  case ISD::GlobalAddress: {
    auto *G = cast<GlobalAddressSDNode>(O);
    return D.getNode(
        GCEISD::ADDR, L, MVT::i32,
        D.getTargetGlobalAddress(G->getGlobal(), L, MVT::i32, G->getOffset()));
  }
  case ISD::ExternalSymbol:
    return D.getNode(GCEISD::ADDR, L, MVT::i32,
                     D.getTargetExternalSymbol(
                         cast<ExternalSymbolSDNode>(O)->getSymbol(), MVT::i32));
  case ISD::ConstantPool: {
    auto *P = cast<ConstantPoolSDNode>(O);
    SDValue Address =
        P->isMachineConstantPoolEntry()
            ? D.getTargetConstantPool(P->getMachineCPVal(), MVT::i32,
                                      P->getAlign(), P->getOffset())
            : D.getTargetConstantPool(P->getConstVal(), MVT::i32, P->getAlign(),
                                      P->getOffset());
    return D.getNode(GCEISD::ADDR, L, MVT::i32, Address);
  }
  case ISD::BlockAddress: {
    auto *B = cast<BlockAddressSDNode>(O);
    return D.getNode(GCEISD::ADDR, L, MVT::i32,
                     D.getTargetBlockAddress(B->getBlockAddress(), MVT::i32,
                                             B->getOffset()));
  }
  case ISD::SRA: {
    SDValue X = O.getOperand(0), S = O.getOperand(1);
    auto Sign = N(ISD::SUB, C(0), N(ISD::SRL, X, C(31)));
    return N(ISD::OR, N(ISD::SRL, X, S),
             N(ISD::SHL, N(ISD::SHL, Sign, N(ISD::SUB, C(31), S)), C(1)));
  }
  case ISD::SETCC:
  case ISD::SELECT_CC:
  case ISD::BR_CC: {
    bool Br = O.getOpcode() == ISD::BR_CC, Set = O.getOpcode() == ISD::SETCC;
    SDValue A = O.getOperand(Br ? 2 : 0), B = O.getOperand(Br ? 3 : 1);
    auto CC = cast<CondCodeSDNode>(O.getOperand(Br ? 1 : Set ? 2 : 4))->get();
    SDValue Cond = D.getTargetConstant(condition(CC, A, B, L, D), L, MVT::i32);
    if (Br)
      return D.getNode(GCEISD::BCC, L, MVT::Other, O.getOperand(0),
                       O.getOperand(4), A, B, Cond);
    return D.getNode(GCEISD::SELECT, L, MVT::i32, A, B, Cond,
                     Set ? C(1) : O.getOperand(2),
                     Set ? C(0) : O.getOperand(3));
  }
  case ISD::LOAD: {
    auto *M = cast<LoadSDNode>(O);
    if (M->isAtomic())
      report_fatal_error("GCE atomics are unsupported");
    unsigned Bits = M->getMemoryVT().getSizeInBits();
    if (Bits > 16)
      report_fatal_error("unexpected GCE custom memory width");
    SDValue Ptr = M->getBasePtr();
    auto Flags = MachineMemOperand::MOLoad;
    if (M->isVolatile())
      Flags |= MachineMemOperand::MOVolatile;
    if (Bits == 16 && M->getAlign() < Align(2)) {
      auto Next = N(ISD::ADD, Ptr, C(1));
      auto Lo = D.getExtLoad(ISD::ZEXTLOAD, L, MVT::i32, M->getChain(), Ptr,
                             MachinePointerInfo(), MVT::i8, Align(1), Flags);
      auto Hi = D.getExtLoad(ISD::ZEXTLOAD, L, MVT::i32, Lo.getValue(1), Next,
                             MachinePointerInfo(), MVT::i8, Align(1), Flags);
      auto V = N(ISD::OR, Lo, N(ISD::SHL, Hi, C(8)));
      if (M->getExtensionType() == ISD::SEXTLOAD)
        V = N(ISD::SRA, N(ISD::SHL, V, C(16)), C(16));
      return D.getMergeValues({V, Hi.getValue(1)}, L);
    }
    auto Base = N(ISD::AND, Ptr, C(~3u));
    auto Shift = N(ISD::SHL, N(ISD::AND, Ptr, C(3)), C(3));
    auto Word = D.getLoad(MVT::i32, L, M->getChain(), Base,
                          MachinePointerInfo(), Align(4), Flags);
    auto V = N(ISD::AND, N(ISD::SRL, Word, Shift), C((1u << Bits) - 1));
    if (M->getExtensionType() == ISD::SEXTLOAD)
      V = N(ISD::SRA, N(ISD::SHL, V, C(32 - Bits)), C(32 - Bits));
    return D.getMergeValues({V, Word.getValue(1)}, L);
  }
  case ISD::DYNAMIC_STACKALLOC:
    report_fatal_error("GCE VLA/dynamic alloca is unsupported");
  case ISD::GlobalTLSAddress:
    report_fatal_error("GCE TLS is unsupported");
  default:
    report_fatal_error("unimplemented GCE lowering operation");
  }
}
namespace {
struct ArgLoc {
  unsigned Reg;
  unsigned Offset;
};
// Sixteen fixed-argument words. The CPR argument bank is already
// caller-clobbered; preserve the existing R2/R3 result and callee-save rules.
constexpr unsigned ArgRegs[] = {
    GCE::R2,     GCE::R3,     GCE::R4,     GCE::R5,
    GCE::CPR0T0, GCE::CPR1T0, GCE::CPR2T0, GCE::CPR3T0,
    GCE::CPR0T1, GCE::CPR1T1, GCE::CPR2T1, GCE::CPR3T1,
    GCE::CPR0T2, GCE::CPR1T2, GCE::CPR2T2, GCE::CPR3T2};
// Each legalized i64/f64 pair is kept together, even-aligned, never split.
template <class Args>
SmallVector<ArgLoc> locations(const Args &A, unsigned &Stack) {
  SmallVector<ArgLoc> L;
  unsigned Slot = 0;
  Stack = 0;
  bool OnStack = false;
  for (unsigned I = 0; I < A.size();) {
    if (A[I].Flags.isByVal() || A[I].Flags.isSRet() || A[I].Flags.isInAlloca())
      report_fatal_error(
          "GCE aggregate by-value arguments and returns are unsupported");
    unsigned Parts = ((A[I].ArgVT == MVT::i64 || A[I].ArgVT == MVT::f64) &&
                      A[I].PartOffset == 0) ? 2 : 1;
    if (I + Parts > A.size())
      report_fatal_error("invalid GCE split argument");
    if (Parts == 2)
      Slot = alignTo(Slot, 2);
    // Only fixed arguments may use the argument bank. Keep unnamed arguments
    // contiguous on the stack so va_list remains a simple pointer.
    OnStack |= A[I].Flags.isVarArg() || Slot + Parts > std::size(ArgRegs);
    if (OnStack)
      Stack = alignTo(Stack, Parts == 2 ? 8 : 4);
    for (unsigned J = 0; J < Parts; ++J) {
      if (OnStack) {
        L.push_back({0, Stack});
        Stack += 4;
      } else
        L.push_back({ArgRegs[Slot++], 0});
    }
    I += Parts;
  }
  // Return the exact end, not the rounded call-frame size: a named argument
  // at SP+0 can be followed by the first unnamed i32 at SP+4.
  return L;
}
static SDValue extend(SDValue V, const ISD::ArgFlagsTy &F, const SDLoc &L,
                      SelectionDAG &D) {
  if (V.getValueType() == MVT::i32)
    return V;
  return D.getNode(F.isSExt() ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND, L,
                   MVT::i32, V);
}
} // namespace
SDValue GCETargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CC, bool Var,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &L, SelectionDAG &D,
    SmallVectorImpl<SDValue> &Vals) const {
  auto &MF = D.getMachineFunction();
  unsigned Stack;
  auto Loc = locations(Ins, Stack);
  if (Var) {
    int FI = MF.getFrameInfo().CreateFixedObject(4, Stack, true);
    MF.getInfo<GCEMachineFunctionInfo>()->setVarArgsFrameIndex(FI);
  }
  for (unsigned I = 0; I < Ins.size(); ++I) {
    SDValue V;
    if (Loc[I].Reg) {
      Register R = MF.addLiveIn(Loc[I].Reg, &GCE::UREGRegClass);
      V = D.getCopyFromReg(Chain, L, R, MVT::i32);
    } else {
      int FI = MF.getFrameInfo().CreateFixedObject(4, Loc[I].Offset, true);
      V = D.getLoad(MVT::i32, L, Chain, D.getFrameIndex(FI, MVT::i32),
                    MachinePointerInfo::getFixedStack(MF, FI));
    }
    if (Ins[I].VT != MVT::i32)
      V = D.getNode(ISD::TRUNCATE, L, Ins[I].VT, V);
    Vals.push_back(V);
  }
  return Chain;
}
SDValue
GCETargetLowering::LowerReturn(SDValue Chain, CallingConv::ID, bool,
                               const SmallVectorImpl<ISD::OutputArg> &Outs,
                               const SmallVectorImpl<SDValue> &Vals,
                               const SDLoc &L, SelectionDAG &D) const {
  if (Outs.size() > 2)
    report_fatal_error("GCE return value exceeds 64 bits");
  SDValue Glue;
  SmallVector<SDValue> Ops;
  for (unsigned I = 0; I < Outs.size(); ++I) {
    Chain = D.getCopyToReg(Chain, L, ArgRegs[I],
                           extend(Vals[I], Outs[I].Flags, L, D), Glue);
    Glue = Chain.getValue(1);
    Ops.push_back(D.getRegister(ArgRegs[I], MVT::i32));
  }
  Ops.insert(Ops.begin(), Chain);
  if (Glue)
    Ops.push_back(Glue);
  return D.getNode(GCEISD::RET, L, MVT::Other, Ops);
}
SDValue GCETargetLowering::LowerCall(CallLoweringInfo &CLI,
                                     SmallVectorImpl<SDValue> &Vals) const {
  auto &D = CLI.DAG;
  auto L = CLI.DL;
  auto Chain = CLI.Chain;
  auto &MF = D.getMachineFunction();
  CLI.IsTailCall = false;
  unsigned Stack;
  auto Loc = locations(CLI.Outs, Stack);
  Stack = alignTo(Stack, 8);
  Chain = D.getCALLSEQ_START(Chain, Stack, 0, L);
  SDValue SP = D.getRegister(GCE::R14, MVT::i32);
  for (unsigned I = 0; I < Loc.size(); ++I)
    if (!Loc[I].Reg) {
      auto Addr = D.getNode(ISD::ADD, L, MVT::i32, SP,
                            D.getConstant(Loc[I].Offset, L, MVT::i32));
      Chain =
          D.getStore(Chain, L, extend(CLI.OutVals[I], CLI.Outs[I].Flags, L, D),
                     Addr, MachinePointerInfo(), Align(4));
    }
  SDValue Glue;
  SmallVector<SDValue> Ops;
  for (unsigned I = 0; I < Loc.size(); ++I)
    if (Loc[I].Reg) {
      Chain =
          D.getCopyToReg(Chain, L, Loc[I].Reg,
                         extend(CLI.OutVals[I], CLI.Outs[I].Flags, L, D), Glue);
      Glue = Chain.getValue(1);
      Ops.push_back(D.getRegister(Loc[I].Reg, MVT::i32));
    }
  SDValue Callee = CLI.Callee;
  unsigned Opcode = GCEISD::CALLR;
  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    Callee =
        D.getTargetGlobalAddress(G->getGlobal(), L, MVT::i32, G->getOffset());
    Opcode = GCEISD::CALL;
  } else if (auto *E = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    Callee = D.getTargetExternalSymbol(E->getSymbol(), MVT::i32);
    Opcode = GCEISD::CALL;
  }
  Ops.insert(Ops.begin(), {Chain, Callee});
  Ops.push_back(D.getRegisterMask(
      MF.getSubtarget().getRegisterInfo()->getCallPreservedMask(MF,
                                                                CLI.CallConv)));
  if (Glue)
    Ops.push_back(Glue);
  Chain = D.getNode(Opcode, L, D.getVTList(MVT::Other, MVT::Glue), Ops);
  Glue = Chain.getValue(1);
  Chain = D.getCALLSEQ_END(Chain, D.getTargetConstant(Stack, L, MVT::i32),
                           D.getTargetConstant(0, L, MVT::i32), Glue, L);
  Glue = Chain.getValue(1);
  if (CLI.Ins.size() > 2)
    report_fatal_error("GCE return value exceeds 64 bits");
  for (unsigned I = 0; I < CLI.Ins.size(); ++I) {
    SDValue V = D.getCopyFromReg(Chain, L, ArgRegs[I], MVT::i32, Glue);
    Chain = V.getValue(1);
    Glue = V.getValue(2);
    if (CLI.Ins[I].VT != MVT::i32)
      V = D.getNode(ISD::TRUNCATE, L, CLI.Ins[I].VT, V);
    Vals.push_back(V);
  }
  return Chain;
}
MachineBasicBlock *
GCETargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                               MachineBasicBlock *B) const {
  assert(MI.getOpcode() == GCE::SELECT_CC);
  auto &MF = *B->getParent();
  auto &II = *MF.getSubtarget().getInstrInfo();
  auto *No = MF.CreateMachineBasicBlock(B->getBasicBlock());
  auto *Join = MF.CreateMachineBasicBlock(B->getBasicBlock());
  // A select can be scheduled between call-frame setup and teardown while
  // materializing a stack argument. Both new blocks inherit that frame size.
  unsigned CallFrameSize = II.getCallFrameSizeAt(MI);
  No->setCallFrameSize(CallFrameSize);
  Join->setCallFrameSize(CallFrameSize);
  auto Pos = std::next(B->getIterator());
  MF.insert(Pos, No);
  MF.insert(Pos, Join);
  Join->splice(Join->begin(), B, std::next(MI.getIterator()), B->end());
  Join->transferSuccessorsAndUpdatePHIs(B);
  B->addSuccessor(No);
  B->addSuccessor(Join);
  No->addSuccessor(Join);
  BuildMI(B, MI.getDebugLoc(), II.get(GCE::BCC))
      .addMBB(Join)
      .add(MI.getOperand(1))
      .add(MI.getOperand(2))
      .add(MI.getOperand(3));
  BuildMI(*Join, Join->begin(), MI.getDebugLoc(), II.get(TargetOpcode::PHI),
          MI.getOperand(0).getReg())
      .addReg(MI.getOperand(4).getReg())
      .addMBB(B)
      .addReg(MI.getOperand(5).getReg())
      .addMBB(No);
  MI.eraseFromParent();
  return Join;
}
