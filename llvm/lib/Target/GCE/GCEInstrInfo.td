// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// TSFlags: opcode[7:0], sub-op[12:8], format[19:16], register
// operand flags L[20], R[21], address/target[22]. MC uses the Rust
// reference's two different register numbering spaces explicitly.
def branch_target : Operand<i32> {
  let OperandType = "OPERAND_PCREL";
}
class GCEInst<int Opcode, int SubOp, int Format, int L = 0, int R = 0,
              int A = 0> : Instruction {
  let Namespace = "GCE";
  let Size = 8;
  let OutOperandList = !cond(
    !or(!eq(Format,0),!eq(Format,1),!eq(Format,2),!eq(Format,5)): (outs UREG:$dst),
    !or(!eq(Format,3),!eq(Format,10)): (outs OLDREG:$dst),
    true: (outs));
  let InOperandList = !cond(
    !eq(Format,0): (ins !if(L,UREG,i32imm):$lhs, !if(R,UREG,i32imm):$rhs),
    !eq(Format,1): (ins i32imm:$imm),
    !eq(Format,2): (ins !if(L,UREG,i32imm):$src),
    !eq(Format,3): (ins !if(A,OLDREG,i32imm):$addr, i32imm:$offset),
    !or(!eq(Format,4),!eq(Format,13)):
      (ins !if(A,GPR,i32imm):$addr, i32imm:$offset, !if(R,GPR,i32imm):$src),
    !eq(Format,5): (ins !if(A,SPR,i32imm):$high, i32imm:$low),
    !eq(Format,6): (ins !if(A,SPR,i32imm):$high, i32imm:$low, !if(R,UREG,i32imm):$src),
    !eq(Format,7): (ins !if(SubOp,i32imm,branch_target):$target),
    !eq(Format,8): (ins !if(A,UREG,branch_target):$target,
                      !if(L,UREG,i32imm):$lhs, !if(R,UREG,i32imm):$rhs),
    !eq(Format,9): (ins i32imm:$mask),
    !eq(Format,10): (ins i64imm:$imm),
    !eq(Format,12): (ins i32imm:$flags),
    true: (ins));
  let AsmString = "";
  let Pattern = [];
  let hasSideEffects = 0;
  let mayLoad = 0;
  let mayStore = 0;
  let TSFlags = !or(Opcode, !shl(SubOp,8), !shl(Format,16),
                   !shl(L,20), !shl(R,21), !shl(A,22));
}
multiclass Binary<int Op> {
  def rr : GCEInst<0xA0, Op, 0, 1, 1>;
  def ri : GCEInst<0xA0, Op, 0, 1, 0>;
  def ir : GCEInst<0xA0, Op, 0, 0, 1>;
  def ii : GCEInst<0xA0, Op, 0, 0, 0>;
}
defm ADD : Binary<1>;
defm SUB : Binary<2>;
defm MUL : Binary<3>;
defm XOR : Binary<8>;
defm ORR : Binary<10>;
defm AND : Binary<11>;
defm LSL : Binary<12>;
defm LSR : Binary<13>;
def LDI : GCEInst<0xA0, 0, 1>;
def NOTr : GCEInst<0xA0, 9, 2, 1>;
def NOTi : GCEInst<0xA0, 9, 2, 0>;
let mayLoad = 1 in {
  def READr : GCEInst<0x01, 0, 3, 0, 0, 1>;
  def READs : GCEInst<0x01, 0, 3>;
  def READ_Sr : GCEInst<0x80, 0, 5, 0, 0, 1>;
  def READ_Ss : GCEInst<0x80, 0, 5>;
}
multiclass Store<int Opcode, int Format> {
  def rr : GCEInst<Opcode, 0, Format, 0, 1, 1>;
  def ri : GCEInst<Opcode, 0, Format, 0, 0, 1>;
  def sr : GCEInst<Opcode, 0, Format, 0, 1, 0>;
  def si : GCEInst<Opcode, 0, Format, 0, 0, 0>;
}
let mayStore = 1 in {
  defm WRITE : Store<0x04, 4>;
  defm MWRITE : Store<0x04, 13>;
  defm WRITE_S : Store<0x90, 6>;
  defm MWRITE_S : Store<0x91, 6>;
}
let isBranch = 1, isTerminator = 1, isBarrier = 1 in {
  def JUMPrel : GCEInst<0x10, 0, 7>;
  def JUMPabs : GCEInst<0x10, 1, 7>;
}
multiclass CondJump<int Cond> {
  def relrr : GCEInst<0xB1, Cond, 8, 1, 1>;
  def relri : GCEInst<0xB1, Cond, 8, 1, 0>;
  def relir : GCEInst<0xB1, Cond, 8, 0, 1>;
  def relii : GCEInst<0xB1, Cond, 8, 0, 0>;
  def absrr : GCEInst<0xB0, Cond, 8, 1, 1, 1>;
  def absri : GCEInst<0xB0, Cond, 8, 1, 0, 1>;
  def absir : GCEInst<0xB0, Cond, 8, 0, 1, 1>;
  def absii : GCEInst<0xB0, Cond, 8, 0, 0, 1>;
}
let isBranch = 1, isTerminator = 1 in {
  defm JEQ : CondJump<0>;
  defm JNE : CondJump<1>;
  defm JGE : CondJump<2>;
  defm JLE : CondJump<3>;
  defm JGT : CondJump<4>;
  defm JLT : CondJump<5>;
}
def MASK : GCEInst<0x02, 0, 9> { let hasSideEffects = 1; let Defs = [R0]; }
def MOVE : GCEInst<0x02, 1, 10> { let hasSideEffects = 1; let Defs = [R1]; }
def EOC : GCEInst<0x40, 0, 11> {
  let isTerminator = 1;
  let isBarrier = 1;
  let hasSideEffects = 1;
}
def MARKER : GCEInst<0x40, 0, 12> { let hasSideEffects = 1; }

