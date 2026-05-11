! RUN: %clang --driver-mode=flang -### -c --target=x86_64-unknown-linux-musl_swcfi %s 2>&1 | FileCheck %s --check-prefix=SWCFI
! RUN: %clang --driver-mode=flang -### -c --target=x86_64-unknown-linux-musl -sandbox=swcfi %s 2>&1 | FileCheck %s --check-prefix=EXPLICIT-SWCFI
! RUN: %clang --driver-mode=flang -### -c --target=x86_64-unknown-linux-musl_hwcfi %s 2>&1 | FileCheck %s --check-prefix=HWCFI
! RUN: %clang --driver-mode=flang -### -c --target=x86_64-unknown-linux-musl -sandbox=hwcfi %s 2>&1 | FileCheck %s --check-prefix=EXPLICIT-HWCFI
! RUN: %clang --driver-mode=flang -### -c --target=x86_64-unknown-linux-musl_hwcfi -sandbox=swcfi %s 2>&1 | FileCheck %s --check-prefix=OVERRIDE
! RUN: %clang --driver-mode=flang -### -c --target=x86_64-unknown-linux-musl %s 2>&1 | FileCheck %s --check-prefix=MUSL

! SWCFI: "-fc1"
! SWCFI-SAME: "-triple" "x86_64-unknown-linux-musl_swcfi"
! SWCFI-SAME: "-sandbox=swcfi"
! SWCFI-SAME: "-fcf-protection"
! SWCFI-SAME: "-fno-jump-tables"
! SWCFI-SAME: "-mllvm" "-x86-force-return-thunk"
! SWCFI-SAME: "-mllvm" "-sandbox-cfi-mode=swcfi"
! SWCFI-SAME: "-D__SANDBOX_CFI__"
! SWCFI-SAME: "-D__SANDBOX_SWCFI__"
! SWCFI-NOT: "-sandbox=swcfi" "-sandbox=swcfi"

! EXPLICIT-SWCFI: "-fc1"
! EXPLICIT-SWCFI-SAME: "-triple" "x86_64-unknown-linux-musl"
! EXPLICIT-SWCFI-SAME: "-sandbox=swcfi"
! EXPLICIT-SWCFI-SAME: "-fcf-protection"
! EXPLICIT-SWCFI-SAME: "-fno-jump-tables"
! EXPLICIT-SWCFI-SAME: "-mllvm" "-x86-force-return-thunk"
! EXPLICIT-SWCFI-SAME: "-mllvm" "-sandbox-cfi-mode=swcfi"
! EXPLICIT-SWCFI-SAME: "-D__SANDBOX_CFI__"
! EXPLICIT-SWCFI-SAME: "-D__SANDBOX_SWCFI__"
! EXPLICIT-SWCFI-NOT: "-sandbox=swcfi" "-sandbox=swcfi"

! HWCFI: "-fc1"
! HWCFI-SAME: "-triple" "x86_64-unknown-linux-musl_hwcfi"
! HWCFI-SAME: "-sandbox=hwcfi"
! HWCFI-SAME: "-mllvm" "-sandbox-cfi-mode=hwcfi"
! HWCFI-SAME: "-D__SANDBOX_CFI__"
! HWCFI-SAME: "-D__SANDBOX_HWCFI__"
! HWCFI-NOT: "-sandbox=hwcfi" "-sandbox=hwcfi"

! EXPLICIT-HWCFI: "-fc1"
! EXPLICIT-HWCFI-SAME: "-triple" "x86_64-unknown-linux-musl"
! EXPLICIT-HWCFI-SAME: "-sandbox=hwcfi"
! EXPLICIT-HWCFI-SAME: "-mllvm" "-sandbox-cfi-mode=hwcfi"
! EXPLICIT-HWCFI-SAME: "-D__SANDBOX_CFI__"
! EXPLICIT-HWCFI-SAME: "-D__SANDBOX_HWCFI__"
! EXPLICIT-HWCFI-NOT: "-sandbox=hwcfi" "-sandbox=hwcfi"

! OVERRIDE: "-fc1"
! OVERRIDE-SAME: "-triple" "x86_64-unknown-linux-musl_hwcfi"
! OVERRIDE-SAME: "-sandbox=swcfi"
! OVERRIDE-SAME: "-fcf-protection"
! OVERRIDE-SAME: "-fno-jump-tables"
! OVERRIDE-SAME: "-mllvm" "-sandbox-cfi-mode=swcfi"
! OVERRIDE-SAME: "-D__SANDBOX_CFI__"
! OVERRIDE-SAME: "-D__SANDBOX_SWCFI__"
! OVERRIDE-NOT: "-sandbox=hwcfi"
! OVERRIDE-NOT: "-sandbox-cfi-mode=hwcfi"
! OVERRIDE-NOT: "-D__SANDBOX_HWCFI__"

! MUSL: "-fc1"
! MUSL-SAME: "-triple" "x86_64-unknown-linux-musl"
! MUSL-NOT: "-sandbox="

end
