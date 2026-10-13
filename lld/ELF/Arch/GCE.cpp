// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "InputFiles.h"
#include "Symbols.h"
#include "Target.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::ELF;
using namespace llvm::support::endian;
using namespace lld;
using namespace lld::elf;

namespace {
class GCE final : public TargetInfo {
public:
  explicit GCE(Ctx &ctx) : TargetInfo(ctx) {
    defaultImageBase = 0xf7600000;
    defaultMaxPageSize = defaultCommonPageSize = 8;
    symbolicRel = R_GCE_ABS32;
    if (ctx.arg.isPic || ctx.arg.shared || !ctx.sharedFiles.empty() ||
        !ctx.arg.dynamicLinker.empty())
      ErrAlways(ctx) << "GCE supports static executables only; PIC/PIE and "
                        "shared libraries are forbidden";
    if (ctx.arg.ekind != ELF32LEKind)
      ErrAlways(ctx) << "GCE requires ELF32 little-endian objects";
  }
  uint32_t calcEFlags() const override {
    if (ctx.arg.ekind != ELF32LEKind)
      return EF_GCE_ABI_V1;
    for (InputFile *f : ctx.objectFiles)
      if (cast<ObjFile<ELF32LE>>(f)->getObj().getHeader().e_flags !=
          EF_GCE_ABI_V1)
        ErrAlways(ctx) << f << ": incompatible GCE ABI (expected e_flags=1)";
    return EF_GCE_ABI_V1;
  }
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override {
    switch (type) {
    case R_GCE_NONE:
      return R_NONE;
    case R_GCE_ABS32:
    case R_GCE_LDI_ABS32:
      return R_ABS;
    case R_GCE_JUMP_REL32:
    case R_GCE_JUMPC_REL16:
      return R_PC;
    default:
      Err(ctx) << getErrorLoc(ctx, loc) << "unsupported GCE relocation "
               << type;
      return R_NONE;
    }
  }
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override {
    switch (rel.type) {
    case R_GCE_NONE:
      return;
    case R_GCE_ABS32:
    case R_GCE_LDI_ABS32:
      // ABI v1 shares this instruction fixup between LDI and absolute JUMP.
      // Only JUMP (upper word 0x10000001) requires an aligned destination;
      // LDI must still be able to materialize arbitrary byte addresses.
      if (rel.type == R_GCE_LDI_ABS32 && read32le(loc + 4) == 0x10000001)
        checkAlignment(ctx, loc, val, 8, rel);
      // LLD sign-extends ELF32 addresses internally; high physical addresses
      // therefore arrive either zero- or sign-extended to 64 bits.
      checkIntUInt(ctx, loc, val, 32, rel);
      write32le(loc, val);
      return;
    case R_GCE_JUMP_REL32:
    case R_GCE_JUMPC_REL16:
      checkAlignment(ctx, loc, val, 8, rel);
      checkInt(ctx, loc, val, rel.type == R_GCE_JUMP_REL32 ? 32 : 16, rel);
      if (rel.type == R_GCE_JUMP_REL32)
        write32le(loc, val);
      else
        write16le(loc + 4, val);
      return;
    default:
      Err(ctx) << getErrorLoc(ctx, loc) << "unsupported GCE relocation "
               << rel.type;
    }
  }
};
} // namespace
void elf::setGCETargetInfo(Ctx &ctx) { ctx.target.reset(new GCE(ctx)); }
