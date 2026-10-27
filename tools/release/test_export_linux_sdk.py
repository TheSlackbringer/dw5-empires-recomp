"""Synthetic host filesystem tests for the offline Linux SDK exporter."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import export_linux_sdk as sdk


class LinuxSdkTest(unittest.TestCase):
    def test_export_includes_c_headers_startup_objects_and_link_libraries(self):
        with tempfile.TemporaryDirectory() as directory:
            scratch = Path(directory)
            host, output = scratch / "host", scratch / "sdk"
            programs = host / "usr/lib/llvm-23/bin"
            programs.mkdir(parents=True)
            for name in ("clang", "lld", "ld.lld", "llvm-ar", "cmake", "ninja", "pkg-config", "wayland-scanner"):
                (programs / name).write_text("synthetic tool")
            files = {
                "usr/include/stdio.h": "synthetic stdio header",
                "usr/lib/x86_64-linux-gnu/crt1.o": "synthetic startup object",
                "usr/lib/x86_64-linux-gnu/libc.so": "synthetic linker script",
                "lib/x86_64-linux-gnu/libc.so.6": "synthetic C runtime",
                "lib64/ld-linux-x86-64.so.2": "synthetic loader",
                "usr/lib/gcc/x86_64-linux-gnu/10/crtbegin.o": "synthetic GCC startup",
                "usr/lib/llvm-23/lib/clang/23/include/stdarg.h": "synthetic resource header",
                "usr/share/cmake-3.25/Modules/CMake.cmake": "synthetic CMake module",
            }
            for name, contents in files.items():
                target = host / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(contents)

            def host_path(value):
                value = str(value)
                return (host / value.lstrip("/") if value == "/" or
                        value.startswith(("/usr/", "/lib/", "/lib64/")) else Path(value))

            def which(value):
                name = Path(value).name
                for suffix in ("-23", "-22", "-21", "-20", "-19", "-18"):
                    if name.endswith(suffix):
                        name = name[:-len(suffix)]
                target = programs / name
                return str(target) if target.is_file() else None

            with patch.object(sdk, "Path", side_effect=host_path), \
                 patch.object(sdk.shutil, "which", side_effect=which), \
                 patch.object(sdk.subprocess, "check_output", return_value=""):
                sdk.export_sdk(output)
            for name in ("usr/include/stdio.h", "usr/lib/x86_64-linux-gnu/crt1.o",
                         "usr/lib/x86_64-linux-gnu/libc.so", "lib/x86_64-linux-gnu/libc.so.6",
                         "lib64/ld-linux-x86-64.so.2"):
                self.assertEqual((output / "sysroot" / name).read_text(), files[name])


if __name__ == "__main__":
    unittest.main()
