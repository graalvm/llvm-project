// RUN: %clang -### -c --target=x86_64-unknown-linux-musl_swcfi %s 2>&1 | FileCheck %s --check-prefix=SWCFI
// RUN: %clang -### -c --target=x86_64-unknown-linux-musl -sandbox=swcfi %s 2>&1 | FileCheck %s --check-prefix=EXPLICIT-SWCFI
// RUN: %clang -### -c --target=x86_64-unknown-linux-musl_hwcfi %s 2>&1 | FileCheck %s --check-prefix=HWCFI
// RUN: %clang -### -c --target=x86_64-unknown-linux-musl -sandbox=hwcfi %s 2>&1 | FileCheck %s --check-prefix=EXPLICIT-HWCFI
// RUN: %clang -### -c --target=x86_64-unknown-linux-musl_hwcfi -sandbox=swcfi %s 2>&1 | FileCheck %s --check-prefix=OVERRIDE
// RUN: %clang -### -c --target=x86_64-unknown-linux-musl %s 2>&1 | FileCheck %s --check-prefix=MUSL
// RUN: %clang -### -c --target=x86_64-unknown-linux-musl-foo %s 2>&1 | FileCheck %s --check-prefix=INVALID
// RUN: %clang -### --target=x86_64-unknown-linux-musl_swcfi %s 2>&1 | FileCheck %s --check-prefix=LINK-SWCFI
// RUN: %clang -### --target=x86_64-unknown-linux-musl_hwcfi %s 2>&1 | FileCheck %s --check-prefix=LINK-HWCFI
// RUN: %clang -### --target=x86_64-unknown-linux-musl_hwcfi -sandbox=swcfi %s 2>&1 | FileCheck %s --check-prefix=LINK-OVERRIDE

// SWCFI: "-cc1"
// SWCFI-SAME: "-triple" "x86_64-unknown-linux-musl_swcfi"
// SWCFI-SAME: "-sandbox=swcfi"
// SWCFI-SAME: "-fcf-protection"
// SWCFI-SAME: "-fno-jump-tables"
// SWCFI-SAME: "-mllvm" "-sandbox-cfi-mode=swcfi"
// SWCFI-SAME: "-D__SANDBOX_CFI__"
// SWCFI-SAME: "-D__SANDBOX_SWCFI__"
// SWCFI-NOT: "-sandbox=swcfi" "-sandbox=swcfi"

// EXPLICIT-SWCFI: "-cc1"
// EXPLICIT-SWCFI-SAME: "-triple" "x86_64-unknown-linux-musl"
// EXPLICIT-SWCFI-SAME: "-sandbox=swcfi"
// EXPLICIT-SWCFI-SAME: "-fcf-protection"
// EXPLICIT-SWCFI-SAME: "-fno-jump-tables"
// EXPLICIT-SWCFI-SAME: "-mllvm" "-sandbox-cfi-mode=swcfi"
// EXPLICIT-SWCFI-SAME: "-D__SANDBOX_CFI__"
// EXPLICIT-SWCFI-SAME: "-D__SANDBOX_SWCFI__"
// EXPLICIT-SWCFI-NOT: "-sandbox=swcfi" "-sandbox=swcfi"

// HWCFI: "-cc1"
// HWCFI-SAME: "-triple" "x86_64-unknown-linux-musl_hwcfi"
// HWCFI-SAME: "-sandbox=hwcfi"
// HWCFI-SAME: "-fcf-protection"
// HWCFI-SAME: "-mllvm" "-sandbox-cfi-mode=hwcfi"
// HWCFI-SAME: "-D__SANDBOX_CFI__"
// HWCFI-SAME: "-D__SANDBOX_HWCFI__"
// HWCFI-NOT: "-sandbox=hwcfi" "-sandbox=hwcfi"

// EXPLICIT-HWCFI: "-cc1"
// EXPLICIT-HWCFI-SAME: "-triple" "x86_64-unknown-linux-musl"
// EXPLICIT-HWCFI-SAME: "-sandbox=hwcfi"
// EXPLICIT-HWCFI-SAME: "-fcf-protection"
// EXPLICIT-HWCFI-SAME: "-mllvm" "-sandbox-cfi-mode=hwcfi"
// EXPLICIT-HWCFI-SAME: "-D__SANDBOX_CFI__"
// EXPLICIT-HWCFI-SAME: "-D__SANDBOX_HWCFI__"
// EXPLICIT-HWCFI-NOT: "-sandbox=hwcfi" "-sandbox=hwcfi"

// OVERRIDE: "-cc1"
// OVERRIDE-SAME: "-triple" "x86_64-unknown-linux-musl_hwcfi"
// OVERRIDE-SAME: "-sandbox=swcfi"
// OVERRIDE-SAME: "-fcf-protection"
// OVERRIDE-SAME: "-fno-jump-tables"
// OVERRIDE-SAME: "-mllvm" "-sandbox-cfi-mode=swcfi"
// OVERRIDE-SAME: "-D__SANDBOX_CFI__"
// OVERRIDE-SAME: "-D__SANDBOX_SWCFI__"
// OVERRIDE-NOT: "-sandbox=hwcfi"
// OVERRIDE-NOT: "-sandbox-cfi-mode=hwcfi"
// OVERRIDE-NOT: "-D__SANDBOX_HWCFI__"

// MUSL: "-cc1"
// MUSL-SAME: "-triple" "x86_64-unknown-linux-musl"
// MUSL-NOT: "-sandbox="

// INVALID: "-cc1"
// INVALID-SAME: "-triple" "x86_64-unknown-linux-musl-foo"
// INVALID-NOT: "-sandbox="

// LINK-SWCFI: "ld.lld"
// LINK-SWCFI-SAME: "-sandbox=swcfi"
// LINK-SWCFI-NOT: "-sandbox=hwcfi"

// LINK-HWCFI: "ld.lld"
// LINK-HWCFI-SAME: "-sandbox=hwcfi"
// LINK-HWCFI-NOT: "-sandbox=swcfi"

// LINK-OVERRIDE: "ld.lld"
// LINK-OVERRIDE-SAME: "-sandbox=swcfi"
// LINK-OVERRIDE-NOT: "-sandbox=hwcfi"

extern int sandbox_driver_test_anchor;
