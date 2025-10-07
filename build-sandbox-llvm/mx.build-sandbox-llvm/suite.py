suite = {
    "mxversion": "7.58.3",
    "name": "build-sandbox-llvm",
    "versionConflictResolution" : "latest",
    "ignore_suite_commit_info": True,

    "imports" : {
        "suites" : [
            {
                "name" : "sdk",
                "subdir" : True,
                "version" : "afaec4b3b77c9e1445c8914d04474ab26e30cb03",
                "urls" : [
                    {"url": "https://github.com/oracle/graal", "kind": "git"},
                ]
            },
        ]
    },

    "projects": {

        "sandbox-llvm" : {
            "class" : "CMakeNinjaProject",
            "vpath" : True,
            "subDir" : "src",
            "sourceDir" : "<path:build-sandbox-llvm>/..",
            "cmakeSubdir" : "llvm",
            "ninja_install_targets" : ["install"],
            "symlinkSource" : True,
            "results" : ["usr"],
            # The LLVM build is very parallelizable, and everything else needs to wait for this build.
            # Turn this up to very high parallelization, to utilize our huge CI machines.
            # mx caps this to the number of CPUs anyway, so it doesn't hurt for local development.
            "max_jobs" : "128",
            "cmakeConfig" : {
                "CMAKE_BUILD_TYPE": "Release",
                "CMAKE_INSTALL_PREFIX" : "usr",
                "CMAKE_C_COMPILER" : "<path:sdk:LLVM_TOOLCHAIN>/bin/clang",

                # avoid putting the URL of the CI's git mirror into the version string
                "LLVM_FORCE_VC_REPOSITORY": "https://github.com/graalvm/llvm-project.git",
                # if we override the repository, the autodetection of the revision gets turned off, so do it manually here
                "LLVM_FORCE_VC_REVISION": "<suite-version:build-sandbox-llvm>",

                # fortran: since musl does not have proper support for float128 we disable it for the moment
                "LLVM_ENABLE_PROJECTS": "clang;lld;flang",

                "LLVM_TARGETS_TO_BUILD": "Native",
                "LLVM_LINK_LLVM_DYLIB": "YES",

                "CLANG_DEFAULT_LINKER": "lld",
                "CLANG_DEFAULT_RTLIB": "compiler-rt",
                "CLANG_DEFAULT_CXX_STDLIB": "libc++",

                # reduce dependencies, make more system independent
                "LLVM_ENABLE_LIBXML2": "NO",
            },
            "clangFormat" : False,
            "buildDependencies" : [ "sdk:LLVM_TOOLCHAIN" ],
        },

        "sandbox-compiler-rt-swcfi" : {
            "class" : "CMakeNinjaProject",
            "vpath" : True,
            "subDir" : "src",
            "sourceDir" : "<path:build-sandbox-llvm>/..",
            "cmakeSubdir" : "compiler-rt",
            "ninja_install_targets" : ["install"],
            "symlinkSource" : True,
            "results" : ["usr"],
            "max_jobs" : "128",
            "cmakeConfig" : {
                "CMAKE_BUILD_TYPE": "Release",
                "CMAKE_INSTALL_PREFIX" : "usr",
                "CMAKE_C_COMPILER" : "<path:sdk:LLVM_TOOLCHAIN>/bin/clang",

                # avoid putting the URL of the CI's git mirror into the version string
                "LLVM_FORCE_VC_REPOSITORY": "https://github.com/graalvm/llvm-project.git",
                # if we override the repository, the autodetection of the revision gets turned off, so do it manually here
                "LLVM_FORCE_VC_REVISION": "<suite-version:build-sandbox-llvm>",

                # Linking against libstdc++ is necessary, otherwise cmake configure tests are failing.
                # The compiler-rt doesn't actually use it, and it's going to be replaced with a musl-based one later.
                "RUNTIMES_CMAKE_ARGS": "-DCMAKE_C_FLAGS=-sandbox=swcfi -DWITHOUT_FLOAT128;-DCMAKE_CXX_FLAGS=-sandbox=swcfi -stdlib=libstdc++ -DWITHOUT_FLOAT128",
                "BUILTINS_CMAKE_ARGS": "-DCMAKE_C_FLAGS=-sandbox=swcfi -DWITHOUT_FLOAT128;-DCMAKE_CXX_FLAGS=-sandbox=swcfi -DWITHOUT_FLOAT128",

                "COMPILER_RT_USE_BUILTINS_LIBRARY": "YES",
                "COMPILER_RT_USE_LLVM_UNWINDER": "YES",

                "COMPILER_RT_BUILD_BUILTINS": "YES",
                "COMPILER_RT_BUILD_CRT": "YES",
                "COMPILER_RT_BUILD_STANDALONE_LIBATOMIC": "NO",

                "COMPILER_RT_BUILD_XRAY": "NO",
                "COMPILER_RT_BUILD_LIBFUZZER": "NO",
                "COMPILER_RT_BUILD_PROFILE": "NO",
                "COMPILER_RT_BUILD_MEMPROF": "NO",
                "COMPILER_RT_BUILD_ORC": "NO",
                "COMPILER_RT_BUILD_SANITIZERS": "NO",
                "COMPILER_RT_BUILD_CTX_PROFILE": "NO",
            },
            "clangFormat" : False,
            "buildDependencies" : [ "sdk:LLVM_TOOLCHAIN", "SANDBOX_LLVM_BASE" ],
        },

        "sandbox-libunwind-swcfi" : {
            "class" : "CMakeNinjaProject",
            "vpath" : True,
            "subDir" : "src",
            "sourceDir" : "<path:build-sandbox-llvm>/..",
            "cmakeSubdir" : "libunwind",
            "ninja_install_targets" : ["install"],
            "symlinkSource" : True,
            "results" : ["usr"],
            "max_jobs" : "128",
            "cmakeConfig" : {
                "CMAKE_BUILD_TYPE": "Release",
                "CMAKE_INSTALL_PREFIX" : "usr",
                "CMAKE_C_COMPILER" : "<path:sdk:LLVM_TOOLCHAIN>/bin/clang",

                # avoid putting the URL of the CI's git mirror into the version string
                "LLVM_FORCE_VC_REPOSITORY": "https://github.com/graalvm/llvm-project.git",
                # if we override the repository, the autodetection of the revision gets turned off, so do it manually here
                "LLVM_FORCE_VC_REVISION": "<suite-version:build-sandbox-llvm>",

                # Linking against libstdc++ is necessary, otherwise cmake configure tests are failing.
                "RUNTIMES_CMAKE_ARGS": "-DCMAKE_C_FLAGS=-sandbox=swcfi -DWITHOUT_FLOAT128;-DCMAKE_CXX_FLAGS=-sandbox=swcfi -stdlib=libstdc++ -DWITHOUT_FLOAT128",
                "BUILTINS_CMAKE_ARGS": "-DCMAKE_C_FLAGS=-sandbox=swcfi -DWITHOUT_FLOAT128;-DCMAKE_CXX_FLAGS=-sandbox=swcfi -DWITHOUT_FLOAT128",
            },
            "clangFormat" : False,
            "buildDependencies" : [ "sdk:LLVM_TOOLCHAIN", "SANDBOX_LLVM_BASE" ],
        },

        "sandbox-compiler-rt-hwcfi" : {
            "class" : "CMakeNinjaProject",
            "vpath" : True,
            "subDir" : "src",
            "sourceDir" : "<path:build-sandbox-llvm>/..",
            "cmakeSubdir" : "compiler-rt",
            "ninja_install_targets" : ["install"],
            "symlinkSource" : True,
            "results" : ["usr"],
            "max_jobs" : "128",
            "cmakeConfig" : {
                "CMAKE_BUILD_TYPE": "Release",
                "CMAKE_INSTALL_PREFIX" : "usr",
                "CMAKE_C_COMPILER" : "<path:sdk:LLVM_TOOLCHAIN>/bin/clang",

                # avoid putting the URL of the CI's git mirror into the version string
                "LLVM_FORCE_VC_REPOSITORY": "https://github.com/graalvm/llvm-project.git",
                # if we override the repository, the autodetection of the revision gets turned off, so do it manually here
                "LLVM_FORCE_VC_REVISION": "<suite-version:build-sandbox-llvm>",

                # Linking against libstdc++ is necessary, otherwise cmake configure tests are failing.
                # The compiler-rt doesn't actually use it, and it's going to be replaced with a musl-based one later.
                "RUNTIMES_CMAKE_ARGS": "-DCMAKE_C_FLAGS=-sandbox=hwcfi -DWITHOUT_FLOAT128;-DCMAKE_CXX_FLAGS=-sandbox=hwcfi -stdlib=libstdc++ -DWITHOUT_FLOAT128",
                "BUILTINS_CMAKE_ARGS": "-DCMAKE_C_FLAGS=-sandbox=hwcfi -DWITHOUT_FLOAT128;-DCMAKE_CXX_FLAGS=-sandbox=hwcfi -DWITHOUT_FLOAT128",

                "COMPILER_RT_USE_BUILTINS_LIBRARY": "YES",
                "COMPILER_RT_USE_LLVM_UNWINDER": "YES",

                "COMPILER_RT_BUILD_BUILTINS": "YES",
                "COMPILER_RT_BUILD_CRT": "YES",
                "COMPILER_RT_BUILD_STANDALONE_LIBATOMIC": "NO",

                "COMPILER_RT_BUILD_XRAY": "NO",
                "COMPILER_RT_BUILD_LIBFUZZER": "NO",
                "COMPILER_RT_BUILD_PROFILE": "NO",
                "COMPILER_RT_BUILD_MEMPROF": "NO",
                "COMPILER_RT_BUILD_ORC": "NO",
                "COMPILER_RT_BUILD_SANITIZERS": "NO",
                "COMPILER_RT_BUILD_CTX_PROFILE": "NO",
            },
            "clangFormat" : False,
            "buildDependencies" : [ "sdk:LLVM_TOOLCHAIN", "SANDBOX_LLVM_BASE" ],
        },

        "sandbox-libunwind-hwcfi" : {
            "class" : "CMakeNinjaProject",
            "vpath" : True,
            "subDir" : "src",
            "sourceDir" : "<path:build-sandbox-llvm>/..",
            "cmakeSubdir" : "libunwind",
            "ninja_install_targets" : ["install"],
            "symlinkSource" : True,
            "results" : ["usr"],
            "max_jobs" : "128",
            "cmakeConfig" : {
                "CMAKE_BUILD_TYPE": "Release",
                "CMAKE_INSTALL_PREFIX" : "usr",
                "CMAKE_C_COMPILER" : "<path:sdk:LLVM_TOOLCHAIN>/bin/clang",

                # avoid putting the URL of the CI's git mirror into the version string
                "LLVM_FORCE_VC_REPOSITORY": "https://github.com/graalvm/llvm-project.git",
                # if we override the repository, the autodetection of the revision gets turned off, so do it manually here
                "LLVM_FORCE_VC_REVISION": "<suite-version:build-sandbox-llvm>",

                # Linking against libstdc++ is necessary, otherwise cmake configure tests are failing.
                "RUNTIMES_CMAKE_ARGS": "-DCMAKE_C_FLAGS=-sandbox=hwcfi -DWITHOUT_FLOAT128;-DCMAKE_CXX_FLAGS=-sandbox=hwcfi -stdlib=libstdc++ -DWITHOUT_FLOAT128",
                "BUILTINS_CMAKE_ARGS": "-DCMAKE_C_FLAGS=-sandbox=hwcfi -DWITHOUT_FLOAT128;-DCMAKE_CXX_FLAGS=-sandbox=hwcfi -DWITHOUT_FLOAT128",
            },
            "clangFormat" : False,
            "buildDependencies" : [ "sdk:LLVM_TOOLCHAIN", "SANDBOX_LLVM_BASE" ],
        }

    },

    "distributions" : {
        "SANDBOX_LLVM_BASE": {
            # like LayoutDirDistribution, but detecting the case where LLVM wasn't actually rebuilt
            # working around a bug in the LLVM build system making ninja always say needsBuild==true
            "class": "LLVMLayoutDistribution",
            "native": True,
            "platformDependent": True,
            "type": "dir",
            "layout": {
                "./": [
                    "dependency:sandbox-llvm/*",
                ],
            },
        },

        "SANDBOX_LLVM_SWCFI": {
            # like LayoutDirDistribution, but detecting the case where LLVM wasn't actually rebuilt
            # working around a bug in the LLVM build system making ninja always say needsBuild==true
            "class": "LLVMLayoutDistribution",
            "native": True,
            "platformDependent": True,
            "type": "dir",
            "layout": {
                "./": [
                    "dependency:sandbox-llvm/*",
                    "dependency:sandbox-libunwind-swcfi/*",
                    "dependency:sandbox-compiler-rt-swcfi/*",
                ],
            },
        },

        "SANDBOX_LLVM_HWCFI": {
            # like LayoutDirDistribution, but detecting the case where LLVM wasn't actually rebuilt
            # working around a bug in the LLVM build system making ninja always say needsBuild==true
            "class": "LLVMLayoutDistribution",
            "native": True,
            "platformDependent": True,
            "type": "dir",
            "layout": {
                "./": [
                    "dependency:sandbox-llvm/*",
                    "dependency:sandbox-libunwind-hwcfi/*",
                    "dependency:sandbox-compiler-rt-hwcfi/*",
                ],
            },
        },

        # for publishing to the cache
        "CACHED_SANDBOX_LLVM_SWCFI": {
            "class": "CachedDistribution",
            "delegate": "SANDBOX_LLVM_SWCFI",
            "artifactName": "sandbox-llvm-{os}-{arch}-g{revision}-swcfi.tar.gz",
            "artifactInfo": {
                "artifactType": "sandbox-llvm",
            },
            "ciJob": "ondemand-build-sandbox-llvm-<os>-<arch>",
        },
    },
}
