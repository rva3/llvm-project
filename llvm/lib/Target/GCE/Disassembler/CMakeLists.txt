add_llvm_component_library(LLVMGCEDisassembler
  GCEDisassembler.cpp
  LINK_COMPONENTS GCEDesc GCEInfo MC MCDisassembler Support
  ADD_TO_COMPONENT GCE
)
add_dependencies(LLVMGCEDisassembler GCECommonTableGen)
