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
    r'z[.-]|expat|fontconfig|freetype|dbus|udev|cap[.-]|pcre|systemd|gbm|'
    r'glib|gobject|gmodule|gthread|gio|ibus|pipewire|decor|fribidi|thai|datrie|'
    r'jack|sndio|usb|unwind|harfbuzz|graphite))'
)


def copy_file(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)



def host_dependencies(path, env=None):
    try:
        linked = subprocess.check_output(["ldd", str(path)], text=True,
                                         stderr=subprocess.STDOUT, env=env)
    except subprocess.CalledProcessError as error:
        if any(message in str(error.output) for message in
               ("not a dynamic executable", "statically linked")):
            return set()
        raise SystemExit(f"cannot inspect Linux tool dependencies for {path}: {error}") from error
    except OSError as error:
        raise SystemExit(f"cannot inspect Linux tool dependencies for {path}: {error}") from error
    missing = re.findall(r"(\S+)\s+=>\s+not found", linked)
    if missing:
        raise SystemExit(f"unresolved Linux tool dependencies for {path}: " + ", ".join(missing))
    return {Path(hit) for hit in re.findall(r"=> (/\S+)", linked)
            if not GLIBC.match(Path(hit).name)}


def copy_target_libraries(sysroot, libraries):
    """Copy target libraries, their link aliases and complete runtime closure."""
    pending = list(libraries)
    copied, inspected, aliases = set(), set(), {}
    while pending:
        logical = pending.pop()
        if logical in copied:
            continue
        if not logical.startswith(("/usr/lib/", "/lib/", "/lib64/")):
            raise SystemExit(f"target SDK library outside system library roots: {logical}")
        source = Path(logical)
        if not source.is_file():
            raise SystemExit(f"unresolved target SDK library: {logical}")
        copy_file(source, sysroot / logical.lstrip("/"))
        copied.add(logical)
        resolved = source.resolve()

        # Keep both SONAME and development names (-lfoo) for each dependency.
        parent = logical.rsplit("/", 1)[0]
        if parent not in aliases:
            index = {}
            for alias in source.parent.iterdir():
                if alias.is_file() and ".so" in alias.name:
                    index.setdefault(alias.resolve(), []).append(parent + "/" + alias.name)
            aliases[parent] = index
        pending.extend(aliases[parent].get(resolved, ()))

        if resolved in inspected:
            continue
        inspected.add(resolved)
        with source.open("rb") as stream:
            if stream.read(4) != b"\x7fELF":
                # GNU linker scripts and static startup archives still get copied.
                continue
        try:
            linked = subprocess.check_output(["ldd", str(source)], text=True,
                                             stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as error:
            if any(message in str(error.output) for message in
                   ("not a dynamic executable", "statically linked")):
                continue
            raise SystemExit(f"cannot inspect target SDK library {logical}: {error}") from error
        except OSError as error:
            raise SystemExit(f"cannot inspect target SDK library {logical}: {error}") from error
        missing = re.findall(r"(\S+)\s+=>\s+not found", linked)
        if missing:
            raise SystemExit(f"unresolved target SDK dependencies for {logical}: " +
                             ", ".join(missing))
        pending.extend(re.findall(r"=> (/\S+)", linked))


def isolate_host_runtime(output, patchelf=None):
    """Give every native tool its private dependencies, including on self-exec."""
    candidates = list((output / 'libexec').iterdir())
    candidates += list((output / 'cmake/bin').iterdir())
    candidates += list((output / 'host-libs').glob('*'))
    candidates += list((output / 'lib').glob('*.so*'))
    candidates += list((output / 'lib').glob('python*/lib-dynload/*.so'))
    for path in candidates:
        if path.is_symlink() or not path.is_file():
            continue
        with path.open('rb') as stream:
            if stream.read(4) != b'\x7fELF':
                continue
        patcher = patchelf or shutil.which('patchelf')
        if not patcher:
            raise SystemExit('Linux SDK runtime isolation requires --patchelf '
                             '(available in the existing linuxdeploy AppImage)')
        directories = (output / 'host-libs', output / 'lib')
        rpath = ':'.join('$ORIGIN/' + os.path.relpath(directory, path.parent)
                         for directory in directories)
        # DT_RPATH covers transitive imports in this process, never child tools
        # such as the player's /bin/sh. Core glibc remains supplied by the host.
        subprocess.run([str(patcher), '--force-rpath', '--set-rpath', rpath, str(path)],
                       check=True)


def export_sdk(output, cxx=None, patchelf=None):
    output.mkdir(parents=True, exist_ok=True)
    programs = {}
    for name in ['clang', 'ld.lld', 'llvm-ar', 'cmake', 'ninja', 'pkg-config', 'wayland-scanner', 'python3']:
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

    required_tools = {'clang', 'ld.lld', 'llvm-ar', 'cmake', 'ninja', 'pkg-config', 'wayland-scanner', 'python3'}
    missing = sorted(required_tools - programs.keys())
    if missing:
        raise SystemExit('Linux SDK host tools missing: ' + ', '.join(missing))

    libraries = set()
    for name, path in programs.items():
        target = output / 'cmake/bin/cmake' if name == 'cmake' else output / 'libexec' / name
        copy_file(Path(path), target)
        libraries.update(host_dependencies(path))

    # glslang generates its build information with Python. Ship that build tool
    # and its standard library instead of depending on the player's Python.
    python_stdlib = Path(subprocess.check_output(
        [programs['python3'], '-c', 'import sysconfig; print(sysconfig.get_path("stdlib"))'],
        text=True).strip())
    if not python_stdlib.is_dir() or not re.fullmatch(r'python3\.\d+', python_stdlib.name):
        raise SystemExit(f'cannot locate the SDK Python standard library: {python_stdlib}')
    shutil.copytree(python_stdlib, output / 'lib' / python_stdlib.name,
                    dirs_exist_ok=True, ignore=shutil.ignore_patterns('__pycache__'))
    for extension in python_stdlib.rglob('*.so'):
        if extension.is_file():
            libraries.update(host_dependencies(extension))
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
    target_libraries = []
    for directory in ('usr/lib/x86_64-linux-gnu', 'lib/x86_64-linux-gnu'):
        base = Path('/' + directory)
        for source in base.iterdir():
            if source.is_file() and SDK_LIBRARY.match(source.name):
                target_libraries.append('/' + directory + '/' + source.name)
        # pkg-config includes target-generated headers here, outside /usr/include.
        for headers in base.rglob('include'):
            if headers.is_dir():
                shutil.copytree(headers, sysroot / directory / headers.relative_to(base),
                                dirs_exist_ok=True)
    copy_target_libraries(sysroot, target_libraries)
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

    isolate_host_runtime(output, patchelf)

    bin_dir = output / 'bin'
    bin_dir.mkdir(exist_ok=True)
    wrapper = bin_dir / 'run-tool'
    wrapper.write_text('''#!/bin/sh
set -eu
tool=$(basename "$0")
sdk=$(dirname "$(dirname "$(readlink -f "$0")")")
# Native tools carry relative RPATHs. Do not export SDK libraries into host
# subprocesses: CMake/Ninja also start the player's /bin/sh.
export PATH="$sdk/bin:$sdk/libexec:$PATH"
case "$tool" in
  cmake)
    exec "$sdk/cmake/bin/cmake" "$@" ;;
  clang|clang++)
    exec "$sdk/libexec/$tool" --sysroot="$sdk/sysroot" --gcc-toolchain="$sdk/sysroot/usr" \\
      -resource-dir="$sdk/lib/clang/@RESOURCE_VERSION@" -L"$sdk/lib" "$@" ;;
  python3)
    unset PYTHONPATH
    export PYTHONHOME="$sdk"
    export PYTHONNOUSERSITE=1
    export PYTHONDONTWRITEBYTECODE=1 ;;
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
    parser.add_argument('--patchelf', type=Path, default=None,
                        help='existing patchelf, also bundled inside linuxdeploy')
    args = parser.parse_args()
    export_sdk(args.output, args.cxx, args.patchelf)
