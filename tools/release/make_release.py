#!/usr/bin/env python3
"""Release packaging for Dynasty Warriors 5: Empires (Issue 001).
Packs self-building release packages (Windows ZIP and Linux AppImage) with bundled offline compiler toolchains.
Enforces strict clean-room checks: packages contain ZERO game files, ZERO game binaries, and ZERO generated C++.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

import export_linux_sdk

from verify_package import validate_package

REPO = Path(__file__).resolve().parents[2]
LLVM_MINGW_HOST = Path(os.environ.get('LLVM_MINGW_ROOT', Path.home() / '.local/opt/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64'))
WIN_TOOLS = Path(os.environ.get('DW5E_WIN_TOOLS', Path.home() / '.local/opt/windows-builder-tools'))
LLVM_MINGW_WIN_ZIP = WIN_TOOLS / 'llvm-mingw-20261006-ucrt-x86_64.zip'
CMAKE_WIN_ZIP = WIN_TOOLS / 'cmake-4.4.4-windows-x86_64.zip'
NINJA_WIN_ZIP = WIN_TOOLS / 'ninja-win.zip'

README_WINDOWS = """Dynasty Warriors 5: Empires - PC Port (Xbox 360)
================================================

A native Windows port made by static recompilation: the game's Xbox 360 Xenon code
is translated to C++ and compiled for your PC. It is not an emulator.

THIS DOWNLOAD CONTAINS NOTHING FROM THE GAME - no game files and no game code.
It is a builder: on the first start it makes the game on your PC from your own disc.
You need:
  * Dynasty Warriors 5: Empires, European PAL (Title ID: 4B4F07D3), as a disc image (.iso)
You do not need to install anything: the compiler and all tools are in this folder.

Play
  1. Unzip to a short path, for example C:\\Games\\DW5E (keep the folder together).
  2. Start DW5E.exe. Windows may say "Windows protected your PC" because the program is not
     signed: choose "More info", then "Run anyway".
  3. First start: choose your disc image and press "Install & Play". Your disc is checked,
     game files are extracted, and the code is compiled for your PC. This happens once
     and takes a few minutes (~2 to 4 min depending on your CPU). Then the game starts.
  4. Every later start of DW5E.exe starts the game at once (<1 second).

Your files
  Game files, the built game, saves, settings and logs are in %LOCALAPPDATA%\\DW5E.
  A newer version of this folder rebuilds the game once and keeps your saves.

Controls (Default Xbox 360 Gamepad / Keyboard):
  A / Space       - Jump
  X / J           - Normal Attack
  Y / I           - Charge Attack
  B / L           - Musou Attack
  LB / Shift      - Guard / Center Camera
  RB / K          - Bow / Strafe

Source code: https://github.com/TheSlackbringer/dw5-empires-recomp (GPL-3.0), version {build}.
Licenses of everything included are in the LICENSES folder.
This is a fan project, not affiliated with or endorsed by Koei Tecmo or Microsoft.
"""

README_LINUX = """Dynasty Warriors 5: Empires - Linux Builder
===========================================

A native Linux / Steam Deck port made by static recompilation: the game's Xbox 360 Xenon
code is translated to C++ and compiled for your PC. It is not an emulator.

THIS DOWNLOAD CONTAINS NOTHING FROM THE GAME - no game files and no game code.
It is a builder: on the first start it makes the game on your PC from your own disc.
You need:
  * Dynasty Warriors 5: Empires, European PAL (Title ID: 4B4F07D3), as a disc image (.iso)
The compiler and all build tools are bundled inside this AppImage; no downloads are needed.

Play
  1. Make this AppImage executable: chmod +x DW5E-*.AppImage
  2. Double-click or run the AppImage.
  3. First start: select your dw5e.iso and press "Install & Play".
  4. Later starts open the game directly without rebuilding.

Your files
  Game files, the built game, saves, and logs stay in $XDG_DATA_HOME/dw5e (normally ~/.local/share/dw5e),
  separate from this AppImage.

