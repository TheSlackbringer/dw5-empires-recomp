# DW5 Empires (X360) recomp port: Phase 1 analysis

Started 2026-10-11. Goal: native Windows/Linux build via static recompilation (XenonRecomp / ReXGlue). Game data stays local and is never committed or distributed.

## Source
- `game/dw5e.iso`: Dynasty Warriors 5 - Empires (Europe), XGD2, SHA1 `82fc63021a0919d8b97584682babf0654997c0a9` (user owns the disc).
- `game/default.xex` → `game/default.pe` (decrypted with retail key, basic compression). Tools in `tools/`.

## Executable
- Title ID 4B4F07D3, version 0.0.0.3, PE name `DW5EMP.pe`, built 2006-05-18 (PE timestamp 1147956307).
- Image base 0x82000000, entry 0x8230AB80, image 11.3 MB, stack 0x40000, 64 TLS slots.
- No symbols. PDB path only: `e:\XBox360\DW5Emp360EU\Release_LTCG_DW5\DW5EMP.pdb`.
- `.pdata` 0x17AE8 bytes (~12k function entries) → function boundaries available for the recompiler.
- `.text` 0x3D85FC (3.9 MB) + 7 `.embsec_` code sections (likely statically linked XDK code).

## Statically linked XDK libs (all 2.0.2732.0, early 2006 XDK)
D3DX9, XGRAPHC, XAPILIB, XBOXKRNL, XMEDIA, XONLINE, LIBCPMT, D3D9LTCG, XAUDLTCG, X3DAUDL

## System imports (full list: `imports-named.txt`)
- xboxkrnl: 127 ordinals (117 functions, 10 variables). xam: 30.
- Graphics: 23 `Vd*` (ring buffer, swap, EDRAM) → the game drives the GPU through statically linked D3D9 writing PM4 command buffers.
- Audio: XAudio render-driver client + `XMACreateContext`/`XMAReleaseContext` → hardware XMA decode. This is the path that froze Xenia after the first voice line until the Sep 2026 Canary fix.
- Files: Nt* file APIs + STFS (saves). Input: XamInputGetState/SetState. Threads/sync: Ke*/Nt*/Rtl*.
- Online: only NetDll_WSAStartup (XONLINE linked, practically unused).

## Disc content
- `data/movie/*.wmv` (19 files): WMV via XMEDIA → replace with ffmpeg in a port.
- Koei LINKDATA archives: LINKDATA/LINKDAT2/LINKSE/LINKVOI `.BIN` + `.IDX`, `bgm.bin`, `OPDATA.lnk`.

## Main risks
1. Renderer: replace D3D9 at function level (needs D3D9 functions identified without symbols, XDK 2732 is early) or emulate PM4 + Xenos (Xenia-style, heavier).
2. XMA audio timing (known Koei issue).
3. Tooling support for an early-XDK title (to verify in ReXGlue/XenonRecomp docs).

## Next
Read ReXGlue + XenonRecomp docs: requirements, D3D handling, XDK-version assumptions, runtime coverage of the imports above.

## Tooling check (2026-10-11): GO
Read rexglue-sdk @ c94f5eb (2026-08-21) and XenonRecomp @ ddd128b (2025-08-04).
- ReXGlue = static recompiler + Xenia-derived runtime. Graphics: Xenia's command processor (PM4/Xenos) with Vulkan + D3D12 backends. **No need to identify/replace the game's D3D9**: the statically linked D3D9 keeps writing PM4, ReXGlue renders it. Renderer risk mostly gone.
- Audio: Xenia-derived XMA context/decoder + SDL driver. XMA timing risk remains (Sep 2026 Canary fix for Koei's voice hang may or may not be in ReXGlue yet; check).
- Kernel coverage vs our imports (`import-coverage.txt`): 144/147 functions implemented, 0 stubs, 10 variables referenced in source. Missing: sprintf, _vsnprintf, DbgPrint (trivial).
- Save/restore helpers: ReXGlue auto-detects them (sig_scanner). Our manual search found all 8 uniquely (`save-restore.toml`), so detection should succeed.
- Config needs: xex path, out dir, optional setjmp/longjmp/jump addresses. No XDK-version assumptions found in source.
- Project is early ("expect breaking changes"); not plug and play per community reports.

Next: Phase 2 (build ReXGlue SDK from source, run codegen on default.xex, compile, aim for the main menu).
