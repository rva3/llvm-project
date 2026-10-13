// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "MCTargetDesc/GCEEncoding.h"
#include "TargetInfo/GCETargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
using namespace llvm;
namespace {
class GCEDisassembler : public MCDisassembler {
  std::unique_ptr<MCInstrInfo> MII;

public:
  GCEDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx), MII(getTheGCETarget().createMCInstrInfo()) {}
  DecodeStatus getInstruction(MCInst &Inst, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t,
                              raw_ostream &) const override {
    if (Bytes.size() < 8) {
      Size = 0;
      return Fail;
    }
    Size = 8;
    return GCE::decode(support::endian::read64le(Bytes.data()), Inst, *MII,
                       *getContext().getRegisterInfo())
               ? Success
               : Fail;
  }
};
} // namespace
extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeGCEDisassembler() {
  TargetRegistry::RegisterMCDisassembler(
      getTheGCETarget(),
      [](const Target &, const MCSubtargetInfo &STI, MCContext &Ctx)
          -> MCDisassembler * { return new GCEDisassembler(STI, Ctx); });
}
