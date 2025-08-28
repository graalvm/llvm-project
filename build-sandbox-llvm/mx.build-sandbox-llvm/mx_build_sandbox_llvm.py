#
# Copyright (c) 2025, Oracle and/or its affiliates.
#
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification, are
# permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this list of
# conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice, this list of
# conditions and the following disclaimer in the documentation and/or other materials provided
# with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors may be used to
# endorse or promote products derived from this software without specific prior written
# permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS
# OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
# GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
# AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
# OF THE POSSIBILITY OF SUCH DAMAGE.
#

# re-export custom mx project classes so they can be used from suite.py
from mx_cmake import CMakeNinjaProject #pylint: disable=unused-import

import mx
import mx_subst

import hashlib
import itertools
import os.path

def suite_version(arg):
    suite = mx.suite(arg)
    return suite.version()

mx_subst.path_substitutions.register_with_arg('suite-version', suite_version)

# The LLVM build system has a bug that makes ninja rebuild queries to always say there is something to do.
# In reality, it's not doing anything, only the install step will bump the timestamp of some unchanged symlinks.
# This leads to mx believing something changed, and it will rebuild all the reverse dependencies of LLVM, which
# includes quite expensive builds the sandboxed jdk, the native libs of native-image, and transitively all native
# images.
#
# To work around this, we use this special distribution for SANDBOX_LLVM. It's identical to a LayoutDirDistribution,
# but after the build, it does a checksum over the result to determine whether something changed. If the checksum is
# unchanged, we tell mx that nothing changed by returning False from the `build` function. We also reset the mtime
# of all files in the distribution, so the build system of other projects (e.g. libc++) doesn't believe it has to
# rebuild things because of dependencies on headers from SANDBOX_LLVM.
class LLVMLayoutDistribution(mx.LayoutDirDistribution):
    def __init__(self, suite, name=None, deps=None, excludedLibs=None, platformDependent=True, theLicense=None, defaultBuild=True, **kw_args):
        super().__init__(suite, name=name, deps=[], theLicense=theLicense, platformDependent=True, **kw_args)

    def getBuildTask(self, args):
        return LLVMArchiveTask(args, self)


class LLVMArchiveTask(mx.LayoutArchiveTask):
    def __init__(self, args, dist):
        super(LLVMArchiveTask, self).__init__(args, dist)

    def build(self):
        super(LLVMArchiveTask, self).build()

        hash = hashlib.new("sha512")
        output = self.subject.get_output()
        for root, dirs, files in os.walk(output, topdown=True):
            # get deterministic traversal order
            dirs.sort()
            files.sort()

            dir = os.path.relpath(root, output)
            hash.update(f"ENTER {dir}\0".encode("utf-8"))
            for f in files:
                path = os.path.join(root, f)
                if os.path.islink(path):
                    hash.update(f"link {f} -> {os.readlink(path)}\0".encode("utf-8"))
                else:
                    hash.update(f"file {f}\0".encode("utf-8"))
                    with open(path, "rb") as fd:
                        # compat: hashlib.file_digest was introduced in python 3.11, our CI is at 3.8
                        # hashlib.file_digest(fd, lambda: hash)
                        while True:
                            content = fd.read(65536)
                            if not content:
                                break
                            hash.update(content)

            hash.update(f"LEAVE {dir}\0".encode("utf-8"))

        new_digest = hash.hexdigest()
        digest_file = self.subject._default_path() + ".digest"
        if os.path.exists(digest_file):
            with open(digest_file, "r") as f:
                old_digest = f.readline().strip()
        else:
            old_digest = "<unknown>"

        if old_digest == new_digest:
            # nothing changed
            # reset the file timestamps back to avoid unnecessary rebuilding of reverse dependencies
            mtime = os.path.getmtime(digest_file)
            os.utime(self.subject._default_path(), (mtime, mtime))
            for root, dirs, files in os.walk(output, topdown=True):
                for f in itertools.chain(dirs, files):
                    path = os.path.join(root, f)
                    os.utime(path, (mtime, mtime))
            return False
        else:
            with open(digest_file, "w") as f:
                f.write(new_digest)
            return True

