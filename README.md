<h1 align="center">Dynasty Warriors 5: Empires Recomp</h1>

<p align="center"><b>Dynasty Warriors 5: Empires (Xbox 360) as a native PC game, built on your PC from your own disc.</b></p>

<p align="center">
  <a href="https://github.com/TheSlackbringer/dw5-empires-recomp/actions/workflows/build.yml"><img alt="build" src="https://img.shields.io/github/actions/workflow/status/TheSlackbringer/dw5-empires-recomp/build.yml?branch=main&label=build&logo=github"></a>
  <img alt="platform: Windows | Linux" src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%2F%20Steam%20Deck-0a7bbb">
  <img alt="graphics: Vulkan" src="https://img.shields.io/badge/graphics-Vulkan-ac162c">
  <a href="LICENSE"><img alt="license: GPL-3.0" src="https://img.shields.io/badge/license-GPL--3.0-6a9a1f"></a>
  <img alt="status: playable" src="https://img.shields.io/badge/status-playable-brightgreen">
  <img alt="disc: PAL 4B4F07D3" src="https://img.shields.io/badge/disc-PAL%204B4F07D3-555">
</p>

A native PC port of **Dynasty Warriors 5: Empires** (Xbox 360 PAL, Title ID `4B4F07D3`) created through static recompilation using **ReXGlue**. The game's PowerPC Xenon machine code is translated ahead-of-time into native C++ and compiled for modern x86-64 PCs. Graphics run natively over **Vulkan** using the `xenos` GPU backend.

### Key Features
- **Native Performance:** 60 FPS gameplay, native resolution scaling, widescreen support, and near-instant load times with zero emulation overhead.
- **Modernized Camera:** Custom dual-stick camera hook supporting analog right-stick orbit (yaw and pitch) around your officer.
- **Audio & Video:** Full BGM, voice lines, sound effects, and WMV movie playback.
- **Zero-Friction Release Builder:** Offline compiler toolchains bundled into the release package (Windows zip and Linux AppImage) so you don't need to install Git, Python, or Visual Studio.

> [!IMPORTANT]
> You must provide your own legally dumped retail disc of **Dynasty Warriors 5: Empires** (Xbox 360 Europe/PAL, Title ID `4B4F07D3`, SHA-1: `82fc63021a0919d8b97584682babf0654997c0a9`).
> No copyrighted game assets, executables, disc images, or game-derived decompiled code are distributed in this repository or its release packages. The game is compiled entirely on your PC from your own copy.

---

## Playing via Release Packages

Release packages are **builders**: they bundle the setup wizard, compiler tools, and source code to build the native game on your machine without external dependencies.

1. Download the latest release from the [Releases](https://github.com/TheSlackbringer/dw5-empires-recomp/releases) page:
   - **Windows:** Download `DW5E-<version>-windows-x64.zip` and extract it to a folder.
   - **Linux / Steam Deck:** Download `DW5E-<version>-linux-x86_64.AppImage` and make it executable (`chmod +x DW5E-*.AppImage`).
2. Run **`DW5E.exe`** or the AppImage.
3. **First launch:** When prompted, select your `dw5e.iso`. The setup wizard verifies your disc, extracts assets, translates Xenon code, and compiles the native executable (~2–4 minutes).
4. **Subsequent launches:** The game boots instantly (<1 second).

Your game data, saves, settings, and logs are stored in:
- **Windows:** `%LOCALAPPDATA%\DW5E`
- **Linux:** `~/.local/share/dw5e` (or `$XDG_DATA_HOME/dw5e`)

---

## Controls

Supports both gamepads (XInput / Xbox controllers recommended) and keyboard input:

| Action | Xbox Gamepad | Keyboard |
| :--- | :--- | :--- |
| **Move** | Left Stick / D-Pad | WASD / Arrow Keys |
| **Camera Orbit (Yaw & Pitch)** | Right Stick | — |
| **Normal Attack** | X | J |
| **Charge Attack** | Y | I |
| **Musou Attack** | B | L |
| **Jump** | A | Space |
| **Guard / Center Camera** | LB | Left Shift |
| **Bow Attack / Strafe** | RB | K |
| **Pause / Map** | Start / Back | Enter / Tab |
| **Save Memory Snapshot (Debug)** | F9 | F9 |

---

## Building from Source

For developers modifying runtime code, hooks, or recompiler profiles.

### Prerequisites
- Linux (x86_64, SSSE3+) or Windows (MSYS2 / llvm-mingw)
- CMake 3.25+
- Clang / Clang++ (LLVM 18+) & Ninja
- Vulkan SDK and compatible GPU drivers
- SDL3 development libraries

### Setup & Build
1. Clone the repository with submodules:
   ```bash
   git clone https://github.com/TheSlackbringer/dw5-empires-recomp.git
   cd dw5-empires-recomp
   ```

2. Fetch and patch the ReXGlue SDK:
   ```bash
   ./tools/setup_sdk.sh
   ```

3. Configure and compile:
   ```bash
   cmake --preset linux-amd64-relwithdebinfo -S app
   cmake --build app/out/build/linux-amd64-relwithdebinfo -j$(nproc)
   ```

4. Run:
   ```bash
   cd app/out/build/linux-amd64-relwithdebinfo
   ./dw5e --gpu_plugin=xenos --game_data_root=/path/to/extracted/game/files
   ```

---

## Credits & Acknowledgements

- **[ReXGlue](https://github.com/rexglue)** — Static recompilation framework and runtime SDK for Xbox 360 Xenon.
- **[Xenia Project](https://github.com/xenia-project/xenia)** & contributors — Research and reference implementations for Xbox 360 emulation (Xenos Vulkan GPU backend, XMA audio decoding, kernel dispatch).
- **[recomp.fyi](https://recomp.fyi)** & the Recompilation Community — Pioneering static recompilation across consoles.
- **Koei Tecmo & Omega Force** — Original developers and creators of the *Dynasty Warriors* series.
- **Microsoft** — Xbox 360 platform and original hardware.

---

## License

This project's code and tools are licensed under the [GNU General Public License v3.0](LICENSE).

*Dynasty Warriors is a registered trademark of KOEI TECMO GAMES CO., LTD. Xbox 360 is a registered trademark of Microsoft Corporation. This fan project is not affiliated with, endorsed by, or sponsored by KOEI TECMO or Microsoft.*
