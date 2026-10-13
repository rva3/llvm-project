; RUN: not --crash llc -mtriple=gce-unknown-none -relocation-model=pic < %s 2>&1 | FileCheck %s
; CHECK: GCE requires static code; PIC/PIE and JIT are unsupported
define i32 @f() { ret i32 0 }
