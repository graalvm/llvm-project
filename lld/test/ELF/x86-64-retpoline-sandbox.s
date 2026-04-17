// REQUIRES: x86
// RUN: llvm-mc -filetype=obj -triple=x86_64-unknown-linux %s -o %t.o
// RUN: not ld.lld -shared %t.o -o /dev/null -z retpolineplt -sandbox=swcfi 2>&1 | FileCheck %s
// RUN: not ld.lld -shared %t.o -o /dev/null -z retpolineplt -z now -sandbox=swcfi 2>&1 | FileCheck %s
// RUN: not ld.lld -shared %t.o -o /dev/null -z retpolineplt -sandbox=hwcfi 2>&1 | FileCheck %s
// RUN: not ld.lld -shared %t.o -o /dev/null -z retpolineplt -z now -sandbox=hwcfi 2>&1 | FileCheck %s

// CHECK: error: -z retpolineplt may not be used with -sandbox

.global _start
_start:
  retq
