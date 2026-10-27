"""Offline release fixtures. No game data, external tools, or network."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import make_release as release


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

    def test_missing_compiler_stops_packaging(self):
        with self.assertRaisesRegex(SystemExit, "(?i)recompiler|rexglue"):
            self.package()
        self.assertFalse(list((self.repo / "dist").glob("*.zip")))

    def test_missing_bundled_cmake_stops_packaging(self):
        self.compiler("rexglue.exe")
        self.write_zip(self.archives["CMAKE_WIN_ZIP"], {"cmake/readme.txt": b"no tool"})
        with self.assertRaisesRegex(SystemExit, "(?i)cmake"):
            self.package()


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
