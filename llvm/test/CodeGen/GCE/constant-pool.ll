; RUN: llc -mtriple=gce-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=gce-unknown-none -filetype=obj < %s -o %t.o
; RUN: llvm-readobj --relocations %t.o | FileCheck %s --check-prefix=RELOC

; CTTZ's generic expansion uses a byte lookup table in the constant pool.
; CHECK: .LCPI0_0:
; CHECK-LABEL: trailing:
; CHECK: ldi {{.*}}, .LCPI0_0
; RELOC: R_GCE_LDI_ABS32
define i32 @trailing(i32 %x) {
  %n = call i32 @llvm.cttz.i32(i32 %x, i1 false)
  ret i32 %n
}

declare i32 @llvm.cttz.i32(i32, i1 immarg)
