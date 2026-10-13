; RUN: llc -mtriple=gce-unknown-none -verify-machineinstrs -stop-after=finalize-isel < %s | FileCheck %s
; RUN: llc -mtriple=gce-unknown-none -verify-machineinstrs < %s -o %t

; Expanding a SELECT_CC while materializing outgoing arguments splits a block
; between call-frame setup and teardown. Each inserted block must inherit the
; active frame size, including successive selects in the same original block.
@rows = external global i1
@cols = external global i1
declare i32 @print(ptr, ptr, ptr, ...)

define i32 @select_callframe() {
entry:
  %col = load i1, ptr @cols, align 4
  %row = load i1, ptr @rows, align 4
  %rowval = select i1 %row, i32 15, i32 1
  %colval = select i1 %col, i32 34, i32 5
  %result = call i32 (ptr, ptr, ptr, ...) @print(ptr null, ptr null, ptr null,
                                               i32 %rowval, i32 %colval)
  ret i32 %result
}

; CHECK-LABEL: name: select_callframe
; CHECK: ADJCALLSTACKDOWN 8, 0
; CHECK: BCC %bb.
; CHECK: bb.{{[0-9]+}}.entry (call-frame-size 8):
; CHECK: bb.{{[0-9]+}}.entry (call-frame-size 8):
; CHECK: CALL @print
; CHECK: ADJCALLSTACKUP 8, 0
