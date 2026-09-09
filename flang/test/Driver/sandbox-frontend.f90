! REQUIRES: x86-registered-target

! RUN: %flang_fc1 -triple x86_64-unknown-linux-musl -sandbox=swcfi -emit-llvm -o - %s | FileCheck %s --check-prefix=SWCFI --implicit-check-not=SandboxModeHWCFI
! RUN: %flang_fc1 -triple x86_64-unknown-linux-musl -sandbox=swcfi -emit-llvm-bc -o - %s | llvm-dis -o - | FileCheck %s --check-prefix=SWCFI --implicit-check-not=SandboxModeHWCFI
! RUN: %flang_fc1 -triple x86_64-unknown-linux-musl -sandbox=hwcfi -emit-llvm -o - %s | FileCheck %s --check-prefix=HWCFI --implicit-check-not=SandboxModeSWCFI
! RUN: %flang_fc1 -triple x86_64-unknown-linux-musl -sandbox=hwcfi -emit-llvm-bc -o - %s | llvm-dis -o - | FileCheck %s --check-prefix=HWCFI --implicit-check-not=SandboxModeSWCFI

! SWCFI: declare void @llvm.sandboxpoll(i32)
! SWCFI: declare ptr @llvm.sandboxcfi.p0.p0(ptr
! SWCFI: !"SandboxModeSWCFI", i32 1

! HWCFI: declare void @llvm.sandboxpoll(i32)
! HWCFI: declare ptr @llvm.sandboxcfi.p0.p0(ptr
! HWCFI: !"SandboxModeHWCFI", i32 1

end program
