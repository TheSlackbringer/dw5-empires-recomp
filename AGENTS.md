# AGENTS.md — Dynasty Warriors 5 Empires native port

Static recompilation of **Dynasty Warriors 5: Empires** (Xbox 360, PAL/Europe, Title ID `4B4F07D3`) to a native Linux and
Windows executable using **ReXGlue** (PPC Xenon 3-Core/6-Thread → C++ via static recompilation) and an Xenia-derived
`xenos` GPU plugin (Vulkan PM4 command-processor backend). Goal: full game playable natively at 60 FPS, widescreen,
clean logs, instant loading, right-stick camera (yaw + pitch), WMV movie playback, and zero emulation overhead.
GitHub: TheSlackbringer/dw5-empires-recomp.

Read first: this file → `notes/PHASE1.md` → `notes/PHASE2.md` → `notes/issues/README.md` (local: index, "Waiting for you",
working agreement) → the current issue's `spec.md` and `work.md`. Setup/build commands: "Essentials" in this file.
Look-ups (repo map, camera architecture, Ghidra, pitfalls): "Reference" at the end of this file.

---

## Hard rules (2026-10-19 — from dw5-ps2 lineage)
- **Never change a test's pass criteria** (thresholds, tick windows, run mode) to make it pass. Changing criteria is
  a spec change: the user approves it first. A failing test is reported as failing.
- **No timing workarounds:** no delays, sleeps or knobs tuned to one game routine or one machine. Fix the root cause.
- **Evidence before binding:** never bind, unbind, or override guest functions or SDK stubs without verified evidence of
  the guest contract (registers, memory written, callbacks, state owned) and calling convention.
- **Every claimed result names its log** (`notes/runN.log` or test logs). "Passes" means the run finished cleanly.
- **Only the user approves a spec.** Write "approved" only after the user said so, with the date.
- **Never commit or close an issue without explicit textual approval:** NEVER create a `Fix #00N` commit and NEVER mark
  an issue `confirmed` or `closed` until the user explicitly types "confirmed", "approved", or "close" in chat. Reaching
  `fixed — maintainer test` means STOP and wait for review.
- **Never ask the user to run an automated test:** automated tests are exclusively the agent's job to run, verify, and log.
  The "Maintainer test" section is strictly reserved for things a machine cannot judge (gameplay feel, controller responsiveness,
  visual artifacts, sound timing). If an issue is 100% covered by automated tests, the Maintainer test is explicitly "None (fully automated)".
- **Pushing requires permission:** push only after the user agreed to that push.
- **Changes to `notes/` are tracked locally;** keep findings, logs, and milestone history updated there.
- **Before relying on the game files, verify them:** `game/dw5e.iso` (SHA1 `82fc63021a0919d8b97584682babf0654997c0a9`)
  and extracted `game/files/default.xex` must match the player's legally dumped retail disc (PAL XGD2).

---

## Working agreement (2026-10-16)
1. **One folder per issue**, `notes/issues/NNN-short-name/` (local, never committed):
   - `spec.md` — what the user and you agreed. Fill EVERY section: what the user sees; background (facts with numbers,
     what is ruled out); how to reproduce; **recommended fix** (step by step, why it works, expected result);
     **native vs emulation** (header line plus section: which parts are native code, which stay emulated/stubbed);
     **main risks** (what could go wrong or need a user decision, and what then); alternatives considered; measurable
     pass criteria; automated test; 1–3 "Maintainer test" items requiring human judgement; out of scope. Plain words,
     no unexplained jargon.
   - `work.md` — stop/go milestones (not in the spec), all findings, progress, measurements, conclusion, fix (commit)
     and history, kept current. Findings never live only in chat.
2. **Spec approval before any work.** Draft the spec (fact-finding needed to write it is fine); the user approves.
   Push back with reasons when you think a spec is wrong; the user decides and can overrule. No fixes or experimental
   code changes without an approved spec. While away, the user can allow work on an unapproved spec — explicitly.
