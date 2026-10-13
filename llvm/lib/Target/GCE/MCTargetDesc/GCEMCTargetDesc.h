// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef LLVM_LIB_TARGET_GCE_MCTARGETDESC_GCEMCTARGETDESC_H
#define LLVM_LIB_TARGET_GCE_MCTARGETDESC_GCEMCTARGETDESC_H
#include <cstdint>
#define GET_REGINFO_ENUM
#include "GCEGenRegisterInfo.inc"
#define GET_INSTRINFO_ENUM
#include "GCEGenInstrInfo.inc"
#define GET_SUBTARGETINFO_ENUM
#include "GCEGenSubtargetInfo.inc"
#endif
