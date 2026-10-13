// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "GCEMCTargetDesc.h"
#include "GCEEncoding.h"
#include "TargetInfo/GCETargetInfo.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfoELF.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/MathExtras.h"
#define GET_INSTRINFO_MC_DESC
#include "GCEGenInstrInfo.inc"
#define GET_REGINFO_MC_DESC
#include "GCEGenRegisterInfo.inc"
#define GET_SUBTARGETINFO_MC_DESC
#include "GCEGenSubtargetInfo.inc"
using namespace llvm;

namespace {
class GCEAsmInfo : public MCAsmInfoELF {
public:
  GCEAsmInfo(const Triple &, const MCTargetOptions &) {
    CodePointerSize = 4;
    CalleeSaveStackSlotSize = 4;
    MinInstAlignment = 8;
    MaxInstLength = 8;
    IsLittleEndian = true;
    CommentString = "//";
    SupportsDebugInformation = false;
  }
};

class GCEPrinter : public MCInstPrinter {
public:
  using MCInstPrinter::MCInstPrinter;
  std::pair<const char *, uint64_t>
  getMnemonic(const MCInst &MI) const override {
    return {GCE::mnemonic(MII.get(MI.getOpcode()).TSFlags).data(), 0};
  }
  void printRegName(raw_ostream &OS, MCRegister Reg) override {
    OS << StringRef(MRI.getName(Reg)).lower();
  }
  void printInst(const MCInst *MI, uint64_t, StringRef Annot,
                 const MCSubtargetInfo &, raw_ostream &OS) override {
    uint64_t F = MII.get(MI->getOpcode()).TSFlags;
    unsigned Form = (F >> 16) & 15;
    auto Print = [&](unsigned N, bool Hash = true) {
      const MCOperand &O = MI->getOperand(N);
      if (O.isReg())
        printRegName(OS, O.getReg());
      else if (O.isExpr())
        MAI.printExpr(OS, *O.getExpr());
      else {
        if (Hash)
          OS << '#';
        OS << O.getImm();
      }
    };
    auto Mem = [&](unsigned N) {
      OS << '[';
      if (MI->getOperand(N).isReg())
        Print(N);
      else {
        OS << "subsys=";
        Print(N, false);
      }
      if (!MI->getOperand(N).isReg() || MI->getOperand(N + 1).getImm() ||
          Form == GCE::ReadS || Form == GCE::WriteS) {
        OS << ", ";
        Print(N + 1);
      }
      OS << ']';
    };
    OS << '\t' << GCE::mnemonic(F);
    if (Form == GCE::Marker) {
      int64_t Flags = MI->getOperand(0).getImm();
      bool First = true;
      for (unsigned I = 0; I < 3; ++I)
        if (Flags & (1ULL << I)) {
          OS << (First ? " " : ", ")
             << (I == 0   ? "prefetch"
                 : I == 1 ? "inc"
                          : "suspend");
          First = false;
        }
    } else if (Form == GCE::Read || Form == GCE::ReadS) {
      OS << ' ';
      Print(0);
      OS << ", ";
      Mem(1);
    } else if (Form == GCE::Write || Form == GCE::MaskedWrite ||
               Form == GCE::WriteS) {
      OS << ' ';
      Mem(0);
      OS << ", ";
      Print(2);
    } else {
      for (unsigned I = 0; I < MI->getNumOperands(); ++I) {
        OS << (I ? ", " : " ");
        bool Target = I == 0 && (Form == GCE::Jump || Form == GCE::JumpC);
        if (Target && MI->getOperand(I).isImm() &&
            (Form == GCE::JumpC || !((F >> 8) & 31)) &&
            MI->getOperand(I).getImm() >= 0)
          OS << '+';
        bool AbsoluteJump = Form == GCE::Jump && ((F >> 8) & 31);
        if (AbsoluteJump && MI->getOperand(I).isExpr())
          OS << '#';
        Print(I, !Target || AbsoluteJump);
      }
    }
    printAnnotation(OS, Annot);
  }
};

class GCEEmitter : public MCCodeEmitter {
  const MCInstrInfo &MII;
  MCContext &Ctx;

public:
  GCEEmitter(const MCInstrInfo &Info, MCContext &Context)
      : MII(Info), Ctx(Context) {}
  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &) const override {
    uint64_t F = MII.get(MI.getOpcode()).TSFlags;
    uint64_t Op = F & 255, S = (F >> 8) & 31, Form = (F >> 16) & 15;
    bool L = F & (1 << 20), R = F & (1 << 21), A = F & (1 << 22);
    uint64_t W = Op << 56;
    auto V = [&](unsigned N, unsigned Kind = GCE::fixup_abs32) -> uint64_t {
      const MCOperand &O = MI.getOperand(N);
      if (O.isReg())
        return Ctx.getRegisterInfo()->getEncodingValue(O.getReg());
      if (O.isImm())
        return O.getImm();
      auto Fixup =
          MCFixup::create(0, O.getExpr(), Kind, Kind != GCE::fixup_abs32);
      Fixups.push_back(Fixup);
      return 0;
    };
    auto Old = [&](unsigned N) -> uint64_t {
      unsigned E = V(N);
      return E >= 0x100 ? E - 0x100 : E - 32;
    };
    switch (Form) {
    case GCE::Binary:
      W |= (1ULL << 55) | (uint64_t(L) << 54) | (uint64_t(R) << 53) |
           (S << 48) | (V(0) << 32) | (V(1) << 16) | V(2);
      break;
    case GCE::Assign:
      W |= (1ULL << 55) | (V(0) << 32) | uint32_t(V(1));
      break;
    case GCE::Not:
      W |= (1ULL << 55) | (uint64_t(L) << 54) | (S << 48) | (V(0) << 32) |
           (V(1) << 16);
      break;
    case GCE::Read:
      W |= (1ULL << 54) | (uint64_t(A) << 55) | ((A ? Old(1) : V(1)) << 48) |
           (V(2) << 32) | Old(0);
      break;
    case GCE::Write:
    case GCE::MaskedWrite:
      W |= (uint64_t(A) << 55) | (uint64_t(R) << 54) |
           ((A ? Old(0) : V(0)) << 48) |
           ((V(1) | uint64_t(Form == GCE::MaskedWrite)) << 32) |
           (R ? Old(2) : uint32_t(V(2)));
      break;
    case GCE::ReadS:
      W |= (1ULL << 54) | (V(1) << 48) | (V(0) << 32) |
           ((V(2) | uint64_t(A)) << 16);
      break;
    case GCE::WriteS:
      W |= (uint64_t(R) << 54) | (V(0) << 48) | ((V(1) | uint64_t(A)) << 32) |
           uint32_t(V(2));
      break;
    case GCE::Jump:
      W |= (S << 32) | uint32_t(V(0, S ? GCE::fixup_abs32 : GCE::fixup_rel32));
      break;
    case GCE::JumpC:
      W |= (uint64_t(A) << 55) | (uint64_t(L) << 54) | (uint64_t(R) << 53) |
           (S << 48) | ((V(0, GCE::fixup_rel16) & 65535) << 32) | (V(1) << 16) |
           V(2);
      break;
    case GCE::Mask:
      W |= uint64_t(uint32_t(~uint32_t(V(0))));
      break;
    case GCE::Move:
      W |= (1ULL << 55) | (Old(0) << 48) | V(1);
      break;
    case GCE::Eoc:
      W |= 1;
      break;
    case GCE::Marker: {
      unsigned Flags = V(0);
      if (Flags & 1)
        W |= 0x130000;
      if (!(Flags & 2))
        W |= 1ULL << 48;
      if (!(Flags & 4))
        W |= 1ULL << 53;
      break;
    }
    default:
      llvm_unreachable("invalid GCE format");
    }
    support::endian::write(CB, W, endianness::little);
  }
};