3. **A test per issue, written first:** automated test scripts printing PASS/FAIL that must FAIL on the bug before the fix.
4. **Testing by change:** run all fast automated checks after every change: unit tests (CTest), Python unit tests and
   static checks. Also run checks relevant to the changed code and behaviour. Slow gameplay runs are separate from the fast checks.
   **After fixing at least three specs, recommend a full-suite run and ask the user directly before running it.**
   Never start the full suite without the user's explicit approval or request. Test thresholds, tick windows, repeats
   and pass criteria stay unchanged; report failures and incomplete runs honestly, with logs.
5. **Commits and pushes:** one local commit per fixed issue ("Fix #00N: …") ONLY AFTER the user explicitly confirms the
   maintainer test ("confirmed" / "close"). Push only after the user confirmed the issues in the batch and agreed to the push.
   Other commits only on command.
6. **Stop/go:** split big work into milestones in `work.md`. User present: a two-line status per milestone, and an
   immediate report (with the alternative) when an approach doesn't pay off. User away: keep going, log milestones.
7. **While the user is away you may:** work on approved specs in the agreed order; run automated tests off-screen;
   update `work.md` and the index; prepare fixes up to "fixed — maintainer test"; capture reference data (headless);
   create NEW issues (draft spec + findings) — but not fix them. Wait for the user for: fixes without an approved spec,
   spec changes, pushing, any visible window or desktop action, deleting outside your scratch space, downloads/installs/
   system settings. Stuck: write it under "Waiting for you" and continue with the next approved issue.

---

## Rules
- **NEVER ASSUME — ask the user, with a recommendation.** Need something opened, switched, installed or done, or unsure
  what the user has or wants: ask what, why, and what you recommend.
- **Decisions belong to the user:** scope, requirements, priorities, library/function/graphics behaviour choices,
  native-vs-emulation direction. Present 2–4 options with a recommendation; research and diagnosis may continue while
  waiting, applying the choice may not. A request to fix something is not agreement with a particular approach.
- **Native over emulation;** model hardware only where the runtime must. Prefer the game's own compiled code over runtime
  HLE stubs. Find root causes; no workarounds that paper over symptoms. Survey existing approaches before inventing.
- **Performance changes must give identical output** (same frames, same sound). Test at native resolution (1x).
- **Exact saved-frame comparison:** for renderer, graphics math, presentation changes, optimizations, or visual regressions,
  compare corresponding before/after frames covering the affected scene under identical settings. For renderer changes,
  replay saved PM4/render traces; for game logic/camera changes, capture matching output so the changed code executes.
- **The port is not specific to one machine.** Keep fixes portable across supported hardware and the planned Linux and
  Windows targets. The test machine's CPU/GPU are test equipment, not product requirements. Label performance measurements
  with test hardware; do not generalize one machine's timings or introduce machine-specific fixes.
- **The user's desktop is off limits:** never send synthetic input to it, never screenshot it (use frame dumps), never
  unlock it. Visible windows only for a manual test session or when asked.
- **One game at a time;** no builds or test runs while the user has a game window open.
- **Downloads need explicit permission** (name, source, size).
- **Nothing derived from the game in Git:** no game files (`game/`, `discs/`, `*.iso`, `*.xex`, `*.pe`), Ghidra DBs (`ghidra/`),
  generated C++ (`app/generated/default/`), frame dumps, memory snapshots (`notes/snapshots/`), or screenshots. Hooks in
  `.githooks/` (`git config core.hooksPath .githooks`); never bypass them (`--no-verify`).
- **Releases:** a release contains only our code and freely licensed tools (builder/wizard, recompiler, runtime, sources) —
  never the compiled game, disc files, ISO, generated C++, or game-derived assets. The player's PC builds the game.
  Releases are published only by the user.
- **Commit identity:** author/committer `TheSlackbringer <15327900+TheSlackbringer@users.noreply.github.com>`;
  end messages with `Co-Authored-By: Claude <model> <noreply@anthropic.com>` (or relevant assistant).
- **Use the Ghidra MCP whenever you can** (`mcp__ghidra__*` tools) for reverse engineering; call `get_program_info` first.
  Ghidra 12.1.2 at `/opt/ghidra`, PPC 64/32-bit big-endian (`ppc:BE:64:default` / `ppc:BE:32:default`).
