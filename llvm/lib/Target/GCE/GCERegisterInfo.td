// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
class GCEReg<bits<16> Enc, string Name> : Register<Name> {
  let Namespace = "GCE";
  let HWEncoding = Enc;
}
foreach I = 0-15 in {
  def R#I : GCEReg<!add(32, I), "r"#I>, DwarfRegNum<[I]>;
}
foreach I = 0-3 in {
  def SPR#I : GCEReg<I, "spr"#I>;
}
foreach T = 0-15 in {
  foreach I = 0-3 in {
    def CPR#I#"T"#T : GCEReg<!add(0x8000, !add(!mul(4,T), I)),
                           "cpr"#I#"t"#T>;
  }
  def CPRClass#T : RegisterClass<"GCE", [i32], 32,
    (sequence "CPR%iT"#T, 0, 3)> {
    let isAllocatable = 0;
  }
}
def TPR : GCEReg<56, "tpr">;
def HND : GCEReg<59, "hnd">;
// P aliases are OLD instruction IDs, distinguished from unified IDs.
let Namespace = "GCE" in {
  def sub_lo : SubRegIndex<32>;
  def sub_hi : SubRegIndex<32, 32>;
}
foreach I = 0-7 in {
  def P#I : GCEReg<!add(0x100, !add(16, I)), "p"#I> {
    let SubRegs = [!cast<Register>("R"#!mul(2,I)),
                   !cast<Register>("R"#!add(!mul(2,I),1))];
    let SubRegIndices = [sub_lo, sub_hi];
    let CoveredBySubRegs = 1;
  }
}
def PREG : RegisterClass<"GCE", [i64], 64, (sequence "P%i", 0, 7)> {
  let isAllocatable = 0;
}
def GPR : RegisterClass<"GCE", [i32], 32, (sequence "R%i", 0, 15)>;
def OLDREG : RegisterClass<"GCE", [i32, i64], 32, (add GPR, PREG)> {
  let isAllocatable = 0;
}
def SPR : RegisterClass<"GCE", [i32], 32, (sequence "SPR%i", 0, 3)>;
def UREG : RegisterClass<"GCE", [i32], 32,
  (add GPR, SPR, CPRClass0, CPRClass1, CPRClass2, CPRClass3,
       CPRClass4, CPRClass5, CPRClass6, CPRClass7,
       CPRClass8, CPRClass9, CPRClass10, CPRClass11,
       CPRClass12, CPRClass13, CPRClass14, CPRClass15, TPR, HND)> {
  let isAllocatable = 1;
}

def CSR_GCE : CalleeSavedRegs<(add R8, R9, R10, R11, R12, R14,
  CPRClass4, CPRClass5, CPRClass6, CPRClass7)>;
