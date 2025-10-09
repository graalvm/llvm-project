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

                # Linking against libstdc++ is necessary, otherwise cmake configure tests are failing.
                # fortran: since musl does not have proper support for float128 we disable it for the moment
                "RUNTIMES_CMAKE_ARGS": "-DCMAKE_C_FLAGS=-DWITHOUT_FLOAT128;-DCMAKE_CXX_FLAGS=-stdlib=libstdc++ -DWITHOUT_FLOAT128",
                "BUILTINS_CMAKE_ARGS": "-DCMAKE_C_FLAGS=-DWITHOUT_FLOAT128;-DCMAKE_CXX_FLAGS= -DWITHOUT_FLOAT128",


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
    },

    "distributions" : {
        "SANDBOX_LLVM": {
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

        # for publishing to the cache
        "CACHED_SANDBOX_LLVM": {
            "class": "CachedDistribution",
            "delegate": "SANDBOX_LLVM",
            "artifactName": "sandbox-llvm-{os}-{arch}-g{revision}.tar.gz",
            "artifactInfo": {
                "artifactType": "sandbox-llvm",
            },
            "ciJob": "ondemand-build-sandbox-llvm-<os>-<arch>",
        },
    },
}