- **Never delete build dirs containing `_deps`;** no `rm` with bare shell variables (use `"${VAR:?}"`).

---

## Essentials

### Building
```bash
# Build the native executable with RelWithDebInfo
cmake --build app/out/build/linux-amd64-relwithdebinfo -j16

# Build alone when regenerating code from manifest:
cmake --build app/out/build/linux-amd64-relwithdebinfo --target dw5e_codegen
```

### Running the game
```bash
cd app/out/build/linux-amd64-relwithdebinfo
./dw5e --gpu_plugin=xenos \
       --game_data_root=$HOME/Projects/dw5e-port/game/files \
       --log_file=$HOME/Projects/dw5e-port/notes/runN.log
```
- Runtime libraries: `librexruntimerd.so` and `libTracyClientrd.so` are symlinked into the build dir from `rexglue-sdk/out/linux-amd64/`.
- Codegen: automatically re-runs during CMake build when `app/config/*.toml` or `app/dw5e_manifest.toml` changes.

### Debugging & Tools
- **Heap snapshot hotkey:** pressing `F9` in-game dumps `0x40000000-0x80000000` + image to `notes/snapshots/` (via `app/src/dw5e_snapshot.cpp`).
- **Diffing snapshots:** `python3 tools/snapdiff.py notes/snapshots/snap_001.bin notes/snapshots/snap_002.bin ...` isolates changed memory addresses.
- **Camwatch hardware-watchpoint tracing:** `tools/run_camwatch.sh` + `tools/camwatch.py` runs batch GDB with hardware watchpoints on view eye/yaw, logging unique writer stacks per game phase to `notes/camwatch.log`.
- **Finding missing functions:** `python3 tools/find_missing_funcs.py` scans code/data for function pointers omitted from `.pdata`.
- **Finding constant references:** `python3 tools/find_const_refs.py`.
- **Raw disassembly:** `tools/disasm.sh 820b0000 3d8600` disassembles a `.text` range with llvm-objdump.

---

## Reference

### Repository map
- `app/` — our port executable and configuration:
  - `src/main.cpp` — native host entrypoint, game initialization, and plugin registration.
  - `src/dw5e_camera.cpp` — right-stick camera hook (yaw & pitch orbit around focus/target, cvars `dw5e_rstick_camera_speed`, `dw5e_rstick_camera_pitch_speed`, `dw5e_rstick_camera_invert`).
  - `src/dw5e_snapshot.cpp` — F9 memory heap snapshot dumper.
  - `src/dw5e_trace_hooks.cpp` — lifecycle trace hooks for XMEDIA player and sub-systems.
  - `src/dw5e_app.h` — application headers and hook definitions.
  - `config/functions.toml` — manual function entries and missing `.pdata` functions (e.g. switch-table thunks, unit callbacks).
  - `config/camera_hooks.toml` — midasm hooks for camera control.
  - `config/trace_hooks.toml` — midasm trace hooks for subsystem debugging.
  - `dw5e_manifest.toml` — ReXGlue project manifest (game root, entrypoint `default.xex`, output dir `generated/default`).
  - `generated/rexglue.cmake` — CMake SDK integration and codegen target definitions.
- `patches/rexglue/` — patch series against ReXGlue SDK v0.10.0 (`c94f5eb`) (following the `Gears3-Azura` and `dw5-ps2` patch-management pattern):
  - `0001-dw5e-runtime-fixes.patch`:
    1. `src/system/function_dispatcher.cpp`: invalid-call trap with fault-free guest stack walk (`process_vm_readv`) and vtable dump.
    2. `src/audio/xma_context.cpp`: XMA frame drain fix (prevents audio starvation / stall).
    3. `src/system/xevent.cpp` & `xobject.cpp`: guest dispatch header sync on native object lookup (`SyncFromGuest()`), fixing the KEVENT Delegate/attract-mode crash.
