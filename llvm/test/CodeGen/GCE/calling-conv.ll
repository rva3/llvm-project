; RUN: llc -mtriple=gce-unknown-none -verify-machineinstrs -stop-after=finalize-isel < %s | FileCheck %s
; RUN: llc -mtriple=gce-unknown-none -verify-machineinstrs < %s -o %t
; RUN: llc -mtriple=gce-unknown-none -stop-after=finalize-isel -print-after=finalize-isel -print-regmask-num-regs=-1 < %s -o %t 2>&1 | FileCheck %s --check-prefix=MASK

; The argument CPRs are caller-saved. Only T4-T7 CPRs survive a call.
; MASK: CALL @sink, {{.*}}<regmask $p4 $p5 $r8 $r9 $r10 $r11 $r12 $r14 $cpr0t4 $cpr0t5 $cpr0t6 $cpr0t7 $cpr1t4 $cpr1t5 $cpr1t6 $cpr1t7 $cpr2t4 $cpr2t5 $cpr2t6 $cpr2t7 $cpr3t4 $cpr3t5 $cpr3t6 $cpr3t7>,

; There are sixteen fixed argument word slots. The seventeenth word is at
; incoming SP+0 and the outgoing call frame remains eight-byte aligned.
declare i32 @sink(i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32)

define i32 @call_seventeen() {
; CHECK-LABEL: name: call_seventeen
; CHECK: ADJCALLSTACKDOWN 8, 0
; CHECK: [[STACK:%[0-9]+]]:ureg = LDI 17
; CHECK: STORE32 killed [[STACK]], $r14, 0
; CHECK-DAG: [[ARG0:%[0-9]+]]:ureg = LDI 1{{$}}
; CHECK-DAG: $r2 = COPY [[ARG0]]
; CHECK-DAG: [[ARG1:%[0-9]+]]:ureg = LDI 2{{$}}
; CHECK-DAG: $r3 = COPY [[ARG1]]
; CHECK-DAG: [[ARG2:%[0-9]+]]:ureg = LDI 3{{$}}
; CHECK-DAG: $r4 = COPY [[ARG2]]
; CHECK-DAG: [[ARG3:%[0-9]+]]:ureg = LDI 4{{$}}
; CHECK-DAG: $r5 = COPY [[ARG3]]
; CHECK-DAG: [[ARG4:%[0-9]+]]:ureg = LDI 5{{$}}
; CHECK-DAG: $cpr0t0 = COPY [[ARG4]]
; CHECK-DAG: [[ARG5:%[0-9]+]]:ureg = LDI 6{{$}}
; CHECK-DAG: $cpr1t0 = COPY [[ARG5]]
; CHECK-DAG: [[ARG6:%[0-9]+]]:ureg = LDI 7{{$}}
; CHECK-DAG: $cpr2t0 = COPY [[ARG6]]
; CHECK-DAG: [[ARG7:%[0-9]+]]:ureg = LDI 8{{$}}
; CHECK-DAG: $cpr3t0 = COPY [[ARG7]]
; CHECK-DAG: [[ARG8:%[0-9]+]]:ureg = LDI 9{{$}}
; CHECK-DAG: $cpr0t1 = COPY [[ARG8]]
; CHECK-DAG: [[ARG9:%[0-9]+]]:ureg = LDI 10{{$}}
; CHECK-DAG: $cpr1t1 = COPY [[ARG9]]
; CHECK-DAG: [[ARG10:%[0-9]+]]:ureg = LDI 11{{$}}
; CHECK-DAG: $cpr2t1 = COPY [[ARG10]]
; CHECK-DAG: [[ARG11:%[0-9]+]]:ureg = LDI 12{{$}}
; CHECK-DAG: $cpr3t1 = COPY [[ARG11]]
; CHECK-DAG: [[ARG12:%[0-9]+]]:ureg = LDI 13{{$}}
; CHECK-DAG: $cpr0t2 = COPY [[ARG12]]
; CHECK-DAG: [[ARG13:%[0-9]+]]:ureg = LDI 14{{$}}
; CHECK-DAG: $cpr1t2 = COPY [[ARG13]]
; CHECK-DAG: [[ARG14:%[0-9]+]]:ureg = LDI 15{{$}}
; CHECK-DAG: $cpr2t2 = COPY [[ARG14]]
; CHECK-DAG: [[ARG15:%[0-9]+]]:ureg = LDI 16{{$}}
; CHECK-DAG: $cpr3t2 = COPY [[ARG15]]
; CHECK: CALL @sink, $r2, $r3, $r4, $r5, $cpr0t0, $cpr1t0, $cpr2t0, $cpr3t0, $cpr0t1, $cpr1t1, $cpr2t1, $cpr3t1, $cpr0t2, $cpr1t2, $cpr2t2, $cpr3t2, csr_gce,
; CHECK: ADJCALLSTACKUP 8, 0
  %result = call i32 @sink(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15, i32 16, i32 17)
  ret i32 %result
}

; Fourteen leading words leave two aligned CPR argument slots for i64.
define i64 @last_register_pair(i32 %a1, i32 %a2, i32 %a3, i32 %a4, i32 %a5, i32 %a6, i32 %a7, i32 %a8, i32 %a9, i32 %a10, i32 %a11, i32 %a12, i32 %a13, i32 %a14, i64 %wide) {
; CHECK-LABEL: name: last_register_pair
; CHECK: fixedStack: []
; CHECK-DAG: [[LOW:%[0-9]+]]:ureg = COPY $cpr2t2
; CHECK-DAG: [[HIGH:%[0-9]+]]:ureg = COPY $cpr3t2
; CHECK: $r2 = COPY [[LOW]]
; CHECK: $r3 = COPY [[HIGH]]
  ret i64 %wide
}

; With fifteen leading words the whole i64 spills; CPR3T2 is left unused.
; The following fixed word must remain on stack even though one CPR is free.
define i64 @spilled_pair(i32 %a1, i32 %a2, i32 %a3, i32 %a4, i32 %a5, i32 %a6, i32 %a7, i32 %a8, i32 %a9, i32 %a10, i32 %a11, i32 %a12, i32 %a13, i32 %a14, i32 %a15, i64 %wide, i32 %last) {
; CHECK-LABEL: name: spilled_pair
; CHECK: fixedStack:
; CHECK-DAG: offset: 0, size: 4
; CHECK-DAG: offset: 4, size: 4
; CHECK-DAG: offset: 8, size: 4
; CHECK-NOT: COPY $cpr3t2
; CHECK: RET $r2, $r3
  %last64 = zext i32 %last to i64
  %result = add i64 %wide, %last64
  ret i64 %result
}
