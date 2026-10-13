; RUN: llc -mtriple=gce-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=gce-unknown-none -verify-machineinstrs -filetype=obj < %s -o %t
define float @add(float %a, float %b) {
; CHECK-LABEL: add:
; CHECK: __addsf3
  %r = fadd float %a, %b
  ret float %r
}
define double @multiply(double %a, double %b) {
; CHECK-LABEL: multiply:
; CHECK: __muldf3
  %r = fmul double %a, %b
  ret double %r
}
define double @convert(i64 %x) {
; CHECK-LABEL: convert:
; CHECK: __floatdidf
  %r = sitofp i64 %x to double
  ret double %r
}
define double @pass_odd(i32 %skip, double %d) {
; CHECK-LABEL: pass_odd:
; CHECK-DAG: add r2, r4, #0
; CHECK-DAG: add r3, r5, #0
  ret double %d
}
define double @load_store(ptr %src, ptr %dst) {
  %x = load volatile double, ptr %src, align 8
  store volatile double %x, ptr %dst, align 8
  ret double %x
}
