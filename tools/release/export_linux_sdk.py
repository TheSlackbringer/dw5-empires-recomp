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
    for name in ['clang', 'ld.lld', 'llvm-ar', 'cmake', 'ninja', 'pkg-config', 'wayland-scanner']:
        if name in ('clang', 'ld.lld', 'llvm-ar'):
            candidates = [
                f"/usr/lib/llvm-23/bin/{'lld' if name == 'ld.lld' else name}",
                f"{name}-23",
                f"/usr/lib/llvm-22/bin/{name}",
                f"{name}-22",
                f"/usr/lib/llvm-21/bin/{name}",
                f"{name}-21",
                f"/usr/lib/llvm-20/bin/{name}",
                f"{name}-20",
                f"/usr/lib/llvm-19/bin/{name}",
                f"{name}-19",
                f"/usr/lib/llvm-18/bin/{name}",
                f"{name}-18",
                name,
            ]
        else:
            candidates = [name]
        for cand in candidates:
            which = shutil.which(cand) or (cand if Path(cand).is_file() else None)
            if which:
                programs[name] = str(which)
                break

    if not programs.get('clang'):
        # Search common llvm locations in descending order
        for p in sorted(Path('/usr/lib').glob('llvm-*/bin/clang'), reverse=True):
            programs['clang'] = str(p)
            programs['ld.lld'] = str(p.parent / 'lld')
            programs['llvm-ar'] = str(p.parent / 'llvm-ar')
            break

    required_tools = {'clang', 'ld.lld', 'llvm-ar', 'cmake', 'ninja', 'pkg-config', 'wayland-scanner'}
    missing = sorted(required_tools - programs.keys())
    if missing:
        raise SystemExit('Linux SDK host tools missing: ' + ', '.join(missing))

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
        except subprocess.CalledProcessError as exc:
            if 'not a dynamic executable' not in str(exc.output):
                raise SystemExit(f'cannot inspect Linux tool dependencies for {path}: {exc}') from exc
        except OSError as exc:
            raise SystemExit(f'cannot inspect Linux tool dependencies for {path}: {exc}') from exc

    for library in libraries:
        copy_file(library, output / 'host-libs' / library.name)

    for alias, target in {'clang++': 'clang', 'llvm-ranlib': 'llvm-ar', 'lld': 'ld.lld'}.items():
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
        shutil.copytree(clang_lib, output / 'lib/clang', dirs_exist_ok=True)

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

    # Reuse the DW5XL offline SDK layout: compiler binaries alone still depend on
    # the player's C development headers, startup objects and linker scripts.
    sysroot = output / 'sysroot'
    shutil.copytree(Path('/usr/include'), sysroot / 'usr/include', dirs_exist_ok=True)
    for directory in ('usr/lib/x86_64-linux-gnu', 'lib/x86_64-linux-gnu'):
        for source in Path('/' + directory).iterdir():
            if source.is_file() and SDK_LIBRARY.match(source.name):
                copy_file(source, sysroot / directory / source.name)
    for version in Path('/usr/lib/gcc/x86_64-linux-gnu').glob('*'):
        if version.is_dir():
            for source in version.iterdir():
                if source.is_file() and source.name.startswith(('crt', 'libgcc', 'libgcov')):
                    copy_file(source, sysroot / 'usr/lib/gcc/x86_64-linux-gnu' /
                              version.name / source.name)
    copy_file(Path('/lib64/ld-linux-x86-64.so.2'), sysroot / 'lib64/ld-linux-x86-64.so.2')
    for directory in ('usr/lib/x86_64-linux-gnu/pkgconfig', 'usr/share/pkgconfig'):
        source = Path('/' + directory)
        if source.is_dir():
            shutil.copytree(source, sysroot / directory, dirs_exist_ok=True)
    for documentation in (Path('/usr/share/doc'), cxx / 'usr/share/doc' if cxx else None):
        if documentation and documentation.is_dir():
            for source in documentation.rglob('*'):
                if source.is_file():
                    copy_file(source, output / 'licenses' / source.relative_to(documentation))
    versions = sorted((p.name for p in (output / 'lib/clang').iterdir()
                       if p.is_dir() and re.fullmatch(r'\d+(?:\.\d+)*', p.name)),
                      key=lambda name: tuple(map(int, name.split('.'))))
    if not versions:
        raise SystemExit('Clang resource headers are missing from the exported Linux SDK')
    resource_version = versions[-1]


    bin_dir = output / 'bin'
    bin_dir.mkdir(exist_ok=True)
    wrapper = bin_dir / 'run-tool'
    wrapper.write_text('''#!/bin/sh
set -eu
tool=$(basename "$0")
sdk=$(dirname "$(dirname "$(readlink -f "$0")")")
export LD_LIBRARY_PATH="$sdk/host-libs:$sdk/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PATH="$sdk/bin:$sdk/libexec:$PATH"
case "$tool" in
  cmake)
    exec "$sdk/cmake/bin/cmake" "$@" ;;
  clang|clang++)
    exec "$sdk/libexec/$tool" --sysroot="$sdk/sysroot" --gcc-toolchain="$sdk/sysroot/usr" \\
      -resource-dir="$sdk/lib/clang/@RESOURCE_VERSION@" -L"$sdk/lib" "$@" ;;
  pkg-config)
    export PKG_CONFIG_SYSROOT_DIR="$sdk/sysroot"
    export PKG_CONFIG_LIBDIR="$sdk/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig:$sdk/sysroot/usr/share/pkgconfig" ;;
esac
exec "$sdk/libexec/$tool" "$@"
'''.replace('@RESOURCE_VERSION@', resource_version))
    wrapper.chmod(0o755)

    for name in (*programs, 'clang++', 'llvm-ranlib', 'lld'):
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
