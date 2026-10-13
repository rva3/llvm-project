// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef LLVM_LIB_TARGET_GCE_MCTARGETDESC_GCEENCODING_H
#define LLVM_LIB_TARGET_GCE_MCTARGETDESC_GCEENCODING_H
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCRegister.h"
namespace llvm {
class MCInst;
class MCInstrInfo;
class MCRegisterInfo;
namespace GCE {
enum Format {
  Binary,
  Assign,
  Not,
  Read,
  Write,
  ReadS,
  WriteS,
  Jump,
  JumpC,
  Mask,
  Move,
  Eoc,
  Marker,
  MaskedWrite
};
enum Fixups {
  fixup_abs32 = FirstTargetFixupKind,
  fixup_rel32,
  fixup_rel16,
  NumFixups
};
unsigned findOpcode(const MCInstrInfo &MII, StringRef Name);
MCRegister findRegister(const MCRegisterInfo &MRI, StringRef Name);
const char *validate(const MCInst &MI, const MCInstrInfo &MII,
                     const MCRegisterInfo &MRI);
bool decode(uint64_t Word, MCInst &MI, const MCInstrInfo &MII,
            const MCRegisterInfo &MRI);
StringRef mnemonic(uint64_t Flags);
} // namespace GCE
} // namespace llvm
#endif
