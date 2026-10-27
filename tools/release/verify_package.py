#!/usr/bin/env python3
"""Check an offline builder's contents and run its tools without game data."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def validate_linux_driver_runtime(root):
    platform_prefixes = ('libwayland-', 'libxcb', 'libX', 'libdrm', 'libvulkan',
                         'libGL', 'libEGL', 'libgbm', 'libstdc++', 'libgcc_s', 'libLLVM')
    conflicts = [str(path.relative_to(root)) for path in (root / 'usr/lib').rglob('*')
                 if '.so' in path.name and path.name.startswith(platform_prefixes)]
    if conflicts:
        raise SystemExit('bundled libraries would override host graphics-driver dependencies:\n  ' +
                         '\n  '.join(sorted(conflicts)))


def validate_package(root, windows):
    root = Path(root)
    required = [
        "BUILD_ID", "app/CMakeLists.txt", "app/dw5e_manifest.toml",
        "rexglue-sdk/CMakeLists.txt",
    ]
    if windows:
        required += [
            "DW5E.exe", "tools/rexglue.exe", "tools/cmake/bin/cmake.exe",
            "tools/ninja.exe", "tools/windows/llvm-mingw.cmake",
            "tools/llvm-mingw/bin/x86_64-w64-mingw32-clang.exe",
            "tools/llvm-mingw/bin/x86_64-w64-mingw32-clang++.exe",
            "tools/llvm-mingw/bin/x86_64-w64-mingw32-windres.exe",
            "tools/llvm-mingw/bin/llvm-ar.exe", "tools/llvm-mingw/bin/llvm-ranlib.exe",
        ]
    else:
        required += [
            "DW5E", "usr/bin/rexglue", "tools/rexglue",
            "tools/cmake/bin/cmake", "tools/ninja", "tools/linux/clang.cmake",
            "tools/clang/bin/clang", "tools/clang/bin/clang++",
            "tools/clang/bin/llvm-ar", "tools/clang/bin/llvm-ranlib",
            "tools/clang/bin/ld.lld", "tools/clang/bin/pkg-config",
            "tools/clang/bin/wayland-scanner", "tools/clang/bin/python3",
            "tools/clang/libexec/python3", "tools/clang/sysroot/usr/include/stdlib.h",
            "tools/clang/sysroot/usr/lib/x86_64-linux-gnu/crt1.o",
            "tools/clang/sysroot/usr/lib/x86_64-linux-gnu/dbus-1.0/include/dbus/dbus-arch-deps.h",
            "tools/clang/sysroot/usr/lib/x86_64-linux-gnu/glib-2.0/include/glibconfig.h",
            "tools/clang/sysroot/lib64/ld-linux-x86-64.so.2",
        ]
    missing = [name for name in required if not (root / name).is_file()]
    if not windows and not any((root / "tools/clang/lib").glob("python3.*/os.py")):
        missing.append("tools/clang/lib/python3.*/os.py")
    if missing:
        raise SystemExit("incomplete builder package; required files missing:\n  " +
                         "\n  ".join(missing))
    if not windows:
        validate_linux_driver_runtime(root)


def package_environment(root, windows):
    tools = root / "tools"
    env = os.environ.copy()
    # The release compiler must not inherit target headers/libraries from the host.
    for variable in ("CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "LIBRARY_PATH",
                     "PKG_CONFIG_PATH", "CMAKE_PREFIX_PATH", "PYTHONPATH", "PYTHONHOME"):
        env.pop(variable, None)
    compiler_root = tools / ("llvm-mingw" if windows else "clang")
    if windows:
        system = Path(os.environ["SystemRoot"])
        env["PATH"] = os.pathsep.join(map(str, [
            compiler_root / "bin", tools, system / "System32", system,
        ]))
    else:
        env["PATH"] = f"{compiler_root}/bin:/usr/bin:/bin"
        env["LD_LIBRARY_PATH"] = ":".join(map(str, [
            tools, root / "usr/lib",
        ]))
    return env


def smoke_package(root, windows):
    if windows != (os.name == "nt"):
        raise SystemExit("native tool smoke checks must run on the target operating system")
    root = Path(root).resolve()
    tools = root / "tools"
    env = package_environment(root, windows)
    compiler_root = tools / ("llvm-mingw" if windows else "clang")
    suffix = ".exe" if windows else ""
    cmake = tools / f"cmake/bin/cmake{suffix}"

    def run(command, cwd=root):
        print("+", " ".join(map(str, command)), flush=True)
        subprocess.run(list(map(str, command)), cwd=cwd, env=env, check=True, timeout=300)

    run([tools / f"rexglue{suffix}", "--help"])
    run([cmake, "--version"])
    run([tools / f"ninja{suffix}", "--version"])
    # Configure through the actual packaged toolchain, exercising its compiler,
    # headers, linker, CRT and runtime dependencies without any retail game files.
    with tempfile.TemporaryDirectory(prefix="dw5e-package-smoke-") as directory:
        scratch = Path(directory)
        source, build = scratch / "source", scratch / "build"
        source.mkdir()
        (source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.25)\n"
            "project(package_smoke LANGUAGES C CXX)\n"
            "set(CMAKE_CXX_STANDARD 23)\n"
            "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
            "file(GLOB smoke_sources CONFIGURE_DEPENDS main.cpp)\n"
            "add_executable(package_smoke main.cpp)\n", encoding="utf-8")
        (source / "main.cpp").write_text(
            '#include <filesystem>\n#include <iostream>\n#include <vector>\n'
            'int main() { std::vector<int> v{1,2,3}; '
            'std::cout << "DW5E package tools OK " << v.size() << "\\n"; '
            'return !std::filesystem::exists("."); }\n', encoding="utf-8")
        toolchain = tools / ("windows/llvm-mingw.cmake" if windows else "linux/clang.cmake")
        root_arg = "LLVM_MINGW_ROOT" if windows else "DW5E_LINUX_TOOLCHAIN_ROOT"
        run([cmake, "-S", source, "-B", build, "-G", "Ninja",
             f"-DCMAKE_MAKE_PROGRAM={tools / ('ninja' + suffix)}",
             f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
             f"-D{root_arg}={compiler_root}", "-DCMAKE_BUILD_TYPE=Release"])
        run([cmake, "--build", build, "--target", "package_smoke"])
        run([build / ("package_smoke" + suffix)], cwd=scratch)
    print("PASS: packaged translator and offline build tools", flush=True)


def build_linux_sdk(root, build=None):
    """Rebuild the packaged SDK with its own exported compiler and sysroot."""
    if os.name == "nt":
        raise SystemExit("the Linux SDK rebuild check requires a Linux runner")
    root = Path(root).resolve()
    compiler = root / "tools/clang"
    env = package_environment(root, False)
    # Match a player launching DW5E without development tools on PATH. The
    # toolchain itself must select its scanner, pkg-config and Python.
    env["PATH"] = "/usr/bin:/bin"
    build = Path(build or tempfile.mkdtemp(prefix="dw5e-sdk-build-")).resolve()
    if build.exists() and any(build.iterdir()):
        raise SystemExit(f"SDK verification requires a fresh build directory: {build}")
    build.mkdir(parents=True, exist_ok=True)
    cmake = root / "tools/cmake/bin/cmake"

    def run(command, run_env=env, cwd=build):
        print("+", " ".join(map(str, command)), flush=True)
        subprocess.run(list(map(str, command)), cwd=cwd, env=run_env,
                       check=True, timeout=1800)

    print(f"Offline SDK rebuild evidence: {build}", flush=True)
    run([cmake, "-S", root / "app", "-B", build, "-G", "Ninja",
         f"-DCMAKE_MAKE_PROGRAM={root / 'tools/ninja'}",
         f"-DCMAKE_TOOLCHAIN_FILE={root / 'tools/linux/clang.cmake'}",
         f"-DDW5E_LINUX_TOOLCHAIN_ROOT={compiler}",
         f"-DREXSDK_DIR={root / 'rexglue-sdk'}", "-DDW5E_BUILD_GAME=OFF",
         "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
         "-DFETCHCONTENT_FULLY_DISCONNECTED=ON",
         "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
         "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF"])
    tests = ["dw5e_setup_test", "dw5e_setup_reliability_test",
             "dw5e_safe_read_test", "dw5e_kernel_export_test", "dw5e_gpu_plugin_test",
             "dw5e_keyboard_test"]
    run([cmake, "--build", build, "--target", "dw5e_setup", "dw5e_host_portability",
         "rexglue", "dw5e_vulkan_startup_test", *tests, "--parallel", str(os.cpu_count() or 1)])
    # Prefer the newly built runtime over the prebuilt AppImage runtime.
    run_env = env.copy()
    run_env["LD_LIBRARY_PATH"] = ":".join(map(str, [
        build, build / "rexglue-sdk/src/system", build / "rexglue-sdk/thirdparty",
        root / "tools/recompiler-runtime/usr/lib",
        env["LD_LIBRARY_PATH"],
    ]))
    for test in tests:
        run([build / test], run_env=run_env)
    print("PASS: actual packaged Linux SDK rebuild and native tests", flush=True)
    # Keep the build tree/logs; do not remove build directories containing _deps.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--platform", choices=("windows", "linux"), required=True)
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--build-sdk", action="store_true",
                        help="rebuild the actual Linux SDK using packaged tools, without game data")
    parser.add_argument("--sdk-build-dir", type=Path,
                        help="fresh directory for retained SDK rebuild evidence")
    args = parser.parse_args()
    windows = args.platform == "windows"
    validate_package(args.root, windows)
    print("PASS: required builder package files", flush=True)
    if args.smoke:
        smoke_package(args.root, windows)
    if args.build_sdk:
        if windows:
            raise SystemExit("--build-sdk currently verifies Linux packages")
        build_linux_sdk(args.root, args.sdk_build_dir)


if __name__ == "__main__":
    main()
