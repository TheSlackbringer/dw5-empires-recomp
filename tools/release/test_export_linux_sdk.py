"""Synthetic host filesystem tests for the offline Linux SDK exporter."""
from contextlib import contextmanager
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import export_linux_sdk as sdk
import verify_package


@contextmanager
def exported_sdk(extra_files=None, linked=None):
    with tempfile.TemporaryDirectory() as directory:
        scratch = Path(directory)
        host, output = scratch / "host", scratch / "sdk"
        programs = host / "usr/lib/llvm-23/bin"
        programs.mkdir(parents=True)
        for name in ("clang", "lld", "ld.lld", "llvm-ar", "cmake", "ninja",
                     "pkg-config", "wayland-scanner", "python3"):
            (programs / name).write_text("synthetic tool")
        files = {
            "usr/include/stdio.h": "synthetic stdio header",
            "usr/lib/python3.9/os.py": "synthetic Python standard library",
            "usr/lib/x86_64-linux-gnu/crt1.o": "synthetic startup object",
            "usr/lib/x86_64-linux-gnu/libc.so": "synthetic linker script",
            "lib/x86_64-linux-gnu/libc.so.6": "synthetic C runtime",
            "lib64/ld-linux-x86-64.so.2": "synthetic loader",
            "usr/lib/gcc/x86_64-linux-gnu/10/crtbegin.o": "synthetic GCC startup",
            "usr/lib/llvm-23/lib/clang/23/include/stdarg.h": "synthetic resource header",
            "usr/share/cmake-3.25/Modules/CMake.cmake": "synthetic CMake module",
        }
        files.update(extra_files or {})
        for name, contents in files.items():
            target = host / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(contents if isinstance(contents, bytes) else contents.encode())

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

        def ldd(command, **kwargs):
            if Path(command[0]).name == "python3" and command[1] == "-c":
                return "/usr/lib/python3.9"
            return (linked or {}).get(Path(command[1]).name, "")

        with patch.object(sdk, "Path", side_effect=host_path), \
             patch.object(sdk.shutil, "which", side_effect=which), \
             patch.object(sdk.subprocess, "check_output", side_effect=ldd), \
             patch.object(sdk.subprocess, "run") as patch_binary:
            # Fixture ELF bytes are synthetic; record the packaging-tool call.
            # Real ELF relocation and CMake self-exec run in package --smoke.
            sdk.export_sdk(output, patchelf="fixture-patchelf")
        yield output, files


class LinuxSdkTest(unittest.TestCase):
    def test_package_verifier_does_not_export_sdk_host_libraries(self):
        root = Path("/synthetic/release")
        env = verify_package.package_environment(root, False)
        self.assertNotIn(str(root / "tools/clang/host-libs"),
                         env["LD_LIBRARY_PATH"].split(":"))

    @unittest.skipIf(os.name == "nt", "Linux ELF tool execution")
    def test_tool_libraries_do_not_escape_into_host_subprocesses(self):
        with exported_sdk() as (output, _):
            # A real ELF tool that starts a host child. No compiler or retail data.
            shutil.copy2("/bin/sh", output / "libexec/ninja")
            (output / "host-libs").mkdir(exist_ok=True)
            env = os.environ.copy()
            original = str(output.parent / "inherited-libraries")
            env["LD_LIBRARY_PATH"] = original
            child = subprocess.run(
                [str(output / "bin/ninja"), "-c", '/bin/sh -c \'printf "%s" "$LD_LIBRARY_PATH"\''],
                env=env, text=True, capture_output=True, check=True)
            self.assertEqual(child.stdout, original)

    def test_export_includes_c_headers_startup_objects_and_link_libraries(self):
        with exported_sdk() as (output, files):
            for name in ("usr/include/stdio.h", "usr/lib/x86_64-linux-gnu/crt1.o",
                         "usr/lib/x86_64-linux-gnu/libc.so", "lib/x86_64-linux-gnu/libc.so.6",
                         "lib64/ld-linux-x86-64.so.2"):
                self.assertEqual((output / "sysroot" / name).read_text(), files[name])

    def test_export_keeps_architecture_specific_dependency_headers(self):
        headers = {
            "usr/lib/x86_64-linux-gnu/dbus-1.0/include/dbus/dbus-arch-deps.h": "dbus target ABI",
            "usr/lib/x86_64-linux-gnu/glib-2.0/include/glibconfig.h": "glib target ABI",
            "usr/lib/x86_64-linux-gnu/vendor-sdk/include/nested/config.h": "private SDK header",
        }
        with exported_sdk(headers) as (output, files):
            for name, contents in headers.items():
                self.assertEqual((output / "sysroot" / name).read_text(), contents)

    def test_export_keeps_enabled_sdl_dependency_libraries(self):
        names = ("libglib-2.0.so", "libgobject-2.0.so", "libgio-2.0.so",
                 "libpipewire-0.3.so", "libdecor-0.so", "libfribidi.so",
                 "libthai.so", "libusb-1.0.so", "libjack.so", "libsndio.so", "libgbm.so")
        libraries = {"usr/lib/x86_64-linux-gnu/" + name: "SDK library " + name
                     for name in names}
        with exported_sdk(libraries) as (output, files):
            for name, contents in libraries.items():
                self.assertEqual((output / "sysroot" / name).read_text(), contents)

    def test_export_follows_target_library_dependencies(self):
        libraries = {
            "usr/lib/x86_64-linux-gnu/libfreetype.so": b"\x7fELFfreetype",
            "lib/x86_64-linux-gnu/libpng16.so.16": b"\x7fELFpng",
            "lib/x86_64-linux-gnu/libbrotlidec.so.1": b"\x7fELFbrotli",
        }
        linked = {
            "libfreetype.so": "libpng16.so.16 => /lib/x86_64-linux-gnu/libpng16.so.16 (0x1000)",
            "libpng16.so.16": "libbrotlidec.so.1 => /lib/x86_64-linux-gnu/libbrotlidec.so.1 (0x2000)",
        }
        with exported_sdk(libraries, linked) as (output, files):
            for name, contents in libraries.items():
                self.assertEqual((output / "sysroot" / name).read_bytes(), contents)

    def test_export_includes_python_and_its_extension_dependencies(self):
        files = {
            "usr/lib/python3.9/lib-dynload/_ssl.so": b"\x7fELFpython SSL",
            "usr/lib/x86_64-linux-gnu/libssl.so.1.1": "Python SSL dependency",
        }
        linked = {"_ssl.so": "libssl.so.1.1 => /usr/lib/x86_64-linux-gnu/libssl.so.1.1 (0x1000)"}
        with exported_sdk(files, linked) as (output, contents):
            self.assertTrue((output / "libexec/python3").is_file())
            self.assertTrue((output / "bin/python3").is_file())
            self.assertEqual((output / "lib/python3.9/os.py").read_text(),
                             contents["usr/lib/python3.9/os.py"])
            self.assertEqual((output / "host-libs/libssl.so.1.1").read_text(),
                             files["usr/lib/x86_64-linux-gnu/libssl.so.1.1"])

    def test_export_rejects_unresolved_target_library_dependencies(self):
        libraries = {"usr/lib/x86_64-linux-gnu/libfreetype.so": b"\x7fELFfreetype"}
        with self.assertRaisesRegex(SystemExit, "unresolved.*libmissing"):
            with exported_sdk(libraries, {"libfreetype.so": "libmissing.so.1 => not found"}):
                pass


if __name__ == "__main__":
    unittest.main()
