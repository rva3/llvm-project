// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef LLVM_LIB_TARGET_GCE_GCE_H
#define LLVM_LIB_TARGET_GCE_GCE_H
#include "MCTargetDesc/GCEMCTargetDesc.h"
#include "llvm/CodeGen/CodeGenTargetMachineImpl.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGTargetInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#define GET_REGINFO_HEADER
#include "GCEGenRegisterInfo.inc"
#define GET_INSTRINFO_HEADER
#include "GCEGenInstrInfo.inc"
#define GET_SUBTARGETINFO_HEADER
#include "GCEGenSubtargetInfo.inc"
namespace llvm {
namespace GCEISD {
enum { RET = ISD::BUILTIN_OP_END, CALL, CALLR, BCC, SELECT, ADDR };
}
class GCEMachineFunctionInfo : public MachineFunctionInfo {
  int VarArgsFrameIndex = 0;

public:
  GCEMachineFunctionInfo(const Function &, const TargetSubtargetInfo *) {}
  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int FI) { VarArgsFrameIndex = FI; }
  MachineFunctionInfo *
  clone(BumpPtrAllocator &, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &) const override {
    return DestMF.cloneInfo<GCEMachineFunctionInfo>(*this);
  }
};
class GCERegisterInfo : public GCEGenRegisterInfo {
public:
  GCERegisterInfo();
  const uint16_t *
  getCalleeSavedRegs(const MachineFunction *MF = nullptr) const override;
  const uint32_t *getCallPreservedMask(const MachineFunction &,
                                       CallingConv::ID) const override;
  BitVector getReservedRegs(const MachineFunction &) const override;
  Register getFrameRegister(const MachineFunction &) const override;
  bool eliminateFrameIndex(MachineBasicBlock::iterator, int, unsigned,
                           RegScavenger *) const override;
};
class GCEInstrInfo : public GCEGenInstrInfo {
  GCERegisterInfo RI;

public:
  explicit GCEInstrInfo(const TargetSubtargetInfo &);
  const GCERegisterInfo &getRegisterInfo() const { return RI; }
  void copyPhysReg(MachineBasicBlock &, MachineBasicBlock::iterator,
                   const DebugLoc &, Register, Register, bool, bool = false,
                   bool = false) const override;
  void storeRegToStackSlot(
      MachineBasicBlock &, MachineBasicBlock::iterator, Register, bool, int,
      const TargetRegisterClass *, Register,
      MachineInstr::MIFlag = MachineInstr::NoFlags) const override;
  void loadRegFromStackSlot(
      MachineBasicBlock &, MachineBasicBlock::iterator, Register, int,
      const TargetRegisterClass *, Register, unsigned = 0,
      MachineInstr::MIFlag = MachineInstr::NoFlags) const override;
  bool analyzeBranch(MachineBasicBlock &, MachineBasicBlock *&,
                     MachineBasicBlock *&, SmallVectorImpl<MachineOperand> &,
                     bool = false) const override;
  unsigned removeBranch(MachineBasicBlock &, int * = nullptr) const override;
  unsigned insertBranch(MachineBasicBlock &, MachineBasicBlock *,
                        MachineBasicBlock *, ArrayRef<MachineOperand>,
                        const DebugLoc &, int * = nullptr) const override;
  bool reverseBranchCondition(SmallVectorImpl<MachineOperand> &) const override;
};
class GCEFrameLowering : public TargetFrameLowering {
  bool hasFPImpl(const MachineFunction &) const override { return false; }

public:
  GCEFrameLowering()
      : TargetFrameLowering(StackGrowsDown, Align(8), 0, Align(8)) {}
  void emitPrologue(MachineFunction &, MachineBasicBlock &) const override;
  void emitEpilogue(MachineFunction &, MachineBasicBlock &) const override;
  bool hasReservedCallFrame(const MachineFunction &) const override {
    return true;
  }
  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &, MachineBasicBlock &,
                                MachineBasicBlock::iterator) const override;
  void determineCalleeSaves(MachineFunction &, BitVector &,
                            RegScavenger *) const override;
};
class GCETargetLowering : public TargetLowering {
public:
  GCETargetLowering(const TargetMachine &, const TargetSubtargetInfo &,
                    const GCERegisterInfo &);
  SDValue LowerOperation(SDValue, SelectionDAG &) const override;
  const char *getTargetNodeName(unsigned) const override;
  SDValue LowerFormalArguments(SDValue, CallingConv::ID, bool,
                               const SmallVectorImpl<ISD::InputArg> &,
                               const SDLoc &, SelectionDAG &,
                               SmallVectorImpl<SDValue> &) const override;
  SDValue LowerReturn(SDValue, CallingConv::ID, bool,
                      const SmallVectorImpl<ISD::OutputArg> &,
                      const SmallVectorImpl<SDValue> &, const SDLoc &,
                      SelectionDAG &) const override;
  SDValue LowerCall(CallLoweringInfo &,
                    SmallVectorImpl<SDValue> &) const override;
  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &,
                              MachineBasicBlock *) const override;
};
class GCESubtarget : public GCEGenSubtargetInfo {
  GCEInstrInfo II;
  GCEFrameLowering FL;
  GCETargetLowering TL;
  SelectionDAGTargetInfo SDI;

public:
  GCESubtarget(const Triple &, StringRef, StringRef, const TargetMachine &);
  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);
  const GCEInstrInfo *getInstrInfo() const override { return &II; }
  const GCERegisterInfo *getRegisterInfo() const override {
    return &II.getRegisterInfo();
  }
  const GCEFrameLowering *getFrameLowering() const override { return &FL; }
  const GCETargetLowering *getTargetLowering() const override { return &TL; }
  const SelectionDAGTargetInfo *getSelectionDAGInfo() const override {
    return &SDI;
  }
};
class GCETargetMachine : public CodeGenTargetMachineImpl {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  GCESubtarget ST;

public:
  GCETargetMachine(const Target &, const Triple &, StringRef, StringRef,
                   const TargetOptions &, std::optional<Reloc::Model>,
                   std::optional<CodeModel::Model>, CodeGenOptLevel, bool);
  const GCESubtarget *getSubtargetImpl(const Function &) const override {
    return &ST;
  }
  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }
  TargetPassConfig *createPassConfig(PassManagerBase &) override;
  MachineFunctionInfo *
  createMachineFunctionInfo(BumpPtrAllocator &Allocator, const Function &F,
                            const TargetSubtargetInfo *STI) const override {
    return MachineFunctionInfo::create<GCEMachineFunctionInfo>(Allocator, F, STI);
  }
};
FunctionPass *createGCEISelDag(GCETargetMachine &);
void initializeGCEISelLegacyPass(PassRegistry &);
} // namespace llvm
#endif