Requires x86-64 Linux with glibc 2.31 or newer and a Vulkan graphics driver.
Source code: https://github.com/TheSlackbringer/dw5-empires-recomp (GPL-3.0), version {build}.
"""


def fail(msg):
    sys.exit(f'release error: {msg}')


def git(*args, cwd=REPO):
    return subprocess.run(
        ['git', '-c', f'safe.directory={cwd.resolve()}', '-C', str(cwd), *args],
        env={**os.environ, 'GIT_OPTIONAL_LOCKS': '0'},
        capture_output=True, text=True, encoding='utf-8', check=True
    ).stdout


def version_for(test):
    if test:
        return git('describe', '--always').strip() + '-test'
    if git('status', '--porcelain', '--untracked-files=no').strip():
        fail('working tree has uncommitted changes; commit or use --test')
    return git('describe', '--tags', '--always').strip()


def copy_tracked(src_repo, rel_paths, dest, skip=()):
    src_repo = Path(src_repo).resolve()
    files = git('ls-files', '-z', '--recurse-submodules', '--', *rel_paths, cwd=src_repo).split('\0')
    # core.symlinks=false checkouts store a link's target as a small text file.
    # Resolve those index entries explicitly so Windows-built packages contain
    # the same materialized sources as packages assembled on Linux.
    links = set()
    for record in git('ls-files', '--stage', '-z', '--', *rel_paths, cwd=src_repo).split('\0'):
        metadata, separator, name = record.partition('\t')
        if separator and metadata.startswith('120000 '):
            links.add(name)
    n = 0
    for f in filter(None, files):
        if any(f.startswith(s) for s in skip):
            continue
        source = src_repo / f
        seen = set()
        while source.relative_to(src_repo).as_posix() in links and not source.is_symlink():
            if source in seen:
                fail(f'cyclic tracked source symlink: {f}')
            seen.add(source)
            link = source.read_text().strip()
            source = (source.parent / link).resolve()
            if not source.is_relative_to(src_repo.resolve()):
                fail(f'tracked source symlink leaves source tree: {f}')
        if not source.exists():
            fail(f'required tracked source missing: {source}')
        if source.is_dir():
            continue
        target = dest / f
        target.parent.mkdir(parents=True, exist_ok=True)
        try:
            shutil.copy2(source, target)
        except OSError as exc:
            fail(f'cannot copy required source {source}: {exc}')
        n += 1
    return n


# Windows and graphics-driver libraries supplied by the supported OS/driver.
# Everything else must resolve from the build or bundled toolchain.
SYSTEM_DLLS = {
    'advapi32.dll', 'avrt.dll', 'bcrypt.dll', 'cfgmgr32.dll', 'comctl32.dll',
    'comdlg32.dll', 'crypt32.dll', 'd3d12.dll', 'd3dcompiler_47.dll',
    'dbghelp.dll', 'dinput8.dll', 'dwmapi.dll', 'dxcore.dll', 'dxgi.dll', 'gdi32.dll',
    'hid.dll', 'imm32.dll', 'iphlpapi.dll', 'kernel32.dll', 'msvcrt.dll',
    'ntdll.dll', 'ole32.dll', 'oleaut32.dll', 'powrprof.dll', 'psapi.dll',
    'rpcrt4.dll', 'secur32.dll', 'setupapi.dll', 'shell32.dll', 'shlwapi.dll',
    'ucrtbase.dll', 'user32.dll', 'userenv.dll', 'uxtheme.dll', 'version.dll',
    'vulkan-1.dll', 'winhttp.dll', 'wininet.dll', 'winmm.dll', 'ws2_32.dll',
    'wtsapi32.dll', 'xinput1_4.dll', 'xinput9_1_0.dll',
}


def dll_closure(exe, search_dirs):
    tool_name = 'llvm-readobj.exe' if os.name == 'nt' else 'llvm-readobj'
    readobj = LLVM_MINGW_HOST / 'bin' / tool_name
    if not readobj.is_file():
        readobj = shutil.which(tool_name) or tool_name
    # Index nested CMake output directories as well as the toolchain bin dir.
    available = {}
    for directory in search_dirs:
        directory = Path(directory)
        if directory.is_dir():
            for path in sorted(directory.rglob('*')):
                if path.is_file() and path.suffix.lower() == '.dll':
                    available.setdefault(path.name.lower(), path)
    found, todo = {}, [exe]
    while todo:
        current = todo.pop()
        try:
            result = subprocess.run([str(readobj), '--coff-imports', str(current)],
                                    capture_output=True, text=True)
        except OSError as exc:
            fail(f'cannot inspect DLL dependencies of {current}: {exc}')
        if result.returncode:
            fail(f'DLL dependency inspection failed for {current}: {result.stderr.strip()}')
        for line in result.stdout.splitlines():
            line = line.strip()
            if not (line.startswith('Name:') and line.lower().endswith('.dll')):
                continue
            name = line.split(':', 1)[1].strip()
            key = name.lower()
            if key in found or key in SYSTEM_DLLS or key.startswith(('api-ms-win-', 'ext-ms-win-')):
                continue
            hit = available.get(key)
            if hit is None:
                fail(f'unresolved DLL dependency {name} required by {current}')
            found[key] = hit
            todo.append(hit)
    return sorted(found.values(), key=lambda p: p.name.lower())


def unzip_into(archive, dest, strip_top=True):
    with zipfile.ZipFile(archive) as z:
        for info in z.infolist():
            parts = Path(info.filename).parts
            rel = Path(*parts[1:]) if strip_top and len(parts) > 1 else Path(*parts)
            if not rel.parts:
                continue
            target = dest / rel
            if info.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with z.open(info) as src, open(target, 'wb') as out:
                shutil.copyfileobj(src, out)


# Disc files and compiled game markers that must NEVER be in a release download
FORBIDDEN_FILES = {
    'DEFAULT.XEX', 'DEFAULT.PE', 'LINKDATA.BIN', 'LINKSE.BIN', 'LINKVOI.BIN',
    'LINKDAT2.BIN', 'BGM.BIN', 'OPDATA.LNK', 'DW5E.ELF'
}
GAME_MARKER = b'dw5e_trace_ctor'


def scan(stage, windows=True):
    """Enforces 100% clean-room distribution: ZERO game files, ISOs, or compiled game binaries."""
    bad = []
    for p in stage.rglob('*'):
        rel = p.relative_to(stage).as_posix()
        name = p.name
        if windows and any(c in name for c in '<>:"|?*\\'):
            bad.append(f'{rel} (character not allowed on Windows)')
        if p.is_dir() or not p.exists():
            continue
        low = rel.lower()
        if (name.upper() in FORBIDDEN_FILES or
            low.endswith(('.iso', '.xex', '.pe', '.ghidra', '.pdb')) or
            re.search(r'(^|/)generated/default/', low) or
            re.match(r'fun_[0-9a-f]+.*\.cpp$', name.lower())):
            bad.append(rel)
        else:
            with p.open('rb') as stream:
                header = stream.read(4)
                if header[:2] == b'MZ' or header == b'\x7fELF':
                    prev = header
                    while chunk := stream.read(1024 * 1024):
                        if GAME_MARKER in prev + chunk:
                            bad.append(f'{rel} (the compiled game binary)')
                            break
                        prev = chunk[-len(GAME_MARKER):]
    if bad:
        fail('strictly forbidden in release package:\n  ' + '\n  '.join(sorted(bad)[:20]))


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def windows(build, recomp_build, test):
    # A Windows build that also builds the game puts the setup in setup/: there build/DW5E.exe is the game's
    # dw5e.exe (file names ignore case).
    launcher = build / 'setup' / 'DW5E.exe'
    if not launcher.exists():
        launcher = build / 'DW5E.exe'
    if not launcher.exists():
        launcher = build / 'dw5e_setup.exe'
    win_candidates = [
        recomp_build / 'rexgluerd.exe',
        recomp_build / 'rexglue.exe',
        build / 'rexglue-sdk/src/rexglue/rexgluerd.exe',
        build / 'rexglue-sdk/src/rexglue/rexglue.exe',
        REPO / 'rexglue-sdk/out/win-amd64/rexgluerd.exe',
        REPO / 'rexglue-sdk/out/win-amd64/rexglue.exe',
        REPO / 'tools/rexglue.exe',
    ]
    recompiler = next((p for p in win_candidates if p.exists()), None)

    if recompiler is None:
        fail('required ReXGlue recompiler missing; searched: ' +
             ', '.join(str(p) for p in win_candidates))

    for p in (launcher, LLVM_MINGW_WIN_ZIP, CMAKE_WIN_ZIP, NINJA_WIN_ZIP):
        if not p.exists():
            fail(f'required build artifact or tool missing: {p}')

    version = version_for(test)
    name = f'DW5E-{version}-windows-x64'
    dist = REPO / 'dist'
    stage = dist / name
    shutil.rmtree(stage, ignore_errors=True)
    stage.mkdir(parents=True)

    # Launcher
    shutil.copy2(launcher, stage / 'DW5E.exe')
    host_dlls = [recompiler.parent, build, LLVM_MINGW_HOST / 'x86_64-w64-mingw32/bin', LLVM_MINGW_HOST / 'bin']
    for dll in dll_closure(launcher, host_dlls):
        shutil.copy2(dll, stage / dll.name)
    (stage / 'BUILD_ID').write_text(version + '\n')

    # Clean tracked source code
    n = copy_tracked(REPO, ['app', 'patches', 'tools/windows', 'tools/linux', 'dw5e_manifest.toml', 'LICENSE', 'README.md'], stage,
                     skip=('app/generated/default/',))
    if (REPO / 'rexglue-sdk').exists():
        n += copy_tracked(REPO / 'rexglue-sdk', ['.'], stage / 'rexglue-sdk',
                          skip=('tools/binutils/', 'out/', 'tests/', 'docs/', 'thirdparty/moltenvk/', 'thirdparty/vulkan-loader/tests/'))

    # Bundled Tools
    tools = stage / 'tools'
    tools.mkdir(parents=True, exist_ok=True)
    shutil.copy2(recompiler, tools / 'rexglue.exe')
    for dll in dll_closure(recompiler, host_dlls):
        shutil.copy2(dll, tools / dll.name)

    unzip_into(LLVM_MINGW_WIN_ZIP, tools / 'llvm-mingw')
    unzip_into(CMAKE_WIN_ZIP, tools / 'cmake')
    unzip_into(NINJA_WIN_ZIP, tools, strip_top=False)

    # README and licenses
    (stage / 'README.txt').write_text(README_WINDOWS.format(build=version).replace('\n', '\r\n'))
    lic = stage / 'LICENSES'
    lic.mkdir(parents=True, exist_ok=True)
    shutil.copy2(REPO / 'LICENSE', lic / 'dw5e (GPL-3.0).txt')
    shutil.copy2(REPO / 'rexglue-sdk/LICENSE', lic / 'rexglue-sdk (BSD-3-Clause).txt')

    validate_package(stage, windows=True)
    scan(stage, windows=True)
    archive = dist / f'{name}.zip'
    archive.unlink(missing_ok=True)
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for p in sorted(stage.rglob('*')):
            if p.is_file():
                z.write(p, Path(name) / p.relative_to(stage))

    (dist / f'{name}.zip.sha256').write_text(f'{sha256(archive)}  {archive.name}\n')
    files = sum(1 for p in stage.rglob('*') if p.is_file())
    print(f'Created {archive} ({archive.stat().st_size / 2**20:.1f} MB, {files} files, {n} sources)')
    return archive



def bundle_linux_runtime(stage, binaries, env):
    """Fill linuxdeploy's excluded desktop dependencies without replacing glibc."""
    target = stage / 'usr/lib'
    target.mkdir(parents=True, exist_ok=True)
    libraries = set()
    for binary in binaries:
        libraries.update(export_linux_sdk.host_dependencies(binary, env=env))
    for library in sorted(libraries):
        if export_linux_sdk.GLIBC.match(library.name):
            continue
        destination = target / library.name
        # Keep any RPATH edits linuxdeploy already made to its own copies.
        if not destination.exists():
            shutil.copy2(library, destination)


