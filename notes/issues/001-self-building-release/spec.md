# 001 — Windows & Linux Self-Building Release Packages (AppImage + Windows Zip with Bundled Compiler Toolchain and Native Setup Wizard)

**Status:** in progress
**Spec:** approved by maintainer (2026-10-26)
**Source Specs & Commit References:**
- Spec 011 (v5: self-building Windows ZIP + Linux AppImage releases with bundled offline compiler SDK)
- `dbziw-port/notes/issues/006-native-first-launch-setup/spec.md` (native first-launch setup pipeline)
- `dbziw-port/notes/issues/007-linux-appimage-packaging/spec.md` (offline SDK, AppImage packaging & CI pipeline)
- Spec 041 (pinned GHCR Linux builder container)
**Native vs emulation:** native — 100% native C++ disc verification, XDVDFS extraction, offline C++ compilation on player PC, ImGui+SDL3 setup wizard, and native game execution
**Area:** release, distribution, packaging, setup wizard (`app/setup/`), Windows & Linux / Steam Deck releases, GitHub Actions CI
**Reported:** 2026-10-26, by maintainer: "Make an AppImage and Windows compiler release like top recomps with bundled toolchains and setup wizard."

---

## 1. Catalog of Exact Reference Commits (from Spec 011 & DBZIW Spec 007)

The self-building release architecture was developed and battle-tested across top recomp projects. The exact reference commits below form our implementation blueprint:

1. `f387b33` — **Build ID from a `BUILD_ID` file when there is no Git:**
   - In a release package running on a player's machine, `.git` is absent. Reads the version/hash from `BUILD_ID` file at the root of the package.
2. `258fc59` — **Build: generated code location configurable, Windows runtime DLLs next to programs:**
   - Introduced `DW5E_GENERATED_DIR` CMake cache variable pointing to the player data directory instead of the read-only package folder. Ensures runtime DLLs (`libc++.dll`, `libunwind.dll`, SDL3, Vulkan loaders) are placed adjacent to executables.
3. `66e3752` — **Setup pipeline: package and player data folder apart; rebuild without the disc image:**
   - Enforces clean separation between read-only package folder and writable player data directory (`%LOCALAPPDATA%\DW5E` on Windows, `~/.local/share/dw5e` on Linux). Once the disc is extracted on first run, subsequent updates/rebuilds reuse extracted game files without asking the player for the ISO again. Includes fast directory synchronization (`syncDirectory`).
4. `ab6e1d6` — **Release launcher: builds the game on the player's PC, then starts it:**
   - Unified entry point: if the game binary is already built for the current `BUILD_ID` and the game assets are installed, launches the game immediately (<1 second); otherwise opens the wizard GUI to extract, codegen, compile, and then boot the game. Supports `--no-window` and `--rebuild`.
5. `ea25c97` — **Release script: the builder package, never the compiled game:**
   - `make_release.py` enforces clean-room distribution: packs launcher, compiler SDK, recompiler CLI, and sources. Includes a strict scanner (`scan()`) that inspects all staged files and aborts if any game assets (`.iso`, `.xex`, `.pe`), generated C++, or compiled game binaries (via `GAME_MARKER` binary pattern check) are detected.
6. `52e0735` — **Setup: one progress bar for the whole run:**
   - Single weighted progress bar tracking the entire setup pipeline (Extraction ~15%, ReXGlue Codegen ~15%, CMake/Ninja compilation ~70%) with smooth progression, current step status text, and realistic time estimates.
7. `b350108` — **Setup: a newer release in another folder resets the old build configuration:**
   - `resetBuildIfMoved`: when a newer release is unpacked in a different directory, detects path mismatch in `CMakeCache.txt` and resets `CMakeCache.txt` and `CMakeFiles/` while preserving existing object files, player saves, and settings.
8. `e72bf49` — **Windows build: copy runtime DLLs once, not after every link:**
   - Fixes parallel Ninja link race conditions where multiple build targets simultaneously attempt `cmake -E copy_if_different` on shared DLLs, causing Windows file-lock errors. Dedicated `dw5e_runtime_dlls` target.
9. `553f229` — **Package offline Linux builder AppImages in CI:**
   - Bundles native setup launcher, ReXGlue recompiler, relocatable Steam Runtime compiler SDK (`tools/release/export_linux_sdk.py`), and sources in a self-contained AppImage.
   - **AppImage FUSE Mount Fix:** Copies/syncs builder inputs to stable `<player-data>/builder` directory via `prepareLinuxBuildRoot` so random AppImage mount points (`/tmp/.mount_DW5Exxxxxx`) do not invalidate `CMakeCache.txt` paths or force full recompiles.
