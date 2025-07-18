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
        "mx": "==7.58.9",
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
    name: "ondemand-build-sandbox-llvm",
    targets: ["ondemand"],
    run: [
        self.mx(["build", "--dependencies", "SANDBOX_LLVM"]),
    ],
};

local linux_amd64 = {
    name +: "-linux-amd64",
    capabilities +: ["linux", "amd64"],
    docker: {
        image: "buildslave_ol7",
        mount_modules: true
    },
};

{
    specVersion: "7",
    builds: [
        ondemand_build_sandbox_llvm + linux_amd64
    ]
}