def isolate_linux_tool_runtime(stage, binaries, env, patchelf):
    """Keep the offline translator's dependency closure out of the game process."""
    private = stage / 'tools/recompiler-runtime'
    bundle_linux_runtime(private, binaries, env)
    libraries = private / 'usr/lib'
    # Only application-built libraries and their C++ ABI belong on the game's
    # path. Platform libraries must match the host's Vulkan driver/desktop.
    platform_libraries = [path for path in (stage / 'usr/lib').iterdir()
                          if (path.is_file() or path.is_symlink()) and not path.name.startswith(
                              ('librex', 'libTracyClient', 'libc++.so', 'libc++abi.so', 'libunwind.so'))]
    # Copy aliases while their original targets still exist, regardless of the
    # filesystem's directory order. Private SONAME files are self-contained.
    for path in platform_libraries:
        shutil.copy2(path, libraries / path.name)
    for path in platform_libraries:
        path.unlink()

    targets = [(path, '$ORIGIN') for path in libraries.iterdir()]
    targets += [(stage / 'tools/rexglue', '$ORIGIN/recompiler-runtime/usr/lib'),
                (stage / 'usr/bin/rexglue', '$ORIGIN/../../tools/recompiler-runtime/usr/lib')]
    for path, rpath in targets:
        if path.is_symlink() or not path.is_file():
            continue
        with path.open('rb') as stream:
            if stream.read(4) != b'\x7fELF':
                continue
        subprocess.run([str(patchelf), '--force-rpath', '--set-rpath', rpath, str(path)],
                       check=True)


