// REQUIRES: gce-registered-target
// RUN: %clang_cc1 -triple gce-unknown-none -mrelocation-model static -ffreestanding -emit-llvm -o /dev/null -verify %s

// Aggregate varargs are legal C but not implemented by the GCE ABI.
// Keep the diagnostic generic rather than recognizing a library typedef.
struct wrapped_float { float value; };
float get_wrapped(int tag, ...) {
  __builtin_va_list args;
  __builtin_va_start(args, tag);
  float result = __builtin_va_arg(args, struct wrapped_float).value; // expected-error 2 {{aggregate va_arg values are unsupported; use pointers or standard scalar promotions (float varargs use double)}}
  __builtin_va_end(args);
  return result;
}