// Code generation pseudos retain memory, branch and call semantics until emission.
class GCEPseudo<dag Out, dag In> : Instruction {
  let Namespace = "GCE";
  let OutOperandList = Out;
  let InOperandList = In;
  let isPseudo = 1;
  let hasSideEffects = 0;
  let mayLoad = 0;
  let mayStore = 0;
  let Size = 8;
}
def LOAD32 : GCEPseudo<(outs UREG:$dst), (ins UREG:$base, i32imm:$off)> {
  let mayLoad = 1; let Defs = [R13, SPR0]; let Size = 32;
}
def STORE32 : GCEPseudo<(outs), (ins UREG:$src, UREG:$base, i32imm:$off)> {
  let mayStore = 1; let Defs = [R13, SPR0, SPR1]; let Size = 48;
}
// Keep a whole RMW indivisible to the scheduler. Disjoint source-language
// byte stores may have parallel DAG chains but touch the same physical word.
def STOREN : GCEPseudo<(outs), (ins UREG:$src, UREG:$base, i32imm:$mask)> {
  let mayLoad = 1; let mayStore = 1;
  let Defs = [R13, SPR0, SPR1]; let Size = 136;
}
def FRAMEADDR : GCEPseudo<(outs UREG:$dst), (ins UREG:$base, i32imm:$off)> {
  let Defs = [SPR0]; let Size = 16;
}
def BCC : GCEPseudo<(outs), (ins branch_target:$target, UREG:$lhs, UREG:$rhs, i32imm:$cc)> {
  let isBranch = 1; let isTerminator = 1; let Size = 16;
}
def SELECT_CC : GCEPseudo<(outs UREG:$dst),
  (ins UREG:$lhs, UREG:$rhs, i32imm:$cc, UREG:$yes, UREG:$no)> {
  let usesCustomInserter = 1;
}
let isCall = 1, hasSideEffects = 1, mayLoad = 1, mayStore = 1,
    Defs = [R15], Uses = [R14] in {
  def CALL : GCEPseudo<(outs), (ins i32imm:$callee, variable_ops)> { let Size = 16; }
  def CALLR : GCEPseudo<(outs), (ins UREG:$callee, variable_ops)> {
    let Defs = [R15, R13, SPR0, SPR1]; let Size = 24;
  }
}
def RET : GCEPseudo<(outs), (ins variable_ops)> {
  let isReturn = 1; let isTerminator = 1; let isBarrier = 1; let Uses = [R15];
  let Defs = [R13, SPR0, SPR1]; let Size = 16;
}
let isCall = 0, Defs = [R14], Uses = [R14] in {
  def ADJCALLSTACKDOWN : GCEPseudo<(outs), (ins i32imm:$size, i32imm:$align)>;
  def ADJCALLSTACKUP : GCEPseudo<(outs), (ins i32imm:$size, i32imm:$callee)>;
}
