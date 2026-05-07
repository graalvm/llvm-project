! RUN: %flang -### -c --target=x86_64-unknown-linux-musl -sandbox=swcfi %s 2>&1 | FileCheck %s --implicit-check-not="-x86-force-return-thunk"

! CHECK: "-fc1"
! CHECK-SAME: "-sandbox=swcfi"
! CHECK-SAME: "-fcf-protection"
! CHECK-SAME: "-fno-jump-tables"
! CHECK-SAME: "-mllvm" "-sandbox-cfi-mode=swcfi"
! CHECK-SAME: "-D__SANDBOX_CFI__"
! CHECK-SAME: "-D__SANDBOX_SWCFI__"

end
