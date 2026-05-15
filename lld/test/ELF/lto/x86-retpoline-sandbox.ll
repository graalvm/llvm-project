; REQUIRES: x86
; RUN: llvm-as %s -o %t.o
; RUN: not ld.lld -shared %t.o -o /dev/null -m elf_x86_64 -sandbox=swcfi 2>&1 | FileCheck %s
; RUN: not ld.lld -shared %t.o -o /dev/null -m elf_x86_64 -sandbox=hwcfi 2>&1 | FileCheck %s
; RUN: sed 's/x86_64-unknown-linux-musl/x86_64-unknown-linux-musl_swcfi/' %s | llvm-as -o %t-swcfi.o
; RUN: not ld.lld -shared %t-swcfi.o -o /dev/null -m elf_x86_64 2>&1 | FileCheck %s

; CHECK: retpoline generation is incompatible with GraalOS sandboxing

target triple = "x86_64-unknown-linux-musl"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

define void @retpoline_indirect_calls(ptr %callee) #0 {
entry:
  call void %callee()
  ret void
}

attributes #0 = { "target-features"="+retpoline-indirect-calls" }
