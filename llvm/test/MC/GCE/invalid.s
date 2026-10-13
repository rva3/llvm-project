// RUN: not llvm-mc -triple=gce-unknown-none %s 2>&1 | FileCheck %s
add r2, r3, #65536
// CHECK: error: expected unified registers or unsigned 16-bit operands
write_s [r2, #0], r3
// CHECK: error: WRITE_S requires SPR address high and aligned imm16 low
jeq +32768, #0, #0
// CHECK: error: invalid conditional jump
jump r2
// CHECK: error: register JUMP is unreliable
