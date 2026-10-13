// REQUIRES: gce-registered-target
// RUN: %clang_cc1 -triple gce-unknown-none -mrelocation-model static -ffreestanding -emit-llvm -o - %s | FileCheck %s
#if !defined(__SOFTFP__) || __GCE_ABI__ != 1
#error GCE must select the soft-float runtime without changing integer ABI
#endif
_Static_assert(sizeof(float)==4 && _Alignof(float)==4, "f32");
_Static_assert(sizeof(double)==8 && _Alignof(double)==8, "f64");
_Static_assert(sizeof(long double)==8 && __LDBL_MANT_DIG__==53, "long double");
// CHECK-LABEL: define{{.*}} float @add
// CHECK: fadd float
float add(float a,float b) { return a+b; }
// CHECK-LABEL: define{{.*}} double @get
// CHECK: call void @llvm.va_start
// CHECK: getelementptr inbounds i8, ptr {{.*}}, i32 7
// CHECK: call ptr @llvm.ptrmask.p0.i32(ptr {{.*}}, i32 -8)
// CHECK: getelementptr inbounds i8, ptr {{.*}}, i32 8
// CHECK: load double, ptr {{.*}}, align 8
double get(int n,...) {
  __builtin_va_list ap;
  __builtin_va_start(ap,n);
  double d=__builtin_va_arg(ap,double);
  __builtin_va_end(ap);
  return d;
}
// CHECK-LABEL: define{{.*}} double @promote
// CHECK: fpext float {{.*}} to double
// CHECK: call double (i32, ...) @get(i32{{.*}} 1, double
double promote(float f) { return get(1,f); }
// CHECK-LABEL: define{{.*}} double @indirect
// CHECK: call double %{{.*}}(double
double indirect(double (*f)(double),double x) { return f(x); }
