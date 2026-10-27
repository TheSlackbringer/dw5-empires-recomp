#!/usr/bin/env python3
"""Check an offline builder's contents and run its tools without game data."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


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
            "tools/clang/bin/wayland-scanner", "tools/clang/sysroot/usr/include/stdlib.h",
            "tools/clang/sysroot/usr/lib/x86_64-linux-gnu/crt1.o",
            "tools/clang/sysroot/lib64/ld-linux-x86-64.so.2",
        ]
    missing = [name for name in required if not (root / name).is_file()]
    if missing:
        raise SystemExit("incomplete builder package; required files missing:\n  " +
                         "\n  ".join(missing))


def smoke_package(root, windows):
    if windows != (os.name == "nt"):
        raise SystemExit("native tool smoke checks must run on the target operating system")
    root = Path(root).resolve()
    tools = root / "tools"
    env = os.environ.copy()
    compiler_root = tools / ("llvm-mingw" if windows else "clang")
    if windows:
        system = Path(os.environ["SystemRoot"])
        env["PATH"] = os.pathsep.join(map(str, [
            compiler_root / "bin", tools, system / "System32", system,
        ]))
    else:
        env["PATH"] = f"{compiler_root}/bin:/usr/bin:/bin"
        env["LD_LIBRARY_PATH"] = ":".join(map(str, [
            tools, root / "usr/lib", compiler_root / "host-libs", compiler_root / "lib",
        ]))
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--platform", choices=("windows", "linux"), required=True)
    parser.add_argument("--smoke", action="store_true")
    args = parser.parse_args()
    windows = args.platform == "windows"
    validate_package(args.root, windows)
    print("PASS: required builder package files", flush=True)
    if args.smoke:
        smoke_package(args.root, windows)


if __name__ == "__main__":
    main()
