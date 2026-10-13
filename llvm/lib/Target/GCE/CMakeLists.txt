add_llvm_component_group(GCE)
set(LLVM_TARGET_DEFINITIONS GCE.td)
tablegen(LLVM GCEGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM GCEGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM GCEGenSubtargetInfo.inc -gen-subtarget)
tablegen(LLVM GCEGenDAGISel.inc -gen-dag-isel)
add_public_tablegen_target(GCECommonTableGen)

add_llvm_target(GCECodeGen
  GCE.cpp
  GCEISelLowering.cpp
  GCEISelDAGToDAG.cpp
  GCEAsmPrinter.cpp
  LINK_COMPONENTS GCEInfo GCEDesc AsmPrinter CodeGen Core MC SelectionDAG Support Target
  ADD_TO_COMPONENT GCE
)
add_subdirectory(TargetInfo)
add_subdirectory(MCTargetDesc)
add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
