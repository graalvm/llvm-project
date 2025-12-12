local fast_machine = {
    capabilities +: ["!x82_16_367", "!e5_2650v2", "!e5_2630v3", "!x52", "manycores", "ram72gb"]
};

local mx = {
    mx(args):: ["mx", "-p", "build-sandbox-llvm"] + args,
    environment+: {
        MX_PYTHON: "python3.8",
        PYTHONIOENCODING: "utf-8",
    },
    packages+: {
        python3: "==3.8.10",
        "pip:ninja_syntax": "==1.7.2",
        "mx": "==7.65.0",
    },
    python_version: "3",
};

local cmake = {
    packages+: {
        cmake: "==3.22.2",
    },
};

local jdk = {
    downloads +: {
        "JAVA_HOME": {"name": "jpg-jdk", "version": "21.0.2", "build_id": "jdk-21.0.2+13", "platformspecific": true},
    }
};

local ondemand_build_sandbox_llvm = fast_machine + mx + cmake + jdk + {
    local build = self,
    local os_arch = { os: build.os, arch: build.arch },

    name: "ondemand-build-sandbox-llvm",
    targets: ["ondemand"],
    deploysArtifacts: true,
    run: [
        self.mx(["build", "--dependencies", "CACHED_SANDBOX_LLVM"]),
        self.mx(["publish-cache", "CACHED_SANDBOX_LLVM"]),
    ],
    publishArtifacts+: [
        {
            name: std.format("cached-sandbox-llvm-%(os)s-%(arch)s", os_arch),
            dir: std.format("build-sandbox-llvm/mxbuild/%(os)s-%(arch)s/dists", os_arch),
            patterns: ["cached-sandbox-llvm.tar.gz"],
        },
    ],
};

local useCachedLLVM = {
    local build = self,
    local os_arch = { os: build.os, arch: build.arch },

    requireArtifacts+: [
        {
            name: std.format("cached-sandbox-llvm-%(os)s-%(arch)s", os_arch),
            dir: "cached-llvm",
            autoExtract: true,
        },
    ],

    local cache_filename = ["realpath", "cached-llvm/cached-sandbox-llvm.tar.gz"],
    setup+: [
        self.mx(["use-cache", "CACHED_SANDBOX_LLVM", "--file", cache_filename]),
    ],
};

local gate_smoketest = mx + cmake + jdk + useCachedLLVM + {
    name: "gate-smoketest",
    targets: ["tier1"],
    run+: [
        self.mx(["build", "--dependencies", "CACHED_SANDBOX_LLVM"]),
        ["set-export", "SANDBOX_LLVM", self.mx(["--quiet", "--no-warning", "path", "--output", "CACHED_SANDBOX_LLVM"])],
        ["$SANDBOX_LLVM/usr/bin/clang", "--version"],
    ],
};

local linux_amd64 = {
    os:: "linux",
    arch:: "amd64",
    name +: "-linux-amd64",
    capabilities +: ["linux", "amd64"],
    docker: {
        image: "buildslave_ol7",
        mount_modules: true
    },
};

{
    specVersion: "7",
    tierConfig: {
        tier1: "gate",
    },
    builds: [
        ondemand_build_sandbox_llvm + linux_amd64,
        gate_smoketest + linux_amd64,
    ]
}
