#!/usr/bin/env python3
"""Export the existing Linux / Steam Runtime SDK into a relocatable offline player toolchain.
Bundles clang, lld, cmake, ninja, libc++ and minimal CRT headers for self-building release packages.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess

GLIBC = re.compile(r'^(ld-linux|lib(c|m|dl|pthread|rt|resolv|util|anl|nss_[^.]+))\.so')
SDK_LIBRARY = re.compile(
    r'^(ld-linux|crt|[SMr]?crt|lib(c[.-]|c_nonshared|m[.-]|mvec|dl|pthread|rt[.-]|resolv|util|anl|'
    r'gcc|X|xcb|GL|glapi|EGL|vulkan|asound|pulse|wayland|xkbcommon|drm|ffi|bsd|md[.-]|'
    r'z[.-]|expat|fontconfig|freetype|dbus|udev|cap[.-]|pcre|systemd))'
)


def copy_file(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)


def export_sdk(output, cxx=None):
    output.mkdir(parents=True, exist_ok=True)
    programs = {}
    for name in ['clang', 'ld.lld', 'llvm-ar', 'cmake', 'ninja', 'pkg-config']:
        which = shutil.which(name)
        if which:
            programs[name] = which

    if not programs.get('clang'):
        # Search common llvm locations
        for p in Path('/usr/lib').glob('llvm-*/bin/clang'):
            programs['clang'] = str(p)
            programs['ld.lld'] = str(p.parent / 'lld')
            programs['llvm-ar'] = str(p.parent / 'llvm-ar')
            break

    libraries = set()
    for name, path in programs.items():
        target = output / 'cmake/bin/cmake' if name == 'cmake' else output / 'libexec' / name
        copy_file(Path(path), target)
        try:
            linked = subprocess.check_output(['ldd', path], text=True)
            for hit in re.findall(r'=> (/\S+)', linked):
                library = Path(hit)
                if not GLIBC.match(library.name):
                    libraries.add(library)
        except Exception:
            pass

    for library in libraries:
        copy_file(library, output / 'host-libs' / library.name)

    for alias, target in {'clang++': 'clang', 'llvm-ranlib': 'llvm-ar'}.items():
        path = output / 'libexec' / alias
        path.unlink(missing_ok=True)
        try:
            path.symlink_to(target)
        except Exception:
            shutil.copy2(output / 'libexec' / target, path)

    # Copy Clang resource dir
    clang_bin = Path(programs['clang'])
    llvm_candidates = [
        Path('/usr/lib/llvm-23/lib/clang'),
        clang_bin.parent.parent / 'lib/clang',
        Path('/usr/lib/clang'),
    ]
    clang_lib = next((p for p in llvm_candidates if p.exists() and p.is_dir()), None)
    if clang_lib:
        shutil.copytree(clang_lib, output / 'lib/clang', dirs_exist_ok=True, symlinks=True)

    # Copy CMake modules
    for cmake_share in Path('/usr/share').glob('cmake*'):
        if cmake_share.is_dir():
            shutil.copytree(cmake_share, output / 'cmake/share' / cmake_share.name, dirs_exist_ok=True)

    if cxx and cxx.exists():
        for inc in cxx.glob('**/include/c++'):
            shutil.copytree(inc, output / 'include/c++', dirs_exist_ok=True)
        for lib in cxx.glob('**/lib/libc++*'):
            if lib.is_file():
                copy_file(lib, output / 'lib' / lib.name)
        for lib in cxx.glob('**/lib/libunwind*'):
            if lib.is_file():
                copy_file(lib, output / 'lib' / lib.name)


    bin_dir = output / 'bin'
    bin_dir.mkdir(exist_ok=True)
    wrapper = bin_dir / 'run-tool'
    wrapper.write_text('''#!/bin/sh
set -eu
tool=$(basename "$0")
sdk=$(dirname "$(dirname "$(readlink -f "$0")")")
export LD_LIBRARY_PATH="$sdk/host-libs:$sdk/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PATH="$sdk/bin:$PATH"
case "$tool" in
  cmake)
    exec "$sdk/cmake/bin/cmake" "$@" ;;
  clang|clang++)
    exec "$sdk/libexec/$tool" "$@" ;;
  pkg-config)
    exec "$sdk/libexec/pkg-config" "$@" ;;
esac
exec "$sdk/libexec/$tool" "$@"
''')
    wrapper.chmod(0o755)

    for name in (*programs, 'clang++', 'llvm-ranlib'):
        path = bin_dir / name
        path.unlink(missing_ok=True)
        try:
            path.symlink_to('run-tool')
        except Exception:
            shutil.copy2(wrapper, path)

    print(f'Exported offline Linux SDK to {output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cxx', type=Path, default=None)
    args = parser.parse_args()
    export_sdk(args.output, args.cxx)
