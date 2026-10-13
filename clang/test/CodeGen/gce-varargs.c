// REQUIRES: gce-registered-target
// RUN: %clang_cc1 -triple gce-unknown-none -mrelocation-model static -ffreestanding -emit-llvm -o - %s | FileCheck %s

#if !defined(__gce__) || !defined(__GCE__) || __GCE_ABI__ != 1
#error Incorrect GCE target macros
#endif
_Static_assert(sizeof(void *) == 4 && sizeof(long) == 4, "ILP32");
_Static_assert(sizeof(long long) == 8 && _Alignof(long long) == 8, "i64");
_Static_assert(__builtin_types_compatible_p(__builtin_va_list, char *),
               "GCE va_list must be a pointer cursor");

// CHECK-LABEL: define{{.*}} i64 @get_args(i32{{.*}}, ...)
// CHECK: call void @llvm.va_start
// CHECK: getelementptr inbounds i8, ptr {{.*}}, i32 4
// CHECK: load i32, ptr {{.*}}, align 4
// CHECK: call void @llvm.va_copy
// CHECK: getelementptr inbounds i8, ptr {{.*}}, i32 7
// CHECK: call ptr @llvm.ptrmask.p0.i32(ptr {{.*}}, i32 -8)
// CHECK: getelementptr inbounds i8, ptr {{.*}}, i32 8
// CHECK: load i64, ptr {{.*}}, align 8
// CHECK: call void @llvm.va_end
// CHECK: call void @llvm.va_end
unsigned long long get_args(unsigned tag, ...) {
  __builtin_va_list ap, copy;
  __builtin_va_start(ap, tag);
  unsigned word = __builtin_va_arg(ap, unsigned);
  __builtin_va_copy(copy, ap);
  unsigned long long wide = __builtin_va_arg(copy, unsigned long long);
  __builtin_va_end(copy);
  __builtin_va_end(ap);
  return wide + word + tag;
}

// CHECK-LABEL: define{{.*}} ptr @get_pointer(i32{{.*}}, ...)
// CHECK: getelementptr inbounds i8, ptr {{.*}}, i32 4
// CHECK: load ptr, ptr {{.*}}, align 4
void *get_pointer(unsigned tag, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, tag);
  void *value = __builtin_va_arg(ap, void *);
  __builtin_va_end(ap);
  return value;
}

// Default argument promotions must happen before target stack assignment.
// CHECK-LABEL: define{{.*}} i64 @promote_args
// CHECK: sext i8 {{.*}} to i32
// CHECK: zext i16 {{.*}} to i32
// CHECK: call i64 (i32, ...) @get_args(i32{{.*}} 3, i32{{.*}}, i32{{.*}}, i64{{.*}})
unsigned long long promote_args(signed char c, unsigned short s,
                                unsigned long long wide) {
  return get_args(3, c, s, wide);
}
