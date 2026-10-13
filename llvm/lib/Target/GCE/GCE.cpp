// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "GCE.h"
#include "TargetInfo/GCETargetInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"
using namespace llvm;
#define DEBUG_TYPE "gce-subtarget"
#define GET_REGINFO_TARGET_DESC
#include "GCEGenRegisterInfo.inc"
#define GET_INSTRINFO_CTOR_DTOR
#include "GCEGenInstrInfo.inc"
#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "GCEGenSubtargetInfo.inc"
GCERegisterInfo::GCERegisterInfo() : GCEGenRegisterInfo(GCE::R15) {}
const uint16_t *
GCERegisterInfo::getCalleeSavedRegs(const MachineFunction *) const {
  // Include RA for PEI; its call mask remains caller-clobbered.
  static const uint16_t Saved[] = {GCE::R8,
                                   GCE::R9,
                                   GCE::R10,
                                   GCE::R11,
                                   GCE::R12,
                                   GCE::CPR0T4,
                                   GCE::CPR1T4,
                                   GCE::CPR2T4,
                                   GCE::CPR3T4,
                                   GCE::CPR0T5,
                                   GCE::CPR1T5,
                                   GCE::CPR2T5,
                                   GCE::CPR3T5,
                                   GCE::CPR0T6,
                                   GCE::CPR1T6,
                                   GCE::CPR2T6,
                                   GCE::CPR3T6,
                                   GCE::CPR0T7,
                                   GCE::CPR1T7,
                                   GCE::CPR2T7,
                                   GCE::CPR3T7,
                                   GCE::R15,
                                   0};
  return Saved;
}
const uint32_t *GCERegisterInfo::getCallPreservedMask(const MachineFunction &,
                                                      CallingConv::ID) const {
  return CSR_GCE_RegMask;
}
BitVector GCERegisterInfo::getReservedRegs(const MachineFunction &) const {
  BitVector R(getNumRegs());
  for (unsigned Reg : {GCE::R1, GCE::R13, GCE::R14, GCE::R15, GCE::SPR0,
                       GCE::SPR1, GCE::TPR, GCE::HND})
    markSuperRegs(R, Reg);
  // Upstream hardware tests confirm CPRs for all 16 threads. T8-T15 are
  // caller-saved (absent from CSR_GCE), like T0-T3.
  return R;
}
Register GCERegisterInfo::getFrameRegister(const MachineFunction &) const {
  return GCE::R14;
}
bool GCERegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator I,
                                          int SPAdj, unsigned Op,
                                          RegScavenger *) const {
  assert(SPAdj == 0 && "reserved call frame");
  auto &MF = *I->getParent()->getParent();
  int FI = I->getOperand(Op).getIndex();
  int64_t Offset = MF.getFrameInfo().getObjectOffset(FI) +
                   MF.getFrameInfo().getStackSize() +
                   I->getOperand(Op + 1).getImm();
  if (!isInt<32>(Offset))
    report_fatal_error("GCE frame offset exceeds 32 bits");
  I->getOperand(Op).ChangeToRegister(GCE::R14, false);
  I->getOperand(Op + 1).setImm(Offset);
  return false;
}
GCEInstrInfo::GCEInstrInfo(const TargetSubtargetInfo &ST)
    : GCEGenInstrInfo(ST, RI, GCE::ADJCALLSTACKDOWN, GCE::ADJCALLSTACKUP) {}
