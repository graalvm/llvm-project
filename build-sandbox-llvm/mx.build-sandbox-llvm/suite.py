suite = {
    "mxversion": "7.65.0",
    "name": "build-sandbox-llvm",
    "versionConflictResolution" : "latest",
    "capture_suite_commit_info": False,

    "imports" : {
    },

    "libraries": {
        "BOOTSTRAP_LLVM" : {
            "version" : "22.1.8-4-g1d96596a53-bg6891668b1e",
            "host" : "https://lafo.ssw.uni-linz.ac.at/pub/llvm",
            "packedResource": True,
            "os_arch" : {
                "linux" : {
                    "amd64" : {
                        "urls" : ["{host}/llvm-{version}-linux-amd64.tar.gz"],
                        "digest" : "sha512:a7c7db68d93d6d446ba0a8fb57c718f9fbed8c3027893a4201b980f368dc7a33fe45c9e979a1d7c9576ec15239b42de08a6d86e020941f1569e72128d9228f9a",
                    },
                    "aarch64" : {
                        "urls" : ["{host}/llvm-{version}-linux-aarch64.tar.gz"],
                        "digest" : "sha512:9428707b32cab9fca85b756aea88daa0e7ea818a53d7e4a75b7e0ee6d7ba4ad82e738764d0464a072a1a4611432c59c478f0761130dabca5311fea3d01866e97",
                    },
                    "riscv64": {
                        "urls" : ["{host}/llvm-{version}-linux-riscv64.tar.gz"],
                        "digest" : "sha512:f9139c334c80fd198c6b4f24b8afedb756e9f484724f741ebe9188dd5a959d19fd6e2e3b097cac07ca8fa7c4a5655987b4fd5030a1066bcb0452dbf2c77a5e82",
                    },
                },
                "darwin" : {
                    "aarch64" : {
                        "urls" : ["{host}/llvm-{version}-darwin-aarch64.tar.gz"],
                        "digest" : "sha512:6c061e61b94756085b8eb104920e2eb2b437d61d7e02e30e8ba3d1b50507b9f531778f729f0b1de254f9b9ef0d7ac052d085b54bb47af425987e29e46590faaa",
                    }
                },
                "windows" : {
                    "amd64" : {
                        "urls" : ["{host}/llvm-{version}-windows-amd64.tar.gz"],
                        "digest" : "sha512:f868d2e752fa984cf254707b781c2a4f8307de1c229c81a70b833d42c9620c995b741f9927235706a07a7a0f3af185690df8076ffb7c5ce04ec8744ee8997229",
                    }
                },
                "<others>": {
                    "<others>": {
                        "optional": True,
                    }
                },
            },
        },
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
                "CMAKE_C_COMPILER" : "<path:BOOTSTRAP_LLVM>/bin/clang",
                "CMAKE_CXX_COMPILER" : "<path:BOOTSTRAP_LLVM>/bin/clang++",

                # avoid putting the URL of the CI's git mirror into the version string
                "LLVM_FORCE_VC_REPOSITORY": "https://github.com/graalvm/llvm-project.git",
                # if we override the repository, the autodetection of the revision gets turned off, so do it manually here
                "LLVM_FORCE_VC_REVISION": "<suite-version:build-sandbox-llvm>",

                # Linking against libstdc++ is necessary, otherwise cmake configure tests are failing.
                # fortran: since musl does not have proper support for float128 we disable it for the moment
                "RUNTIMES_CMAKE_ARGS": "-DCMAKE_C_FLAGS=-DWITHOUT_FLOAT128;-DCMAKE_CXX_FLAGS=-stdlib=libstdc++ -DWITHOUT_FLOAT128",
                "BUILTINS_CMAKE_ARGS": "-DCMAKE_C_FLAGS=-DWITHOUT_FLOAT128;-DCMAKE_CXX_FLAGS= -DWITHOUT_FLOAT128",


                "LLVM_ENABLE_PROJECTS": "clang;lld;flang",
                "LLVM_ENABLE_RUNTIMES": "compiler-rt",

                "LLVM_TARGETS_TO_BUILD": "Native",
                "LLVM_LINK_LLVM_DYLIB": "YES",

                "CLANG_DEFAULT_LINKER": "lld",
                "CLANG_DEFAULT_RTLIB": "compiler-rt",
                "CLANG_DEFAULT_CXX_STDLIB": "libc++",

                # reduce dependencies, make more system independent
                "LLVM_ENABLE_LIBXML2": "NO",

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
            "buildDependencies" : [ "BOOTSTRAP_LLVM" ],
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
