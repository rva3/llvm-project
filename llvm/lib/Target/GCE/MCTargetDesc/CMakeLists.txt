add_llvm_component_library(LLVMGCEDesc
  GCEMCTargetDesc.cpp
  GCEEncoding.cpp
  LINK_COMPONENTS GCEInfo MC Support TargetParser
  ADD_TO_COMPONENT GCE
)
add_dependencies(LLVMGCEDesc GCECommonTableGen)
