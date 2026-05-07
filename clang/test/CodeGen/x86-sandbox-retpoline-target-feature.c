// REQUIRES: x86-registered-target
// RUN: not %clang_cc1 -triple x86_64-unknown-linux -sandbox=swcfi -target-feature +retpoline-indirect-calls -S -o /dev/null %s 2>&1 | FileCheck %s
// RUN: not %clang_cc1 -triple x86_64-unknown-linux -sandbox=hwcfi -target-feature +retpoline-indirect-branches -S -o /dev/null %s 2>&1 | FileCheck %s
// RUN: not %clang --target=x86_64-unknown-linux-musl_swcfi -Xclang -target-feature -Xclang +retpoline -S -o /dev/null %s 2>&1 | FileCheck %s
// RUN: not %clang --target=x86_64-unknown-linux-musl_hwcfi -Xclang -target-feature -Xclang +retpoline-external-thunk -S -o /dev/null %s 2>&1 | FileCheck %s
// RUN: %clang --target=x86_64-unknown-linux-musl_swcfi -sandbox=off -Xclang -target-feature -Xclang +retpoline-indirect-calls -S -o /dev/null %s

// CHECK: retpoline generation is incompatible with GraalOS sandboxing

void f(void (*callee)(void)) {
  callee();
}
