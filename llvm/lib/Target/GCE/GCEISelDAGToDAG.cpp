// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "GCE.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/ErrorHandling.h"
using namespace llvm;
namespace {
class GCEISel : public SelectionDAGISel {
public:
  explicit GCEISel(GCETargetMachine &TM) : SelectionDAGISel(TM) {}
  bool runOnMachineFunction(MachineFunction &MF) override {
    const Function &F = MF.getFunction();
    auto CheckType = [](Type *T) {
      if (T->isVectorTy())
        report_fatal_error("GCE vector operations are unsupported");
      if (T->isFloatingPointTy() && !T->isFloatTy() && !T->isDoubleTy())
        report_fatal_error("GCE supports only f32/f64 floating-point types");
    };
    CheckType(F.getReturnType());
    for (const auto &A : F.args())
      CheckType(A.getType());
    for (const auto &B : F)
      for (const auto &I : B) {
        CheckType(I.getType());
        for (const auto &O : I.operands())
          CheckType(O->getType());
        if (isa<ConstrainedFPIntrinsic>(I))
          report_fatal_error("GCE strict floating-point environment is unsupported");
        if (I.getOpcode() == Instruction::FRem)
          report_fatal_error("GCE floating remainder requires an external math library");
        if (const auto *II = dyn_cast<IntrinsicInst>(&I)) {
          switch (II->getIntrinsicID()) {
          case Intrinsic::fma:
          case Intrinsic::sqrt:
          case Intrinsic::sin:
          case Intrinsic::cos:
          case Intrinsic::pow:
          case Intrinsic::powi:
          case Intrinsic::exp:
          case Intrinsic::exp2:
          case Intrinsic::log:
          case Intrinsic::log2:
          case Intrinsic::log10:
          case Intrinsic::floor:
          case Intrinsic::ceil:
          case Intrinsic::trunc:
          case Intrinsic::round:
          case Intrinsic::roundeven:
          case Intrinsic::nearbyint:
          case Intrinsic::rint:
          case Intrinsic::minnum:
          case Intrinsic::maxnum:
          case Intrinsic::minimum:
          case Intrinsic::maximum:
          case Intrinsic::minimumnum:
          case Intrinsic::maximumnum:
            report_fatal_error("GCE math intrinsics require an external math library");
          default:
            break;
          }
        }
        if (I.isAtomic() || isa<FenceInst>(I))
          report_fatal_error("GCE atomics are unsupported");
        if (const auto *A = dyn_cast<AllocaInst>(&I))
          if (!A->isStaticAlloca())
            report_fatal_error("GCE VLA/dynamic alloca is unsupported");
        if (const auto *C = dyn_cast<CallBase>(&I))
          if (C->isInlineAsm())
            report_fatal_error("GCE user inline assembly is unsupported");
      }
    return SelectionDAGISel::runOnMachineFunction(MF);
  }
  void Select(SDNode *N) override {
    if (N->isMachineOpcode()) {
      N->setNodeId(-1);
      return;
    }
    SDLoc L(N);
    auto &D = *CurDAG;
    auto Imm = [&](int64_t V) {
      return D.getTargetConstant(uint32_t(V), L, MVT::i32);
    };
    unsigned Op = 0;
    switch (N->getOpcode()) {
    case ISD::ADD:
      Op = GCE::ADDrr;
      break;
    case ISD::SUB:
      Op = GCE::SUBrr;
      break;
    case ISD::MUL:
      Op = GCE::MULrr;
      break;
    case ISD::AND:
      Op = GCE::ANDrr;
      break;
    case ISD::OR:
      Op = GCE::ORRrr;
      break;
    case ISD::XOR:
      Op = GCE::XORrr;
      break;
    case ISD::SHL:
      Op = GCE::LSLrr;
      break;
    case ISD::SRL:
      Op = GCE::LSRrr;
      break;
    case ISD::Constant:
      D.SelectNodeTo(N, GCE::LDI, MVT::i32,
                     Imm(cast<ConstantSDNode>(N)->getSExtValue()));
      return;
    case ISD::FrameIndex:
      D.SelectNodeTo(N, GCE::FRAMEADDR, MVT::i32,
                     D.getTargetFrameIndex(
                         cast<FrameIndexSDNode>(N)->getIndex(), MVT::i32),
                     Imm(0));
      return;
    case GCEISD::ADDR:
      D.SelectNodeTo(N, GCE::LDI, MVT::i32, N->getOperand(0));
      return;
    case ISD::LOAD: {
      auto *M = cast<LoadSDNode>(N);
      if (M->getMemoryVT() != MVT::i32 || M->isAtomic())
        report_fatal_error("unsupported GCE load");
      auto *R =
          D.getMachineNode(GCE::LOAD32, L, D.getVTList(MVT::i32, MVT::Other),
                           {M->getBasePtr(), Imm(0), M->getChain()});
      D.setNodeMemRefs(R, {M->getMemOperand()});
      ReplaceNode(N, R);
      return;
    }
    case ISD::STORE: {
      auto *M = cast<StoreSDNode>(N);
      unsigned Bits = M->getMemoryVT().getSizeInBits();
      if ((Bits == 1 || Bits == 8 || Bits == 16) && !M->isAtomic()) {
        auto *R = D.getMachineNode(GCE::STOREN, L, MVT::Other,
                                   {M->getValue(), M->getBasePtr(),
                                    Imm((1u << Bits) - 1), M->getChain()});
        auto Flags = M->getMemOperand()->getFlags() | MachineMemOperand::MOLoad;
        auto *MMO = D.getMachineFunction().getMachineMemOperand(
            MachinePointerInfo(), Flags, 4, Align(4));
        D.setNodeMemRefs(R, {MMO});
        ReplaceNode(N, R);
        return;
      }
      if (M->getMemoryVT() != MVT::i32 || M->isAtomic())
        report_fatal_error("unsupported GCE store");
      auto *R = D.getMachineNode(
          GCE::STORE32, L, MVT::Other,
          {M->getValue(), M->getBasePtr(), Imm(0), M->getChain()});
      D.setNodeMemRefs(R, {M->getMemOperand()});
      ReplaceNode(N, R);
      return;
    }
    case ISD::BR:
      D.SelectNodeTo(N, GCE::JUMPrel, MVT::Other, N->getOperand(1),
                     N->getOperand(0));
      return;
    case GCEISD::SELECT:
      Op = GCE::SELECT_CC;
      break;
    case ISD::CALLSEQ_START:
    case ISD::CALLSEQ_END:
    case GCEISD::BCC:
    case GCEISD::CALL:
    case GCEISD::CALLR:
    case GCEISD::RET: {
      Op = N->getOpcode() == GCEISD::BCC          ? GCE::BCC
           : N->getOpcode() == GCEISD::CALL       ? GCE::CALL
           : N->getOpcode() == GCEISD::CALLR      ? GCE::CALLR
           : N->getOpcode() == ISD::CALLSEQ_START ? GCE::ADJCALLSTACKDOWN
           : N->getOpcode() == ISD::CALLSEQ_END   ? GCE::ADJCALLSTACKUP
                                                  : GCE::RET;
      SmallVector<SDValue> A;
      SDValue Glue;
      for (unsigned I = 1; I < N->getNumOperands(); ++I)
        if (N->getOperand(I).getValueType() == MVT::Glue)
          Glue = N->getOperand(I);
        else
          A.push_back(N->getOperand(I));
      A.push_back(N->getOperand(0));
      if (Glue)
        A.push_back(Glue);
      D.SelectNodeTo(N, Op, N->getVTList(), A);
      return;
    }
    default:
      SelectCode(N);
      return;
    }
    SmallVector<SDValue> A(N->op_begin(), N->op_end());
    D.SelectNodeTo(N, Op, N->getVTList(), A);
  }
#include "GCEGenDAGISel.inc"
};
class GCEISelLegacy : public SelectionDAGISelLegacy {
public:
  static char ID;
  explicit GCEISelLegacy(GCETargetMachine &TM)
      : SelectionDAGISelLegacy(ID, std::make_unique<GCEISel>(TM)) {}
};
char GCEISelLegacy::ID = 0;
} // namespace
INITIALIZE_PASS(GCEISelLegacy, "gce-isel", "GCE instruction selection", false,
                false)
FunctionPass *llvm::createGCEISelDag(GCETargetMachine &TM) {
  return new GCEISelLegacy(TM);
}
