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
import mx_util

import argparse
import hashlib
import itertools
import os.path
import shutil
import time

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

# A distribution that's a proxy for another LayoutDirDistribution. This adds the capability to skip building the
# other distribution, instead picking up a pre-built copy of it.
class CachedDistribution(mx.LayoutTARDistribution):
    def __init__(self, suite, name=None, deps=None, excludedLibs=None, platformDependent=True, theLicense=None, defaultBuild=True, layout=None, **kw_args):
        self._delegate = kw_args.pop('delegate')
        self.artifactInfo = kw_args.pop('artifactInfo')
        ciJob = kw_args.pop('ciJob', None)
        if ciJob is not None:
            ciJob = mx_subst.results_substitutions.substitute(ciJob)
        self.ciJob = ciJob
        super().__init__(suite, name=name, deps=[], theLicense=theLicense, platformDependent=True, layout={}, **kw_args)
        cache = self.cache_filename()
        if os.path.exists(cache):
            self.is_cached = True
            # switch output to a different directory so we never confuse downloaded and locally built artifacts
            self.output = os.path.join(self.get_cache_root(), name)
        else:
            self.is_cached = False
            self.deps = [self._delegate]
            self.layout = {
                "./": {
                    "source_type": "dependency",
                    "dependency": self._delegate,
                    "dereference": "never",
                    "path": "*",
                }
            }

    def get_cache_root(self):
        return os.path.join(self.suite.get_output_root(platformDependent=self.platformDependent), 'dists-cache')

    def cache_filename(self):
        return os.path.join(self.get_cache_root(), self.default_filename() + '.gz')

    def get_artifact_selector(self):
        ret = self.artifactInfo.copy()
        ret['revision'] = self.suite.version()
        if self.isJDKDependent():
            ret['javaVersion'] = str(mx.get_jdk().javaCompliance.value)
        if self.isPlatformDependent():
            ret['os'] = mx.get_os()
            ret['arch'] = mx.get_arch()
        return ret

    def getBuildTask(self, args):
        if self.is_cached:
            return CachedArchiveTask(args, self)
        else:
            return super(CachedDistribution, self).getBuildTask(args)

    def get_output(self):
        if self.is_cached:
            return self.output
        else:
            return super(CachedDistribution, self).get_output()


class CachedArchiveTask(mx.LayoutArchiveTask):
    def needsBuild(self, newestInput):
        if not os.path.isdir(self.subject.output):
            return (True, "does not exist")

        result = mx.TimeStampFile(self.subject.path)
        cache = mx.TimeStampFile(self.subject.cache_filename())
        if result.isOlderThan(cache):
            return (True, "distribution is older than downloaded file")
        else:
            return (False, "up to date")

    def build(self):
        zip = self.subject.path + '.gz'
        with mx_util.SafeFileCreation(zip) as sfc:
            shutil.copy(self.subject.cache_filename(), sfc.tmpPath)
            final_path = self.subject.postPull(sfc.tmpPath)
        if final_path:
            os.rename(final_path, self.subject.path)


@mx.command('llvm-project', 'publish-cache', '[options]')
def publish_cache(args):
    parser = argparse.ArgumentParser(prog='mx publish-cache')
    parser.add_argument('distribution', action='store',
                        help="Distribution to publish")
    args = parser.parse_args(args)

    artifact_uploader = mx.get_env('ARTIFACT_UPLOADER_SCRIPT')
    if not artifact_uploader:
        mx.abort("ARTIFACT_UPLOADER_SCRIPT is not set!")

    d = mx.distribution(args.distribution)
    if d.is_cached:
        mx.abort("Can not publish cached distribution. Try 'mx use-cache --reset' first to switch to a local build.")

    mx.log(f"Compressing {d.name}")
    file = d.prePush(d.path)

    mx.log("Checking for pre-existing upload")
    selector = d.get_artifact_selector()
    info = _query_artifact_info(**selector)
    if len(info) > 0:
        mx.log(f"Artifact already exists: {info[0]['artifactName']}\nSkipping upload.")
        return

    upload_cmd = [artifact_uploader, file, d.artifactName.format(**selector), "graal",
                  "--lifecycle", "cache",
                  "--artifact-type", selector['artifactType'],
                  "--revision", selector['revision']
                  ]
    if 'os' in selector:
        upload_cmd += ["--platform", f"{selector['os']}-{selector['arch']}"]
    if 'javaVersion' in selector:
        upload_cmd += ["--jdk", selector['javaVersion']]

    for (retry, fatal) in [("", False), (" (retry 1)", False), (" (final retry)", True)]:
        mx.log(f"Uploading...{retry}")
        retcode = mx.run(upload_cmd, nonZeroIsFatal=fatal)
        if retcode:
            mx.log("Upload failed, maybe another job concurrently uploaded the same artifact? Waiting 30 seconds, then retrying...")
            time.sleep(30)
        else:
            return

@mx.command('llvm-project', 'use-cache', '[options]')
def use_cache(args):
    parser = argparse.ArgumentParser(prog='mx use-cache')
    parser.add_argument('distribution', action='store',
                        help="The cached distribution")

    group = parser.add_mutually_exclusive_group()
    group.add_argument('--reset', action='store_true',
                       help="Switch distribution back to local build")
    group.add_argument('--file', action='store',
                       help="Use a given file as cache")

    args = parser.parse_args(args)

    d = mx.distribution(args.distribution)
    symlink = d.cache_filename()

    if args.reset:
        if os.path.islink(symlink):
            os.unlink(symlink)
        else:
            mx.warn("Cache symlink doesn't exist")
    elif args.file is not None:
        if not os.path.exists(args.file):
            mx.abort("Cache file does not exist")
        dir = os.path.dirname(symlink)
        mx_util.ensure_dir_exists(dir)
        if os.path.islink(symlink):
            os.unlink(symlink)
        os.symlink(os.path.relpath(args.file, dir), symlink)
    else:
        if os.path.islink(symlink):
            mx.log(f"{d.name}: cached ({os.readlink(symlink)})")
        else:
            mx.log(f"{d.name}: local build")