- `.recomp.json` — project metadata and tracking specification for `recomp.fyi` / `recomp.board`.
- `tools/setup_sdk.sh` — automated script to clone pinned ReXGlue SDK v0.10.0 and apply `patches/rexglue/*.patch`.
- `tools/` — analysis, decryption, and extraction scripts:
  - `xdvdfs.py` — Xbox 360 ISO extractor.
  - `xex_decrypt.py` & `xex_info.py` — retail XEX decryption and PE header analysis.
  - `camwatch.py` & `run_camwatch.sh` — GDB hardware watchpoint camera analyzer.
  - `snapdiff.py` — memory snapshot differ.
  - `ghidra-ext/` — XEXLoaderWV and GhidraMCP extension downloads and work directories.
- `notes/` — technical documentation and analysis:
  - `PHASE1.md` — initial executable, XDK 2732 library analysis, and tooling assessment.
  - `PHASE2.md` — playable bring-up notes, KEVENT bug root cause, and camera architecture reverse engineering.
  - `save-restore.toml`, `imports-named.txt`, `import-coverage.txt`, `disc-files.txt`.
- Gitignored (local only):
  - `game/` (`dw5e.iso`, `default.xex`, `default.pe`, `default_ghidra.xex`, `files/`).
  - `ghidra/` (`dw5e.gpr`, `dw5e.rep`).
  - `app/generated/default/` (recompiled C++ generated by ReXGlue from `default.xex`).
  - `app/out/` and `rexglue-sdk/out/` (build directories).
  - `notes/snapshots/` (1 GB+ memory dumps) and `*.log` files.

### Camera architecture
- **Views array at `0x82846FD0`:** 6 views, 256 bytes per view (`0x822AA950(idx) = 0x82846FD0 + idx * 256`).
  - Active view index at `0x82846FB0` (get `0x822AA940`, set `0x822AA908`).
  - View layout: `+0x00` up-basis vector, `+0x10` forward vector (normalized eye→target), `+0x20` `{pitch, yaw, roll, 0}` (yaw = `atan2(dx, dz)`, rad), `+0x30` eye position, `+0x40` target position, `+0x50` eye copy, `+0x60` angle copy, `+0x70` focus/player point.
- **Battle camera tick:** `0x822181C8` → `0x82217B10` → `0x822171B0` → `0x82254EF8` → `0x82253C10`:
  - `0x822AE118(viewIdx, focus*, B*, mode)` → `0x822AC7A8` (battle follow camera, clamps eye.y vs terrain, leash logic).
  - `0x822AE258` → `0x822AC120` (L1 guard/snap reset path).
- **Right-stick hook implementation (`app/src/dw5e_camera.cpp`):**
  - Yaw hook at `0x822AE118` entry: orbits `view.eye` (`+0x30`) and `+0x50` around focus point (`r4`) in XZ by right-stick X.
  - Pitch hook at `0x822AE1F8` (after `0x822AC7A8` returns): orbits eye vertically around target (`+0x40`) by accumulated offset, recomputing pitch `+0x20` via `-atan2(target.y - eye.y, horiz)`.

### Pitfalls & lessons learned
- **Direct guest memory writes to KEVENT:** Xbox 360 guest code directly writes `SignalState = 0` to guest structs without invoking `KeResetEvent`. Host event shadows must call `SyncFromGuest()` whenever a native object lookup occurs (`xobject.cpp`), otherwise threads loop indefinitely on destroyed objects (the Delegate / attract mode crash).
- **XMA audio frame draining:** XMA subframe decode loops must not break immediately when input buffers exhaust if subframes remain unconsumed; otherwise audio streams stall.
- **Hidden functions after jump tables:** early XDK LTCG compilers emit small 0x10-byte callback functions immediately following `blr`/`bctr` instructions in jump-table dispatchers without `.pdata` records (e.g. `0x8213D118`, `0x8213D608`, `0x8231E208`). They must be explicitly listed in `config/functions.toml`.
- **XEXLoaderWV early-XDK import bug:** early XDK titles have `0xFF` in the high byte of `name_index` (e.g. `0x2B14`, `0x2C2C`). XEXLoaderWV `readShort` fails unless patched to mask to the low byte (`name_index & 0xFF`).