10. `43aadd4` & `67a6dba` — **041: pinned builder image on GHCR:**
    - Pins the Linux builder container image (`ghcr.io/...`) in CI (`.github/workflows/builder-image.yml`), completely insulating release builds from third-party apt/LLVM repository outages.

---

## 2. Background & Evidence from the 5 Big Xbox 360 Recomps on recomp.fyi

An audit of the top 5 Xbox 360 recompilation projects on [recomp.fyi](https://recomp.fyi/) demonstrates current ecosystem trade-offs:

| Project | Approach | Player Friction | Legal / Copyright Risk |
|---|---|---|---|
| **Sonic Unleashed** (`hedge-dev/UnleashedRecomp`, ~5.2k stars) | Ships precompiled game library (`UnleashedRecompLib.dll`) + native C++ installer (`installer.cpp`) | Zero friction (extracts retail disc assets into precompiled engine) | **High risk**: redistributes translated proprietary game machine code; vulnerable to publisher takedowns |
| **Gears of War 3** (`Carolux97/Gears3-Azura`) | Source repository with shell build script (`scripts/build.sh`), ReXGlue submodule, and `versions.toml` | High friction (requires player to install Git, Python, Visual Studio, CMake, and compile manually) | Clean (no game binaries), but inaccessible to non-technical players |
| **Skate 3** (`chasmlol/skate3-recomp`) | Windows release zip with `Setup Skate 3 Recomp.cmd` script and manual `Skate 3 Files` drop folder | Medium-High friction (relies on external extraction scripts, CMD batch files, and manual file placement) | Clean, but fragile user experience prone to script path failures |
| **Forza Horizon 2** (`Gabrick75/fh2-recompiled`) | Source distribution with vendored ReXGlue and platform build scripts (`build_pc.sh`, `build_android.sh`) | High friction (requires developer toolchains) | Clean, but developer-focused only |
| **Guitar Hero: Warriors of Rock** (`ronniecloud/re-wor`) | Distributes precompiled executable; requires external `extract-xiso` tool to dump assets into `assets/` | Medium friction (manual command-line ISO extraction required) | High risk (precompiled game executable distributed) |

### The "Builder Release" Advantage for DW5 Empires
The DW5/DBZIW architecture combines the **complete legal safety** of source distribution with the **zero-friction experience** of precompiled releases:
- **Zero copyrighted material distributed:** No disc files, no `.iso`, no `.xex`, no `.pe`, no lifted C++, and no compiled game binaries.
- **Zero player system dependencies:** Freely licensed compiler toolchain (`llvm-mingw` on Windows, relocatable Clang SDK on Linux) is bundled directly in the release download. The player never installs Python, Git, CMake, or Visual Studio.
- **Native ImGui + SDL3 Setup Wizard:** Player double-clicks `DW5E.exe` (Windows) or `DW5E-linux-x86_64.AppImage` (Linux/Steam Deck), selects their retail `dw5e.iso` once, and watches a single unified progress bar.
- **Fast Path:** Subsequent launches boot `<player-data>/bin/dw5e` immediately in <1 second.

---

## 3. Xbox 360 / ReXGlue Specific Architecture vs PS2

While the high-level builder flow mirrors DW5 PS2, the underlying game engine and disc filesystem differ:

1. **Disc Filesystem (XGD2 / XDVDFS vs ISO9660):**
   - PS2 games use standard ISO9660 / UDF sectors.
   - Xbox 360 retail discs use XGD2 (Xbox Game Disc 2). The game partition uses the XDVDFS filesystem, which starts at sector offset `32,448` (byte offset `0xFD90000` / `265,879,552` bytes).
   - We implement a native C++ XDVDFS parser in `app/setup/xdvdfs.cpp` (ported from the validated `tools/xdvdfs.py`) to verify the ISO SHA-1 checksum (`82fc63021a0919d8b97584682babf0654997c0a9`), read directory tables, and extract `default.xex` and game media directly into `<player-data>/game/files`.
2. **Recompiler Engine (ReXGlue vs PS2Recomp):**
   - Input: Extracted `default.pe` (or uncompressed `default.xex`) in `<player-data>/game/`.
   - Tool: ReXGlue CLI (`rexglue codegen dw5e_manifest.toml`).
   - Output: C++ translation units in `<player-data>/generated/default/`.
3. **Runtime & Graphics (rex::runtime + Vulkan xenos vs ps2xRuntime + paraLLEl-GS):**
   - Game runtime links against `rex::runtime` (Xenon kernel, threads, memory, audio) and Vulkan `xenos` GPU plugin.
   - Setup wizard uses ImGui + SDL3 (already integrated into ReXGlue SDK).
4. **Build System (C++23 & Win32 SEH):**
   - ReXGlue targets C++23. Under Windows/MinGW, Clang requires `-fasync-exceptions` for guest SEH (`__try`/`__except`).
   - Windows build uses `llvm-mingw` Clang 19/20 + CMake + Ninja from `~/.local/opt/windows-builder-tools`.
   - Linux AppImage uses relocatable Clang SDK + CMake + Ninja from `~/.local/opt/appimage-tools`.

---

## 4. Technical Design & Implementation Plan

### Component 1: Native Setup Wizard & Launcher (`app/setup/`)
- **`setup_layout.h` / `setup_layout.cpp`**:
  - Defines directory structure:
    - Package Root (`root`): Read-only directory containing launcher, tools, sources, manifest.
    - Player Data Directory (`data`): `%LOCALAPPDATA%\DW5E` (Windows) or `~/.local/share/dw5e` (Linux). Overridable via `DW5E_DATA_DIR`.
    - Builder Root (`data/builder`): Staging directory on Linux populated via `prepareLinuxBuildRoot` to stabilize CMake paths across AppImage FUSE mounts (`553f229`).
    - Build Output: `<player-data>/bin/dw5e[.exe]`, `<player-data>/bin/built-from.txt`.
- **`xdvdfs.h` / `xdvdfs.cpp`**:
  - 100% native C++ XGD2 / XDVDFS reader.
  - Verifies retail disc SHA-1: `82fc63021a0919d8b97584682babf0654997c0a9`.
  - Parses XDVDFS directory tables at offset `0xFD90000`.
  - Extracts required game files (`default.xex`, game media) to `<player-data>/game/files`.
- **`codegen.h` / `codegen.cpp`**:
  - Wrapper executing ReXGlue CLI: `rexglue codegen <manifest>`.
  - Configures output path to `<player-data>/generated/default`.
- **`build_game.h` / `build_game.cpp`**:
  - Invokes bundled CMake and Ninja to build the native game binary.
  - Parses Ninja stdout (`[X/Y] ...`) to provide real-time progress percentages.
  - Implements `resetBuildIfMoved` (`b350108`) to reset CMake cache if source path has changed.
  - Configures `dw5e_runtime_dlls` dedicated target (`e72bf49`).
- **`wizard_ui.h` / `wizard_ui.cpp` & `wizard_main.cpp`**:
  - ImGui + SDL3 interface matching ReXGlue styling.
  - File picker & drag-and-drop support for `dw5e.iso`.
  - Single weighted progress bar (`52e0735`):
    - XDVDFS Extraction: 15%
    - ReXGlue Codegen: 15%
    - Native Compilation: 70%
  - Fast path: If `built-from.txt == BUILD_ID` and binary exists, starts game immediately (<1 sec).
  - CLI arguments: `--install --disc <path> [--rebuild] [--root <dir>] --no-window`.

### Component 2: Windows Builder Release Package (`tools/release/make_release.py windows`)
- Produces `DW5E-<version>-windows-x64.zip`.
- Staging directory structure:
  ```text
  DW5E/
  ├── DW5E.exe                     (Setup Wizard & Launcher)
  ├── BUILD_ID                     (Source revision / tag)
  ├── README.txt                   (Player guide: instructions, keys, run anyway note)
  ├── LICENSES/                    (GPL-3.0, Apache-2.0, MIT, Zlib, etc.)
  ├── dw5e_manifest.toml           (ReXGlue recompiler manifest)
  ├── app/                         (Clean CMake build files and C++ sources)
  ├── rexglue-sdk/                 (Clean patched ReXGlue SDK source tree)
  └── tools/
      ├── rexglue.exe              (ReXGlue CLI recompiler)
      ├── ninja.exe                (Bundled Ninja)
      ├── cmake/                   (Bundled CMake)
      └── llvm-mingw/              (Bundled Clang/LLVM toolchain)
  ```
- **Rigorous Clean-Room Scanner (`scan()` from `ea25c97`):**
  - Traverses staging folder before archiving.
  - Rejects any forbidden file names (`default.xex`, `default.pe`, disc assets).
  - Rejects file extensions `.iso`, `.xex`, `.pe`, `.ghidra`, `.pdb`.
  - Rejects recompiler outputs (`generated/default/`, `fun_*.cpp`).
  - Scans executable binaries for `GAME_MARKER` byte string to guarantee no compiled game binary is packaged.

### Component 3: Linux Builder AppImage (`tools/release/build_linux.sh` & `export_linux_sdk.py`)
- Produces `DW5E-<version>-linux-x86_64.AppImage`.
- Self-contained SquashFS layout:
  ```text
  AppDir/
  ├── AppRun                       (Entrypoint script invoking DW5E launcher)
  ├── dw5e.desktop                 (XDG desktop file)
  ├── dw5e.png                     (Application icon)
  ├── DW5E                         (Setup Wizard & Launcher native binary)
  ├── BUILD_ID                     (Source revision / tag)
  ├── README.txt                   (Player guide)
  ├── LICENSES/                    (License files)
  ├── dw5e_manifest.toml           (Recompiler manifest)
  ├── app/                         (C++ sources and build files)
  ├── rexglue-sdk/                 (ReXGlue SDK sources)
  └── usr/
      ├── bin/rexglue              (ReXGlue CLI binary)
      └── sdk/                     (Relocatable Linux compiler SDK from export_linux_sdk.py)
          ├── bin/                 (clang, clang++, cmake, ninja wrappers)
          ├── lib/                 (libc++.a, libc++abi.a, compiler-rt)
          └── sysroot/             (minimal C++ headers, CRT objects)
  ```
- Uses `linuxdeploy` and `appimagetool` from `~/.local/opt/appimage-tools`.
- Built against standard baseline glibc (2.31+) for full compatibility with Ubuntu, Debian, Fedora, Arch, and SteamOS / Steam Deck.

### Component 4: GitHub Actions CI Pipeline (`.github/workflows/build.yml` & `builder-image.yml`)
- Pinned container image hosted on GitHub Container Registry (`ghcr.io/...`) per DW5 041 (`43aadd4`, `67a6dba`).
- Builds on every tag/push to main:
  - Linux job: packages `DW5E-<ver>-linux-x86_64.AppImage`.
  - Windows job: packages `DW5E-<ver>-windows-x64.zip`.
  - Validation job: runs scanner on archives and executes headless `--install` test.
  - Release job: updates numbered release draft with artifacts and SHA-256 checksums (`190ca80`).

---

## 5. Pass Criteria

1. **Clean-Room Verification:**
   - Both Windows ZIP and Linux AppImage contain **ZERO** copyrighted game files, **ZERO** `.iso`/`.xex`/`.pe` files, **ZERO** generated C++ files, and **ZERO** precompiled game binaries. Verified by `make_release.py` scanner.
2. **Zero-Dependency First Start:**
   - A clean Windows PC or Linux / Steam Deck system without Python, Visual Studio, Git, Clang, or CMake can download the package, run the launcher, select `dw5e.iso`, and successfully complete extraction, codegen, compilation, and launch.
3. **Single Weighted Progress Bar:**
   - Wizard GUI displays a single unified progress bar across extraction, codegen, and compilation, with smooth progression and step descriptions.
4. **Instant Subsequent Launches:**
   - Relaunching `DW5E` after initial build boots `<player-data>/bin/dw5e` immediately in <1 second without rebuilding.
5. **Incremental Updates & Save Preservation:**
   - Installing a newer release package into a different folder preserves existing player saves, settings, and logs in `%LOCALAPPDATA%\DW5E` / `~/.local/share/dw5e`.
   - `resetBuildIfMoved` handles moved folders cleanly without re-extracting disc files.
6. **Automated Headless Test:**
   - Running `DW5E --install --disc dw5e.iso --no-window` completes end-to-end with exit code 0.

---

## 6. Maintainer Tests

1. **Linux / Steam Deck Test:**
   - Run `DW5E-<version>-linux-x86_64.AppImage`.
   - Select retail `dw5e.iso` (PAL Europe).
   - Observe single progress bar progression (~2–4 minutes depending on CPU).
   - Confirm game boots into main menu and plays smoothly.
   - Close game and re-run AppImage; verify instant boot (<1 sec).
2. **Windows Test:**
   - Unzip `DW5E-<version>-windows-x64.zip` on Windows 10/11.
   - Double-click `DW5E.exe` (select "More info" -> "Run anyway" if SmartScreen prompts).
   - Select `dw5e.iso`, verify build completion and boot into main menu.
   - Restart `DW5E.exe` and confirm instant boot.
3. **Save Preservation Check:**
   - Create a save game / campaign in Empires mode.
   - Simulate an update by running a test release with bumped `BUILD_ID`.
   - Verify save file remains intact and loads properly.

---

## 7. Out of Scope

- Emulation or runtime shader changes (tracked in separate runtime issues).
- macOS / ARM64 packaging (planned for a future milestone).
- Automatic in-game updater (player downloads releases from GitHub Releases).
