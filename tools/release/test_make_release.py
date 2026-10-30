"""Offline release fixtures. No game data, external tools, or network."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import make_release as release
import export_linux_sdk as sdk
import verify_package


class WindowsPackageTest(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.repo = self.root / "repo"
        self.build = self.root / "build"
        self.recomp = self.root / "compiler"
        for directory in (self.repo, self.build, self.recomp):
            directory.mkdir()
        (self.build / "DW5E.exe").write_bytes(b"MZsynthetic setup")
        (self.repo / "LICENSE").write_text("synthetic license")
        (self.repo / "rexglue-sdk").mkdir()
        (self.repo / "rexglue-sdk/LICENSE").write_text("synthetic SDK license")
        self.archives = {}
        contents = {
            "LLVM_MINGW_WIN_ZIP": {
                "llvm/bin/clang++.exe": b"MZsynthetic compiler",
                "llvm/bin/x86_64-w64-mingw32-clang.exe": b"MZsynthetic C compiler",
                "llvm/bin/x86_64-w64-mingw32-clang++.exe": b"MZsynthetic compiler",
                "llvm/bin/x86_64-w64-mingw32-windres.exe": b"MZsynthetic windres",
                "llvm/bin/llvm-ar.exe": b"MZsynthetic ar",
                "llvm/bin/llvm-ranlib.exe": b"MZsynthetic ranlib",
            },
            "CMAKE_WIN_ZIP": {"cmake/bin/cmake.exe": b"MZsynthetic cmake"},
            "NINJA_WIN_ZIP": {"ninja.exe": b"MZsynthetic ninja"},
        }
        for name, entries in contents.items():
            archive = self.root / (name + ".zip")
            self.archives[name] = archive
            self.write_zip(archive, entries)
        for name, value in {"REPO": self.repo, **self.archives}.items():
            p = patch.object(release, name, value)
            p.start()
            self.addCleanup(p.stop)
        for name, value in (("version_for", "fixture"), ("dll_closure", [])):
            p = patch.object(release, name, return_value=value)
            p.start()
            self.addCleanup(p.stop)
        p = patch.object(release, "copy_tracked", side_effect=self.copy_sources)
        p.start()
        self.addCleanup(p.stop)

    @staticmethod
    def write_zip(path, entries):
        with zipfile.ZipFile(path, "w") as archive:
            for name, data in entries.items():
                archive.writestr(name, data)

    @staticmethod
    def copy_sources(repo, paths, stage, skip=()):
        if "app" in paths:
            for name in ("app/CMakeLists.txt", "app/dw5e_manifest.toml",
                         "tools/windows/llvm-mingw.cmake"):
                target = stage / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text("synthetic source")
        if repo.name == "rexglue-sdk":
            stage.mkdir(parents=True, exist_ok=True)
            (stage / "CMakeLists.txt").write_text("synthetic SDK")
        return 3

    def compiler(self, filename):
        path = self.build / "rexglue-sdk/src/rexglue" / filename
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"MZsynthetic translator")
        return path

    def package(self):
        return release.windows(self.build, self.recomp, True)

    def test_relwithdebinfo_compiler_is_bundled_under_canonical_name(self):
        self.compiler("rexgluerd.exe")
        archive = self.package()
        with zipfile.ZipFile(archive) as contents:
            self.assertEqual(contents.read("DW5E-fixture-windows-x64/tools/rexglue.exe"),
                             b"MZsynthetic translator")

    def test_release_compiler_and_checksum_are_preserved(self):
        self.compiler("rexglue.exe")
        archive = self.package()
        checksum = archive.with_suffix(".zip.sha256")
        self.assertEqual(checksum.read_text(),
                         f"{release.sha256(archive)}  {archive.name}\n")

    def test_setup_folder_launcher_wins_over_game_in_combined_build(self):
        # A Windows build with the game puts the setup in setup/; build/DW5E.exe is then the game.
        (self.build / "DW5E.exe").write_bytes(b"MZsynthetic game")
        (self.build / "setup").mkdir()
        (self.build / "setup/DW5E.exe").write_bytes(b"MZsynthetic setup")
        self.compiler("rexglue.exe")
        with zipfile.ZipFile(self.package()) as contents:
            self.assertEqual(contents.read("DW5E-fixture-windows-x64/DW5E.exe"), b"MZsynthetic setup")

    def test_missing_compiler_stops_packaging(self):
        with self.assertRaisesRegex(SystemExit, "(?i)recompiler|rexglue"):
            self.package()
        self.assertFalse(list((self.repo / "dist").glob("*.zip")))

    def test_missing_bundled_cmake_stops_packaging(self):
        self.compiler("rexglue.exe")
        self.write_zip(self.archives["CMAKE_WIN_ZIP"], {"cmake/readme.txt": b"no tool"})
        with self.assertRaisesRegex(SystemExit, "(?i)cmake"):
            self.package()


class LinuxRuntimeTest(unittest.TestCase):
    @unittest.skipIf(os.name == "nt", "Linux packaging symlink layout")
    def test_private_runtime_keeps_platform_library_aliases(self):
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            (stage / "usr/lib").mkdir(parents=True)
            canonical = stage / "usr/lib/libwayland-client.so.0.18"
            canonical.write_text("SDK Wayland")
            alias = stage / "usr/lib/libwayland-client.so.0"
            alias.symlink_to("libwayland-client.so.0.18")
            iterdir = Path.iterdir
            def entries(path):
                return iter((canonical, alias)) if path == stage / "usr/lib" else iterdir(path)
            with patch.object(sdk, "host_dependencies", return_value=set()), \
                 patch.object(Path, "iterdir", entries):
                release.isolate_linux_tool_runtime(stage, [], {}, "unused-patchelf")
            self.assertFalse(alias.is_symlink())
            self.assertEqual((stage / "tools/recompiler-runtime/usr/lib/libwayland-client.so.0")
                             .read_text(), "SDK Wayland")

    def test_actual_package_rejects_platform_libraries_on_game_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "usr/lib").mkdir(parents=True)
            (root / "usr/lib/libwayland-client.so.0").write_text("older SDK Wayland")
            with self.assertRaisesRegex(SystemExit, "host.*driver|driver.*host"):
                verify_package.validate_linux_driver_runtime(root)

    def test_actual_package_needs_png_icon_for_diricon(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "dw5e.svg").write_text("<svg/>")
            with self.assertRaisesRegex(SystemExit, "PNG icon"):
                verify_package.validate_linux_icon(root)
            (root / "dw5e.png").write_bytes(b"\x89PNG\r\n\x1a\n" + b"\0" * 8)
            verify_package.validate_linux_icon(root)

    def test_shipped_linux_icon_is_png(self):
        verify_package.validate_linux_icon(release.REPO / "tools/release")

    def test_recompiler_dependencies_are_private_to_build_tools(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            stage = root / "stage"
            (stage / "usr/lib").mkdir(parents=True)
            libraries = []
            for name in ("libwayland-client.so.0", "libxcb.so.1", "libX11.so.6",
                         "libgcc_s.so.1", "librexruntimerd.so", "libc++.so.1"):
                source = root / name
                source.write_bytes(b"\x7fELFsynthetic " + name.encode())
                libraries.append(source)
                (stage / "usr/lib" / name).write_bytes(source.read_bytes())
            for name in ("tools/rexglue", "usr/bin/rexglue"):
                path = stage / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"\x7fELFsynthetic translator")
            with patch.object(sdk, "host_dependencies", return_value=set(libraries)), \
                 patch.object(release.subprocess, "run"):
                release.isolate_linux_tool_runtime(stage, [root / "rexglue"], {}, "patchelf")
            for source in libraries:
                self.assertTrue((stage / "tools/recompiler-runtime/usr/lib" / source.name).is_file())
            for name in ("libwayland-client.so.0", "libxcb.so.1", "libX11.so.6", "libgcc_s.so.1"):
                self.assertFalse((stage / "usr/lib" / name).exists())
            self.assertTrue((stage / "usr/lib/librexruntimerd.so").is_file())
            self.assertTrue((stage / "usr/lib/libc++.so.1").is_file())

    def test_linux_runtime_closure_includes_libraries_linuxdeploy_omitted(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            x11 = root / "libX11-xcb.so.1"
            xcb = root / "libxcb.so.1"
            libc = root / "libc.so.6"
            for path in (x11, xcb, libc):
                path.write_text("SDK " + path.name)
            stage = root / "stage"
            with patch.object(sdk, "host_dependencies", return_value={x11, xcb, libc}):
                release.bundle_linux_runtime(stage, [root / "rexglue"], {})
            self.assertEqual((stage / "usr/lib/libX11-xcb.so.1").read_text(), x11.read_text())
            self.assertEqual((stage / "usr/lib/libxcb.so.1").read_text(), xcb.read_text())
            self.assertFalse((stage / "usr/lib/libc.so.6").exists())

    def test_linux_runtime_closure_preserves_linuxdeploy_patched_libraries(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "libxcb.so.1"
            source.write_text("original SDK binary")
            stage = root / "stage"
            (stage / "usr/lib").mkdir(parents=True)
            target = stage / "usr/lib/libxcb.so.1"
            target.write_text("linuxdeploy RPATH-patched binary")
            with patch.object(sdk, "host_dependencies", return_value={source}):
                release.bundle_linux_runtime(stage, [root / "rexglue"], {})
            self.assertEqual(target.read_text(), "linuxdeploy RPATH-patched binary")


class DependencyTest(unittest.TestCase):
    def test_dependency_inspection_failure_is_fatal(self):
        failed = subprocess.CompletedProcess([], 1, "", "invalid PE input")
        with patch.object(release.subprocess, "run", return_value=failed):
            with self.assertRaisesRegex(SystemExit, "(?i)inspect|dependenc|readobj"):
                release.dll_closure(Path("fixture.exe"), [])

    def test_unresolved_non_system_dependency_is_fatal(self):
        imports = subprocess.CompletedProcess([], 0, "  Name: rexcodegenrd.dll\n", "")
        with patch.object(release.subprocess, "run", return_value=imports):
            with self.assertRaisesRegex(SystemExit, "rexcodegenrd.dll"):
                release.dll_closure(Path("fixture.exe"), [])

    def test_system_libraries_need_not_be_bundled(self):
        imports = subprocess.CompletedProcess([], 0,
            "  Name: KERNEL32.dll\n  Name: api-ms-win-core-synch-l1-2-0.dll\n", "")
        with patch.object(release.subprocess, "run", return_value=imports):
            self.assertEqual(release.dll_closure(Path("fixture.exe"), []), [])

    def test_git_symlink_files_are_materialized_without_windows_symlink_privilege(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            (source / "real").mkdir(parents=True)
            (source / "real/implementation.c").write_text("int synthetic_function(void) { return 1; }")
            (source / "link.c").write_text("real/implementation.c")

            def git_listing(*args, **kwargs):
                if "--stage" in args:
                    return "120000 " + "0" * 40 + " 0\tlink.c\0"
                return "link.c\0"

            with patch.object(release, "git", side_effect=git_listing):
                release.copy_tracked(source, ["."], root / "stage")
            self.assertEqual((root / "stage/link.c").read_text(),
                             "int synthetic_function(void) { return 1; }")

    def test_non_ascii_tracked_source_names_survive_windows_locale(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            source.mkdir()
            name = chr(0x1f30b) + ".def"
            (source / name).write_text("synthetic Unicode source")
            subprocess.run(["git", "init", "-q", str(source)], check=True, capture_output=True)
            subprocess.run(["git", "-C", str(source), "add", "--", name],
                           check=True, capture_output=True)
            release.copy_tracked(source, ["."], root / "stage")
            self.assertEqual((root / "stage" / name).read_text(), "synthetic Unicode source")

    def test_tracked_source_copy_error_is_fatal(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            source.mkdir()
            (source / "important.cpp").write_text("synthetic")
            with patch.object(release, "git", return_value="important.cpp\0"), \
                 patch.object(release.shutil, "copy2", side_effect=PermissionError("fixture")):
                with self.assertRaises((SystemExit, OSError)):
                    release.copy_tracked(source, ["."], root / "stage")


if __name__ == "__main__":
    unittest.main()
