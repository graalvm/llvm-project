; REQUIRES: x86
; RUN: llvm-as %s -o %t-generic.bc
; RUN: ld.lld -shared %t-generic.bc -o %t-generic.so -m elf_x86_64
; RUN: llvm-readobj --dynamic-table %t-generic.so | FileCheck %s --check-prefix=NOSANDBOX

; RUN: sed 's/x86_64-unknown-linux-musl/x86_64-unknown-linux-musl_swcfi/' %s | llvm-as -o %t-swcfi.bc
; RUN: ld.lld -shared %t-swcfi.bc -o %t-swcfi.so -m elf_x86_64
; RUN: llvm-readobj --dynamic-table %t-swcfi.so | FileCheck %s --check-prefix=SANDBOX

; RUN: sed 's/x86_64-unknown-linux-musl/x86_64-unknown-linux-musl_hwcfi/' %s | llvm-as -o %t-hwcfi.bc
; RUN: ld.lld -shared %t-hwcfi.bc -o %t-hwcfi.so -m elf_x86_64
; RUN: llvm-readobj --dynamic-table %t-hwcfi.so | FileCheck %s --check-prefix=SANDBOX

; RUN: ld.lld -shared %t-generic.bc -o %t-explicit-swcfi.so -m elf_x86_64 -sandbox=swcfi
; RUN: llvm-readobj --dynamic-table %t-explicit-swcfi.so | FileCheck %s --check-prefix=SANDBOX

; RUN: ld.lld -shared %t-hwcfi.bc -o %t-override-on.so -m elf_x86_64 -sandbox=swcfi
; RUN: llvm-readobj --dynamic-table %t-override-on.so | FileCheck %s --check-prefix=SANDBOX

; RUN: ld.lld -shared %t-swcfi.bc -o %t-override-off.so -m elf_x86_64 -sandbox=off
; RUN: llvm-readobj --dynamic-table %t-override-off.so | FileCheck %s --check-prefix=NOSANDBOX

; RUN: not ld.lld -shared %t-swcfi.bc %t-hwcfi.bc -o /dev/null -m elf_x86_64 2>&1 | FileCheck %s --check-prefix=CONFLICT

; RUN: ld.lld -shared %t-swcfi.bc %t-hwcfi.bc -o %t-mixed-explicit.so -m elf_x86_64 -sandbox=swcfi --allow-multiple-definition
; RUN: llvm-readobj --dynamic-table %t-mixed-explicit.so | FileCheck %s --check-prefix=SANDBOX

; SANDBOX: 0x000000006000000E
; NOSANDBOX-NOT: 0x000000006000000E
; CONFLICT: error: incompatible sandbox modes derived from bitcode target triples:

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-musl"

define i32 @main() {
  ret i32 0
}
