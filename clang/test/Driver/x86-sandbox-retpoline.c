// RUN: not %clang --target=x86_64-unknown-linux -sandbox=swcfi -mretpoline -### %s 2>&1 | FileCheck --check-prefix=RETPOLINE-SWCFI %s
// RUN: not %clang --target=x86_64-unknown-linux -sandbox=hwcfi -mretpoline -### %s 2>&1 | FileCheck --check-prefix=RETPOLINE-HWCFI %s
// RUN: not %clang --target=x86_64-unknown-linux-musl_swcfi -mretpoline -### %s 2>&1 | FileCheck --check-prefix=RETPOLINE-SWCFI %s
// RUN: not %clang --target=x86_64-unknown-linux-musl_hwcfi -mretpoline -### %s 2>&1 | FileCheck --check-prefix=RETPOLINE-HWCFI %s
// RUN: not %clang --target=x86_64-unknown-linux -sandbox=swcfi -mretpoline-external-thunk -### %s 2>&1 | FileCheck --check-prefix=EXTERNAL %s
// RUN: not %clang --target=x86_64-unknown-linux-musl_swcfi -mretpoline-external-thunk -### %s 2>&1 | FileCheck --check-prefix=EXTERNAL %s
// RUN: not %clang --target=x86_64-unknown-linux -sandbox=swcfi -mspeculative-load-hardening -### %s 2>&1 | FileCheck --check-prefix=SLH %s
// RUN: not %clang --target=x86_64-unknown-linux-musl_swcfi -mspeculative-load-hardening -### %s 2>&1 | FileCheck --check-prefix=SLH %s
// RUN: %clang --target=x86_64-unknown-linux -sandbox=swcfi -mfunction-return=thunk-extern -### %s 2>&1 | FileCheck --check-prefix=RETURN %s
// RUN: %clang --target=x86_64-unknown-linux-musl_swcfi -mfunction-return=thunk-extern -### %s 2>&1 | FileCheck --check-prefix=RETURN %s
// RUN: %clang --target=x86_64-unknown-linux -sandbox=swcfi -mno-retpoline -mno-retpoline-external-thunk -mno-speculative-load-hardening -mfunction-return=keep -### %s 2>&1 | FileCheck --check-prefix=NEGATIVE %s
// RUN: %clang --target=x86_64-unknown-linux-musl_swcfi -mno-retpoline -mno-retpoline-external-thunk -mno-speculative-load-hardening -mfunction-return=keep -### %s 2>&1 | FileCheck --check-prefix=NEGATIVE %s
// RUN: %clang --target=x86_64-unknown-linux -sandbox=off -mretpoline -mretpoline-external-thunk -mspeculative-load-hardening -mfunction-return=thunk-extern -### %s 2>&1 | FileCheck --check-prefix=OFF %s

// RETPOLINE-SWCFI: error: invalid argument 'mretpoline' not allowed with '-sandbox=swcfi'
// RETPOLINE-HWCFI: error: invalid argument 'mretpoline' not allowed with '-sandbox=hwcfi'
// EXTERNAL: error: invalid argument 'mretpoline-external-thunk' not allowed with '-sandbox=swcfi'
// SLH: error: invalid argument 'mspeculative-load-hardening' not allowed with '-sandbox=swcfi'
// RETURN: "-mfunction-return=thunk-extern"
// RETURN-NOT: error: invalid argument
// NEGATIVE-NOT: error: invalid argument
// OFF-NOT: error: invalid argument