// Project-local ELF ABI. All relocations are resolved by the static linker.
class GCEObjectWriter : public MCELFObjectTargetWriter {
  const MCAsmBackend &Backend;

public:
  explicit GCEObjectWriter(const MCAsmBackend &Backend)
      : MCELFObjectTargetWriter(false, ELF::ELFOSABI_NONE, ELF::EM_GCE, true),
        Backend(Backend) {}
  unsigned getRelocType(const MCFixup &Fixup, const MCValue &,
                        bool) const override {
    switch (Fixup.getKind()) {
    case FK_Data_4:
      return ELF::R_GCE_ABS32;
    case GCE::fixup_abs32:
      return ELF::R_GCE_LDI_ABS32;
    case GCE::fixup_rel32:
      return ELF::R_GCE_JUMP_REL32;
    case GCE::fixup_rel16:
      return ELF::R_GCE_JUMPC_REL16;
    default:
      Backend.getContext().reportError(Fixup.getLoc(),
                                       "unsupported GCE relocation width");
      return ELF::R_GCE_NONE;
    }
  }
};
class GCEELFStreamer : public MCTargetStreamer {
public:
  explicit GCEELFStreamer(MCStreamer &S) : MCTargetStreamer(S) {
    static_cast<MCELFStreamer &>(S).getWriter().setELFHeaderEFlags(
        ELF::EF_GCE_ABI_V1);
  }
};
class GCEBackend : public MCAsmBackend {
public:
  GCEBackend() : MCAsmBackend(endianness::little) {}
  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return std::make_unique<GCEObjectWriter>(*this);
  }
  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override {
    static const MCFixupKindInfo Infos[] = {{"gce_abs32", 0, 32, 0},
                                            {"gce_rel32", 0, 32, 0},
                                            {"gce_rel16", 32, 16, 0}};
    if (Kind < FirstTargetFixupKind)
      return MCAsmBackend::getFixupKindInfo(Kind);
    assert(Kind < GCE::NumFixups);
    return Infos[Kind - FirstTargetFixupKind];
  }
  void applyFixup(const MCFragment &Frag, const MCFixup &Fixup,
                  const MCValue &Target, uint8_t *Data, uint64_t Value,
                  bool IsResolved) override {
    maybeAddReloc(Frag, Fixup, Target, Value, IsResolved);
    unsigned Kind = Fixup.getKind();
    // A deferred absolute expression can resolve entirely in the assembler.
    // Use the instruction tag to distinguish JUMP from LDI, just as LLD does
    // for the shared ABI v1 relocation when a symbol remains unresolved.
    if (Kind == GCE::fixup_abs32 &&
        support::endian::read32le(Data + 4) == 0x10000001 && (Value & 7)) {
      getContext().reportError(Fixup.getLoc(),
                               "GCE absolute jump target is unaligned");
      return;
    }
    if (Kind == GCE::fixup_abs32 && !isUInt<32>(Value) &&
        !isInt<32>(int64_t(Value))) {
      getContext().reportError(Fixup.getLoc(),
                               "GCE 32-bit immediate out of range");
      return;
    }
    if (Kind == GCE::fixup_rel16 || Kind == GCE::fixup_rel32) {
      unsigned Bits = Kind == GCE::fixup_rel16 ? 16 : 32;
      if (!isIntN(Bits, int64_t(Value)) || (Value & 7)) {
        getContext().reportError(
            Fixup.getLoc(),
            "GCE branch displacement out of range or unaligned");
        return;
      }
    }
    MCFixupKindInfo Info = getFixupKindInfo(Kind);
    unsigned ByteOffset = Info.TargetOffset / 8;
    unsigned NumBytes = (Info.TargetSize + 7) / 8;
    for (unsigned I = 0; I < NumBytes; ++I)
      Data[ByteOffset + I] |= uint8_t(Value >> (8 * I));
  }
  unsigned getMinimumNopSize() const override { return 8; }
  unsigned getMaximumNopSize(const MCSubtargetInfo &) const override {
    return 8;
  }
  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *) const override {
    if (Count % 8)
      return false;
    while (Count) {
      support::endian::write(OS, uint64_t(0x1000000000000008ULL),
                             endianness::little);
      Count -= 8;
    }
    return true;
  }
};
} // namespace

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeGCETargetMC() {
  Target &T = getTheGCETarget();
  TargetRegistry::RegisterObjectTargetStreamer(
      T, [](MCStreamer &S, const MCSubtargetInfo &) -> MCTargetStreamer * {
        return new GCEELFStreamer(S);
      });
  RegisterMCAsmInfo<GCEAsmInfo> A(T);
  TargetRegistry::RegisterMCInstrInfo(T, []() {
    auto *Info = new MCInstrInfo();
    InitGCEMCInstrInfo(Info);
    return Info;
  });
  TargetRegistry::RegisterMCRegInfo(T, [](const Triple &) {
    auto *Info = new MCRegisterInfo();
    InitGCEMCRegisterInfo(Info, GCE::R15);
    return Info;
  });
  TargetRegistry::RegisterMCSubtargetInfo(
      T, [](const Triple &TT, StringRef CPU, StringRef FS) {
        if (CPU.empty())
          CPU = "generic";
        return createGCEMCSubtargetInfoImpl(TT, CPU, CPU, FS);
      });
  TargetRegistry::RegisterMCInstPrinter(
      T,
      [](const Triple &, unsigned Variant, const MCAsmInfo &AI,
         const MCInstrInfo &II, const MCRegisterInfo &RI) -> MCInstPrinter * {
        return Variant == 0 ? new GCEPrinter(AI, II, RI) : nullptr;
      });
  TargetRegistry::RegisterMCCodeEmitter(
      T, [](const MCInstrInfo &II, MCContext &Ctx) -> MCCodeEmitter * {
        return new GCEEmitter(II, Ctx);
      });
  TargetRegistry::RegisterMCAsmBackend(
      T,
      [](const Target &, const MCSubtargetInfo &, const MCRegisterInfo &,
         const MCTargetOptions &) -> MCAsmBackend * {
        return new GCEBackend();
      });
}