def linux(build, recomp_build, sdk, appimage_tools, test):
    launcher = build / 'DW5E'
    if not launcher.exists():
        launcher = build / 'dw5e_setup'

    recompiler_candidates = [
        recomp_build / 'rexglue',
        recomp_build / 'rexgluerd',
        build / 'rexglue-sdk/src/rexglue/rexglue',
        build / 'rexglue-sdk/src/rexglue/rexgluerd',
        REPO / 'rexglue-sdk/out/linux-amd64/rexgluerd',
        REPO / 'rexglue-sdk/out/linux-amd64/rexglue',
        REPO / 'rexglue-sdk/out/linux-amd64/RelWithDebInfo/rexgluerd',
        REPO / 'tools/rexglue',
    ]
    recompiler = next((p for p in recompiler_candidates if p.exists()), None)

    if recompiler is None:
        fail('required ReXGlue recompiler missing; searched: ' +
             ', '.join(str(p) for p in recompiler_candidates))

    for p in (launcher, sdk / 'bin/clang++', appimage_tools / 'linuxdeploy-x86_64.AppImage', appimage_tools / 'appimagetool-x86_64.AppImage'):
        if not p.exists():
            fail(f'required build artifact or tool missing: {p}')

    version = version_for(test)
    name = f'DW5E-{version}-linux-x86_64'
    dist = REPO / 'dist'
    stage = dist / f'{name}.AppDir'
    shutil.rmtree(stage, ignore_errors=True)
    (stage / 'usr/bin').mkdir(parents=True)
    (stage / 'usr/lib').mkdir(parents=True)
    (stage / 'tools').mkdir(parents=True, exist_ok=True)

    # Copy clean tracked sources
    n = copy_tracked(REPO, ['app', 'patches', 'tools/linux', 'dw5e_manifest.toml', 'LICENSE', 'README.md'], stage,
                     skip=('app/generated/default/',))
    if (REPO / 'rexglue-sdk').exists():
        n += copy_tracked(REPO / 'rexglue-sdk', ['.'], stage / 'rexglue-sdk',
                          skip=('tools/binutils/', 'out/', 'tests/', 'docs/', 'thirdparty/moltenvk/', 'thirdparty/vulkan-loader/tests/'))

    shutil.copy2(launcher, stage / 'DW5E')
    shutil.copy2(launcher, stage / 'usr/bin/dw5e_setup')
    if recompiler and recompiler.exists():
        shutil.copy2(recompiler, stage / 'usr/bin/rexglue')
        (stage / 'usr/bin/rexglue').chmod(0o755)
        shutil.copy2(recompiler, stage / 'tools/rexglue')
        (stage / 'tools/rexglue').chmod(0o755)

    # Copy recompiler shared libraries into usr/lib and tools
    so_candidates = []
    for d in (build, recomp_build, REPO / 'rexglue-sdk/out/linux-amd64', REPO / 'rexglue-sdk/out/linux-amd64/RelWithDebInfo'):
        if d.exists():
            so_candidates.extend(d.glob('**/*.so*'))

    extra_libs = []
    seen = set()
    for so in so_candidates:
        if so.is_file() and not so.is_symlink() and so.name not in seen:
            seen.add(so.name)
            shutil.copy2(so, stage / 'usr/lib' / so.name)
            shutil.copy2(so, stage / 'tools' / so.name)
            extra_libs.extend(['-l', str(so)])

    # Bundle offline SDK
    shutil.copytree(sdk, stage / 'tools/clang', symlinks=True)
    (stage / 'tools/cmake/bin').mkdir(parents=True, exist_ok=True)
    (stage / 'tools/cmake/bin/cmake').symlink_to('../../clang/bin/cmake')
    (stage / 'tools/ninja').symlink_to('clang/bin/ninja')

    for lib in (sdk / 'lib').glob('*.so*'):
        shutil.copy2(lib, stage / 'usr/lib' / lib.name)

    (stage / 'BUILD_ID').write_text(version + '\n')
    (stage / 'README.txt').write_text(README_LINUX.format(build=version))

    lic = stage / 'LICENSES'
    lic.mkdir(parents=True, exist_ok=True)
    shutil.copy2(REPO / 'LICENSE', lic / 'dw5e (GPL-3.0).txt')
    shutil.copy2(REPO / 'rexglue-sdk/LICENSE', lic / 'rexglue-sdk (BSD-3-Clause).txt')

    icon = REPO / 'tools/release/dw5e.svg'
    desktop = REPO / 'tools/release/dw5e.desktop'
    ld_path = f"{stage / 'usr/lib'}:{build / 'rexglue-sdk/src/system'}:{os.environ.get('LD_LIBRARY_PATH', '')}"
    env = {**os.environ, 'APPIMAGE_EXTRACT_AND_RUN': '1', 'ARCH': 'x86_64', 'NO_STRIP': '1', 'LD_LIBRARY_PATH': ld_path}

    linuxdeploy_cmd = [
        str(appimage_tools / 'linuxdeploy-x86_64.AppImage'),
        '--appdir', str(stage),
        '-e', str(stage / 'DW5E'),
        *extra_libs,
        '-d', str(desktop),
        '-i', str(icon)
    ]
    subprocess.run(linuxdeploy_cmd, env=env, check=True)

    # The recompiler must work on a clean host too. linuxdeploy intentionally
    # excludes some X11 desktop libraries that its runtime still imports.
    runtime_inputs = [launcher, recompiler]
    runtime_inputs.extend(so for so in so_candidates if so.is_file())
    runtime_inputs.extend(lib for lib in (sdk / 'lib').glob('*.so*') if lib.is_file())
    # The translator needs desktop imports even in clean, non-graphical CI.
    # Those older imports must not replace the player's host-driver libraries.
    with tempfile.TemporaryDirectory(prefix='dw5e-linuxdeploy-') as directory:
        subprocess.run([str((appimage_tools / 'linuxdeploy-x86_64.AppImage').resolve()),
                        '--appimage-extract', 'usr/bin/patchelf'],
                       cwd=directory, stdout=subprocess.DEVNULL, env=env, check=True)
        patcher = Path(directory) / 'squashfs-root/usr/bin/patchelf'
        isolate_linux_tool_runtime(stage, runtime_inputs, env, patcher)

    app_run = stage / 'AppRun'
    app_run.unlink(missing_ok=True)
    app_run.write_text('''#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH="$root/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$root/DW5E" --root "$root" "$@"
''')
    app_run.chmod(0o755)

    validate_package(stage, windows=False)
    scan(stage, windows=False)
    archive = dist / f'{name}.AppImage'
    image_tool = appimage_tools / 'appimagetool-x86_64.AppImage'

    offset = int(subprocess.check_output([str(image_tool), '--appimage-offset'], text=True).strip())
    runtime = build / 'appimage-runtime-x86_64'
    with image_tool.open('rb') as stream:
        runtime.write_bytes(stream.read(offset))

    subprocess.run([
        str(image_tool),
        '--no-appstream',
        '--runtime-file', str(runtime),
        str(stage),
        str(archive)
    ], env=env, check=True)

    (dist / f'{name}.AppImage.sha256').write_text(f'{sha256(archive)}  {archive.name}\n')
    print(f'Created {archive} ({archive.stat().st_size / 2**20:.1f} MB)')
    return archive


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('target', choices=['windows', 'linux'])
    ap.add_argument('--build', required=True, type=Path)
    ap.add_argument('--recomp-build', type=Path, default=REPO / 'rexglue-sdk/out/bin')
    ap.add_argument('--test', action='store_true')
    ap.add_argument('--linux-sdk', type=Path, default=REPO / 'dist/sdk')
    ap.add_argument('--appimage-tools', type=Path, default=Path.home() / '.local/opt/appimage-tools')
    args = ap.parse_args()

    if args.target == 'windows':
        windows(args.build.resolve(), args.recomp_build.resolve(), args.test)
    else:
        linux(args.build.resolve(), args.recomp_build.resolve(), args.linux_sdk.resolve(),
              args.appimage_tools.resolve(), args.test)


if __name__ == '__main__':
    main()
