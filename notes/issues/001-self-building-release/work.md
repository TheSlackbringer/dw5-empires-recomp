# 001 — Windows and Linux Self-Building Release (Work Log)

## Stop/Go Milestones

### Milestone 1: Toolchain Audit & Packaging Prerequisites
- [x] Verified host tools in `~/.local/opt`:
  - `windows-builder-tools`: `llvm-mingw-20261006-ucrt-x86_64.zip`, `cmake-4.4.4-windows-x86_64.zip`, `ninja-win.zip`
  - `appimage-tools`: `appimagetool-x86_64.AppImage`, `linuxdeploy-x86_64.AppImage`
- [x] Audited 5 major Xbox 360 recompilations on [recomp.fyi](https://recomp.fyi/) (Sonic Unleashed, Gears of War 3, Skate 3, Forza Horizon 2, Guitar Hero Warriors of Rock).
- [x] Integrated exact commit references and proven patterns from DW5 port Spec 011 and DBZIW port Spec 007 (`f387b33`, `258fc59`, `66e3752`, `ab6e1d6`, `ea25c97`, `52e0735`, `b350108`, `e72bf49`, `553f229`, `43aadd4`/`67a6dba`).
- [x] Comprehensive spec completed in `notes/issues/001-self-building-release/spec.md`.
- [x] Spec approved by maintainer (2026-10-26).

--- **STOP: Await Maintainer Approval Before Proceeding to Implementation** ---

### Milestone 2: Native Setup Wizard & XDVDFS Disc Extraction (`app/setup/`)
- [x] Implement `xdvdfs.h` / `xdvdfs.cpp`: Native C++ XGD2 / XDVDFS reader (sector offset `0xFD90000`) with SHA-1 validation (`82fc63021a0919d8b97584682babf0654997c0a9`) and extraction into `<player-data>/game/files`. Tested on real `game/dw5e.iso` (found all 34 files at `0xFD90000`).
- [x] Implement `setup_layout.h` / `setup_layout.cpp`: `%LOCALAPPDATA%\DW5E` and `~/.local/share/dw5e` layout, `prepareLinuxBuildRoot` staging for AppImage FUSE stability (`553f229`), and `BUILD_ID` reader (`f387b33`).
- [x] Implement `codegen.h` / `codegen.cpp`: ReXGlue CLI codegen process runner with stdout logging and progress tracking.
- [x] Implement `build_game.h` / `build_game.cpp`: CMake/Ninja invoker, `resetBuildIfMoved` cache reset (`b350108`), dedicated DLL copy target (`e72bf49`), and Ninja `[X/Y]` progress parsing.
- [x] Implement `wizard_ui.h` / `wizard_ui.cpp` & `wizard_main.cpp`: ImGui + SDL3 UI with single weighted progress bar (Extraction 15%, Codegen 15%, Compile 70%), file dialog / drag & drop, fast-path instant launch (<1 sec), and headless `--install --disc <iso>` argument.
- [x] Add `dw5e_setup` and `dw5e_setup_test` targets in `app/CMakeLists.txt`. All 5 setup unit tests pass.

### Milestone 3: Windows Package Assembler (`tools/release/make_release.py windows`)
- [x] Implemented `tools/release/make_release.py windows` to assemble `DW5E-<version>-windows-x64.zip`.
- [x] Implemented strict clean-room `scan()`: rejects disc files, ISOs, `.xex`, `.pe`, generated C++, and compiled game binaries via `GAME_MARKER`.
- [x] Successfully packaged `dist/DW5E-f53a735-test-windows-x64.zip` (322.8 MB, 37,829 files, 19,948 clean sources):
  - Bundled `llvm-mingw` UCRT x86-64 toolchain
  - Bundled CMake 4.4.4 & Ninja
  - Bundled ReXGlue recompiler CLI (`tools/rexglue.exe`, 3.3 MB)
  - Bundled launcher (`DW5E.exe`, 4.7 MB)
  - Generated SHA-256 sidecar checksum
- [x] Bundled player `LICENSES/` (GPLv2, BSD-3-Clause, etc.).

### Milestone 4: Linux AppImage Assembler (`tools/release/build_linux.sh`)
- [x] Implemented `tools/release/export_linux_sdk.py` to bundle relocatable Clang SDK inside AppImage with CMake modules.
- [x] Implemented `tools/release/make_release.py linux` and `tools/release/build_linux.sh` to package `DW5E-<version>-linux-x86_64.AppImage` using `linuxdeploy` and `appimagetool`.
- [x] Successfully packaged `dist/DW5E-f53a735-test-linux-x86_64.AppImage` (261.8 MB):
  - Bundled relocatable Clang toolchain, LLD, and Ninja
  - Bundled ReXGlue recompiler CLI (`usr/bin/rexglue` and `tools/rexglue`)
  - Bundled setup wizard launcher (`DW5E` AppRun entrypoint)
  - Verified desktop integration (`dw5e.desktop`) and vector icon (`dw5e.svg`)
  - Generated SHA-256 sidecar checksum

### Milestone 5: Verification & Automated Tests
- [x] Setup unit tests pass 100% (`ctest --test-dir app/out/build/linux-amd64-relwithdebinfo`).
- [x] Headless end-to-end installation test against retail European PAL `game/dw5e.iso` passed:
  - Validated retail disc SHA-1 (`82fc63021a0919d8b97584682babf0654997c0a9`).
  - Extracted all 34 files from XGD2 partition at sector 32448 (`0xFD90000`).
  - Successfully generated C++ sources from Xenon PowerPC via ReXGlue.
  - Successfully compiled all 930 targets using bundled toolchain into native game binary (`dw5e`).
- [x] Fast-path instant launch verified (<0.05 seconds) on subsequent run: skips build and verifies game is ready immediately.
- [x] Clean-room release scan confirmed zero game files, decrypted executables, or copyrighted assets inside both distribution packages.
- [x] Verified user save and configuration data separation in `%LOCALAPPDATA%\DW5E` / `~/.local/share/dw5e`.
