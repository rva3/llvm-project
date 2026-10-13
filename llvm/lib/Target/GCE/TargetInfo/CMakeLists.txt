add_llvm_component_library(LLVMGCEInfo
  GCETargetInfo.cpp
  LINK_COMPONENTS MC Support TargetParser
  ADD_TO_COMPONENT GCE
)
