; RUN: llc -mtriple=gce-unknown-none -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=gce-unknown-none -stop-after=finalize-isel -verify-machineinstrs < %s | FileCheck %s --check-prefix=MIR

; Exercise LLVM's va_arg legalizer, independently of Clang's inline cursor
; arithmetic. An unnamed i64 following an i32 must skip one stack word.
; The copied list must retain its own cursor when the original list advances.
define i64 @gce_ir_varargs(i32 %tag, ...) {
; CHECK-LABEL: gce_ir_varargs:
; CHECK: and
; CHECK: jump #__gce_indirect
entry:
  %ap = alloca ptr, align 4
  %copy = alloca ptr, align 4
  call void @llvm.va_start(ptr %ap)
  %word = va_arg ptr %ap, i32
  call void @llvm.va_copy(ptr %copy, ptr %ap)
  %wide = va_arg ptr %ap, i64
  %tail = va_arg ptr %ap, i32
  %wide_copy = va_arg ptr %copy, i64
  call void @llvm.va_end(ptr %copy)
  call void @llvm.va_end(ptr %ap)
  %t64 = zext i32 %tag to i64
  %w64 = zext i32 %word to i64
  %end64 = zext i32 %tail to i64
  %s0 = add i64 %wide, %wide_copy
  %s1 = add i64 %s0, %t64
  %s2 = add i64 %s1, %w64
  %s3 = add i64 %s2, %end64
  ret i64 %s3
}

; Sixteen fixed words use R2-R5 and twelve CPRs; the seventeenth is at SP+0.
; The first
; unnamed word starts at SP+4, not at the rounded eight-byte frame boundary.
define i32 @gce_ir_named_stack(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e,
                             i32 %f, i32 %g, i32 %h, i32 %i, i32 %j, i32 %k,
                             i32 %l, i32 %m, i32 %n, i32 %o, i32 %p, i32 %q, ...) {
; CHECK-LABEL: gce_ir_named_stack:
; CHECK: jump #__gce_indirect
; MIR-LABEL: name: gce_ir_named_stack
; MIR: fixedStack:
; MIR-DAG: offset: 0, size: 4
; MIR-DAG: offset: 4, size: 4
entry:
  %ap = alloca ptr, align 4
  call void @llvm.va_start(ptr %ap)
  %value = va_arg ptr %ap, i32
  call void @llvm.va_end(ptr %ap)
  %result = add i32 %q, %value
  ret i32 %result
}

; R3-R5 and the CPR argument slots remain unused for unnamed arguments.
; Stack locations are 0 (i32), 8/12 (i64), and 16 (i32), with total size 24.
define i64 @gce_ir_call() {
; CHECK-LABEL: gce_ir_call:
; CHECK: jump #gce_ir_varargs
; MIR-LABEL: name: gce_ir_call
; MIR: ADJCALLSTACKDOWN 24, 0
; MIR: [[OFF16:%[0-9]+]]:ureg = LDI 16
; MIR: [[ADDR16:%[0-9]+]]:ureg = ADDrr $r14, killed [[OFF16]]
; MIR: [[TAIL:%[0-9]+]]:ureg = LDI 13
; MIR: STORE32 killed [[TAIL]], killed [[ADDR16]], 0
; MIR: [[OFF12:%[0-9]+]]:ureg = LDI 12
; MIR: [[ADDR12:%[0-9]+]]:ureg = ADDrr $r14, killed [[OFF12]]
; MIR: [[HIGH:%[0-9]+]]:ureg = LDI 305419896
; MIR: STORE32 killed [[HIGH]], killed [[ADDR12]], 0
; MIR: [[OFF8:%[0-9]+]]:ureg = LDI 8
; MIR: [[ADDR8:%[0-9]+]]:ureg = ADDrr $r14, killed [[OFF8]]
; MIR: [[LOW:%[0-9]+]]:ureg = LDI -1698898192
; MIR: STORE32 killed [[LOW]], killed [[ADDR8]], 0
; MIR: [[WORD:%[0-9]+]]:ureg = LDI 11
; MIR: STORE32 killed [[WORD]], $r14, 0
; MIR: [[TAG:%[0-9]+]]:ureg = LDI 7
; MIR: $r2 = COPY [[TAG]]
; MIR: CALL @gce_ir_varargs, $r2, csr_gce,
; MIR: ADJCALLSTACKUP 24, 0
entry:
  %result = call i64 (i32, ...) @gce_ir_varargs(i32 7, i32 11,
                                             i64 1311768467463790320, i32 13)
  ret i64 %result
}

declare void @llvm.va_start(ptr)
declare void @llvm.va_copy(ptr, ptr)
declare void @llvm.va_end(ptr)
