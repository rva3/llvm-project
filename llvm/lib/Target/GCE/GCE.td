// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
include "llvm/Target/Target.td"
include "GCERegisterInfo.td"
include "GCEInstrInfo.td"
defm : RemapAllTargetPseudoPointerOperands<UREG>;
def GCEInstrInfo : InstrInfo;
def : Processor<"generic", NoItineraries, []>;
def : Processor<"v3", NoItineraries, []>;
def GCE : Target { let InstructionSet = GCEInstrInfo; }
