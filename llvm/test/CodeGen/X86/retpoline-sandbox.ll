; RUN: not llc < %s 2>&1 | FileCheck %s

; CHECK: retpoline generation is incompatible with GraalOS sandboxing

target triple = "x86_64-unknown-linux-musl_swcfi"
!llvm.module.flags = !{!0}
!0 = !{i32 1, !"SandboxModeSWCFI", i32 1}

define void @retpoline_indirect_calls(ptr %callee) #0 {
entry:
  call void %callee()
  ret void
}

attributes #0 = { "target-features"="+retpoline-indirect-calls" }