void GCEInstrInfo::copyPhysReg(MachineBasicBlock &B,
                               MachineBasicBlock::iterator I, const DebugLoc &D,
                               Register Dst, Register Src, bool Kill, bool,
                               bool) const {
  BuildMI(B, I, D, get(GCE::ADDri), Dst)
      .addReg(Src, getKillRegState(Kill))
      .addImm(0);
}
void GCEInstrInfo::storeRegToStackSlot(MachineBasicBlock &B,
                                       MachineBasicBlock::iterator I,
                                       Register Src, bool Kill, int FI,
                                       const TargetRegisterClass *, Register,
                                       MachineInstr::MIFlag F) const {
  auto &MF = *B.getParent();
  auto *MMO = MF.getMachineMemOperand(MachinePointerInfo::getFixedStack(MF, FI),
                                      MachineMemOperand::MOStore, 4, Align(4));
  BuildMI(B, I, DebugLoc(), get(GCE::STORE32))
      .addReg(Src, getKillRegState(Kill))
      .addFrameIndex(FI)
      .addImm(0)
      .addMemOperand(MMO)
      .setMIFlag(F);
}
void GCEInstrInfo::loadRegFromStackSlot(MachineBasicBlock &B,
                                        MachineBasicBlock::iterator I,
                                        Register Dst, int FI,
                                        const TargetRegisterClass *, Register,
                                        unsigned,
                                        MachineInstr::MIFlag F) const {
  auto &MF = *B.getParent();
  auto *MMO = MF.getMachineMemOperand(MachinePointerInfo::getFixedStack(MF, FI),
                                      MachineMemOperand::MOLoad, 4, Align(4));
  BuildMI(B, I, DebugLoc(), get(GCE::LOAD32), Dst)
      .addFrameIndex(FI)
      .addImm(0)
      .addMemOperand(MMO)
      .setMIFlag(F);
}
bool GCEInstrInfo::analyzeBranch(MachineBasicBlock &B, MachineBasicBlock *&T,
                                 MachineBasicBlock *&F,
                                 SmallVectorImpl<MachineOperand> &C,
                                 bool) const {
  T = F = nullptr;
  C.clear();
  for (auto &I : B.terminators()) {
    if (I.isDebugInstr())
      continue;
    if (I.getOpcode() == GCE::BCC && !T) {
      T = I.getOperand(0).getMBB();
      for (unsigned N = 1; N < 4; ++N)
        C.push_back(I.getOperand(N));
    } else if (I.getOpcode() == GCE::JUMPrel) {
      if (!T)
        T = I.getOperand(0).getMBB();
      else
        F = I.getOperand(0).getMBB();
    } else
      return true;
  }
  return false;
}
unsigned GCEInstrInfo::removeBranch(MachineBasicBlock &B, int *Bytes) const {
  unsigned N = 0;
  if (Bytes)
    *Bytes = 0;
  while (!B.empty()) {
    auto I = B.getLastNonDebugInstr();
    if (I == B.end() ||
        (I->getOpcode() != GCE::BCC && I->getOpcode() != GCE::JUMPrel))
      break;
    if (Bytes)
      *Bytes += I->getDesc().getSize();
    I->eraseFromParent();
    ++N;
  }
  return N;
}
unsigned GCEInstrInfo::insertBranch(MachineBasicBlock &B, MachineBasicBlock *T,
                                    MachineBasicBlock *F,
                                    ArrayRef<MachineOperand> C,
                                    const DebugLoc &D, int *Bytes) const {
  assert(T);
  if (Bytes)
    *Bytes = 0;
  if (C.empty()) {
    BuildMI(&B, D, get(GCE::JUMPrel)).addMBB(T);
    if (Bytes)
      *Bytes = 8;
    return 1;
  }
  auto M = BuildMI(&B, D, get(GCE::BCC)).addMBB(T);
  for (auto &O : C)
    M.add(O);
  if (Bytes)
    *Bytes = 16;
  if (!F)
    return 1;
  BuildMI(&B, D, get(GCE::JUMPrel)).addMBB(F);
  if (Bytes)
    *Bytes += 8;
  return 2;
}
bool GCEInstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &C) const {
  static constexpr unsigned Inverse[] = {1, 0, 5, 4, 3, 2};
  C[2].setImm(Inverse[C[2].getImm()]);
  return false;
}
static void adjustSP(MachineBasicBlock &B, MachineBasicBlock::iterator I,
                     int64_t Amount, MachineInstr::MIFlag Flag) {
  if (!Amount)
    return;
  const auto &II = *B.getParent()->getSubtarget().getInstrInfo();
  BuildMI(B, I, DebugLoc(), II.get(GCE::LDI), GCE::R13)
      .addImm(uint32_t(Amount))
      .setMIFlag(Flag);
  BuildMI(B, I, DebugLoc(), II.get(GCE::ADDrr), GCE::R14)
      .addReg(GCE::R14)
      .addReg(GCE::R13)
      .setMIFlag(Flag);
}
void GCEFrameLowering::emitPrologue(MachineFunction &MF,
                                    MachineBasicBlock &B) const {
  if (MF.getFrameInfo().hasVarSizedObjects() ||
      MF.getFrameInfo().getMaxAlign() > Align(8))
    report_fatal_error(
        "GCE does not support dynamic or over-aligned stack frames");
  MF.getFrameInfo().setStackSize(alignTo(MF.getFrameInfo().getStackSize(), 8));
  adjustSP(B, B.begin(), -int64_t(MF.getFrameInfo().getStackSize()),
           MachineInstr::FrameSetup);
}
void GCEFrameLowering::emitEpilogue(MachineFunction &MF,
                                    MachineBasicBlock &B) const {
  adjustSP(B, B.getFirstTerminator(), MF.getFrameInfo().getStackSize(),
           MachineInstr::FrameDestroy);
}
MachineBasicBlock::iterator GCEFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &, MachineBasicBlock &B,
    MachineBasicBlock::iterator I) const {
  return B.erase(I);
}
void GCEFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                            BitVector &Saved,
                                            RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, Saved, RS);
  if (MF.getFrameInfo().hasCalls())
    Saved.set(GCE::R15);
}
GCESubtarget::GCESubtarget(const Triple &TT, StringRef CPU, StringRef FS,
                           const TargetMachine &TM)
    : GCEGenSubtargetInfo(TT, CPU.empty() ? "generic" : CPU,
                          CPU.empty() ? "generic" : CPU, FS),
      II(*this), TL(TM, *this, II.getRegisterInfo()) {
  ParseSubtargetFeatures(CPU.empty() ? "generic" : CPU,
                         CPU.empty() ? "generic" : CPU, FS);
}
GCETargetMachine::GCETargetMachine(const Target &T, const Triple &TT,
                                   StringRef CPU, StringRef FS,
                                   const TargetOptions &O,
                                   std::optional<Reloc::Model> RM,
                                   std::optional<CodeModel::Model> CM,
                                   CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, "e-m:e-p:32:32-i64:64-n32-S64", TT, CPU, FS,
                               O, RM.value_or(Reloc::Static),
                               CM.value_or(CodeModel::Small), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()),
      ST(TT, CPU, FS, *this) {
  if (getRelocationModel() != Reloc::Static || JIT)
    report_fatal_error(
        "GCE requires static code; PIC/PIE and JIT are unsupported");
  initAsmInfo();
}
namespace {
class GCEPassConfig : public TargetPassConfig {
public:
  GCEPassConfig(GCETargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}
  bool addInstSelector() override {
    addPass(createGCEISelDag(getTM<GCETargetMachine>()));
    return false;
  }
};
} // namespace
TargetPassConfig *GCETargetMachine::createPassConfig(PassManagerBase &PM) {
  return new GCEPassConfig(*this, PM);
}
extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeGCETarget() {
  RegisterTargetMachine<GCETargetMachine> X(getTheGCETarget());
  initializeGCEISelLegacyPass(*PassRegistry::getPassRegistry());
}
