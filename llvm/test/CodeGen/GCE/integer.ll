; RUN: llc -mtriple=gce-unknown-none -verify-machineinstrs < %s | FileCheck %s

define i32 @add(i32 %a, i32 %b) {
; CHECK-LABEL: add:
; CHECK: add r2, r2, r3
; CHECK: add r13, r15, #0
; CHECK: jump #__gce_indirect
  %c = add i32 %a, %b
  ret i32 %c
}

define i32 @minimum(i32 %a, i32 %b) {
; CHECK-LABEL: minimum:
; CHECK: xor
; CHECK: xor
; CHECK: {{j[a-z]+}} +16,
; CHECK-NEXT: jump #.LBB
  %c = icmp slt i32 %a, %b
  %s = select i1 %c, i32 %a, i32 %b
  ret i32 %s
}

define i32 @byte_rmw(ptr %p, i32 %a) {
; CHECK-LABEL: byte_rmw:
; CHECK: read r0, [r13]
; CHECK: write [r13], r0
  %t = trunc i32 %a to i8
  store volatile i8 %t, ptr %p
  %b = load volatile i8, ptr %p
  %v = sext i8 %b to i32
  ret i32 %v
}

define i64 @divide(i64 %a, i64 %b) {
; CHECK-LABEL: divide:
; CHECK: write [r13], r0
; CHECK: ldi r15, .Lgce_return
; CHECK: jump #__udivdi3
; CHECK: read r13, [r13]
; CHECK: jump #__gce_indirect
  %v = udiv i64 %a, %b
  ret i64 %v
}
