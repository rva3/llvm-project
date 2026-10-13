add_llvm_component_library(LLVMGCEAsmParser
  GCEAsmParser.cpp
  LINK_COMPONENTS GCEDesc GCEInfo MC MCParser Support
  ADD_TO_COMPONENT GCE
)
add_dependencies(LLVMGCEAsmParser GCECommonTableGen)
