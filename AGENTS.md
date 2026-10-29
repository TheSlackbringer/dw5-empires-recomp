# AGENTS.md — Dynasty Warriors 5 Empires native port

Static recompilation of **Dynasty Warriors 5: Empires** (Xbox 360, PAL/Europe, Title ID `4B4F07D3`) to native Linux
and Windows executables with **ReXGlue** (PowerPC Xenon → C++ ahead of time) and the Xenia-derived `xenos` GPU plugin
(PM4 command processor; Direct3D 12 on Windows, Vulkan on Linux). Goal: the full game playable natively at 60 FPS,
widescreen, clean logs, instant loading, right-stick camera (yaw + pitch), WMV movie playback, no emulation overhead.
GitHub: TheSlackbringer/dw5-empires-recomp.

Read first: this file → `notes/issues/README.md` (local: index, "Waiting for you", working agreement) → the current
issue's `spec.md` and `work.md`. Background: `notes/PHASE1.md`, `notes/PHASE2.md`. Setup/build commands:
"Essentials" below and `notes/PROJECT_SETUP.md`. Look-ups (repo map, how our code reaches the game, debug switches,
Ghidra, pitfalls): "Reference" at the end of this file.

## Hard rules (2026-10-19 — after an agent weakened tests to make them pass)
- **Never change a test's pass criteria** (thresholds, tick windows, run mode) to make it pass. Changing criteria is
  a spec change: the user approves it first. A failing test is reported as failing.
- **No timing workarounds:** no delays, sleeps or knobs tuned to one game routine or one machine. Fix the root cause.
- **Evidence before binding:** never bind, unbind or override guest functions or SDK stubs without verified evidence
  of the guest contract (registers, memory written, callbacks, state owned) and calling convention.
- **Every claimed result names its log** (`notes/issues/tests/out/...`, `notes/evidence/...` or the game log).
  "Passes" means the run has finished.
- **Only the user approves a spec.** Write "approved" only after the user said so, with the date.
- **Only the user confirms or closes an issue:** mark an issue `confirmed` or `closed` only after the user typed
  "confirmed", "approved" or "close" in chat. Reaching `fixed — maintainer test` means stop and wait for the review.
- **Never ask the user to run an automated test:** automated tests are the agent's job to run, check and log. The
  "Maintainer test" items are only for what a machine cannot judge (gameplay feel, controller response, visual
  artifacts, sound timing). If an issue is fully covered by automated tests, its Maintainer test is
  "None (fully automated)".
- **Pushing requires permission:** push only after the user agreed to that push.
- Changes to `notes/` are tracked in its own local git repo (`git -C notes log`); commit your notes changes there.
- **Before relying on the game files, verify them:** `game/dw5e.iso` (SHA1 `82fc63021a0919d8b97584682babf0654997c0a9`)
  and the extracted `default.xex` must match the player's legally dumped retail disc (PAL XGD2).

## Working agreement (2026-10-16)
1. **One folder per issue**, `notes/issues/NNN-short-name/` (local, never committed; templates in `notes/issues/`):
   - `spec.md` — what the user and you agreed. Copy `notes/issues/TEMPLATE-spec.md` and fill EVERY section:
     what the user sees; background (facts with numbers, what is ruled out); how to reproduce; **recommended fix**
     (step by step, why it works, expected result); **native vs emulation** (a header line plus a section: which
     parts are native code, which stay emulated or stubbed, anything moving between them); **main risks** (what could
     go wrong or need a user decision, and what then); alternatives considered (and why not); measurable pass
     criteria; the automated test; 1–3 "Maintainer test" items that require human judgement; out of scope. Plain
     words, no unexplained jargon. Examples: issues 003 and 004.
   - `work.md` — stop/go milestones (not in the spec), all your findings, progress, measurements, conclusion, fix
     (commit) and history, kept current. Findings never live only in chat.
2. **Spec approval before any work.** Draft the spec (fact-finding needed to write it is fine); the user approves.
   Push back with reasons when you think a spec is wrong; the user decides and can overrule. No fixes or experimental
   code changes without an approved spec. While away, the user can allow work on an unapproved spec — explicitly.
3. **A test per issue, written first:** `notes/issues/tests/issue_NNN_*` prints PASS/FAIL and must FAIL on the bug
   before the fix (where measurable); its output goes to `notes/issues/tests/out/NNN-*/`. Lasting checks of our code
   belong in `app/tests/` (CTest) or `tools/release/test_*.py`. Closed issues keep their tests as regression guards.
4. **Testing by change (2026-10-21):** run all fast automated checks after every change: unit tests (CTest), Python
   unit tests and static checks. Also run checks relevant to the changed code and behaviour. Use CI
   (`.github/workflows/build.yml`, Linux AppImage + Windows) to validate builds. Slow gameplay runs are separate from
   the fast checks. **After fixing at least three specs, recommend a full-suite run and ask the user directly before
   running it.** Never start the full suite without the user's explicit approval or request. Test thresholds, tick
   windows, repeats and pass criteria stay unchanged; report failures and incomplete runs honestly, with logs.
   Maintainer checks cover only the spec's short "Maintainer test" list.
5. **Commits and pushes:** one local commit per fixed issue ("Fix #00N: …") when it reaches `fixed — maintainer test`.
   Push only after the user confirmed the issues in the batch and agreed to the push. Other commits only on command.
6. **Stop/go:** split big work into milestones in `work.md`. User present: a two-line status per milestone, and an
   immediate report (with the alternative) when an approach doesn't pay off. User away: keep going, log milestones.
7. **While the user is away** you may: work on approved specs in the agreed order; run automated tests off-screen;
   update `work.md` and the index; make local per-issue commits; capture reference data (headless); create NEW
   issues (draft spec + findings) — but not fix them. Wait for the user for: fixes without an approved spec, spec
   changes, pushing, any visible window or desktop action, deleting outside your scratch space, downloads/installs/
   system settings, turning the PC off (only when asked that time). Stuck: write it under "Waiting for you" and
   continue with the next approved issue.

## Rules
- **NEVER ASSUME — ask the user, with a recommendation.** Need something opened, switched, installed or done
  (a Ghidra tab, a game screen, a file), or unsure what the user has or wants: ask what, why, and what you recommend.
- **Decisions belong to the user:** scope, requirements, priorities, library/function/graphics behaviour choices,
  native-vs-emulation direction. Present 2–4 options with a recommendation; research and diagnosis may continue while
  waiting, applying the choice may not. A request to fix something is not agreement with a particular approach.
- **Native over emulation;** model hardware only where the runtime must. Prefer the game's own recompiled code over
  runtime HLE stubs (kernel/XAM exports in `rexglue-sdk/src/kernel/`). Find root causes; no workarounds that paper
  over symptoms. Survey existing approaches (ReXGlue, Xenia, other ReXGlue ports) before inventing.
- **Performance changes must give identical output** (same frames, same sound). Test at **1x**: default
  `resolution_scale`/`draw_resolution_scale_x/_y` = 1 and no `swap_post_effect`; check them in the run's settings.
- **Exact saved-frame comparison:** for renderer, shader, graphics math, presentation changes, optimizations or
  refactors in those paths, and visual-regression investigations. Compare a small set of corresponding before/after
  frames covering the affected scene, with the same input, machine/GPU/driver, backend and settings (1x). For
  renderer changes, replay the same saved PM4/render trace in both builds; for game/camera changes, capture matching
  output from each build so the changed code actually runs. If matching evidence is unavailable, record the
  comparison as pending and explain why; unrelated frames cannot establish output equality. Output-preserving
  optimizations must be IDENTICAL; for intended graphics fixes, explain the differences against the approved spec.
  Capture images separately from timing runs (saving frames can itself cause stutter). Not needed for documentation,
  packaging, CI, setup-only, audio-only or other changes unrelated to rendered output.
- **Function-map and generated-code checks:** when changing `app/config/functions.toml`, midasm hooks
  (`app/config/*_hooks.toml`), `app/dw5e_manifest.toml` or the codegen in `rexglue-sdk`, check the edited entries
  against the binary first (Ghidra, `tools/find_missing_funcs.py`, `tools/disasm.sh`), then read the affected
  generated output in `app/generated/default/` after regeneration. Record what was checked and its limits in the
  issue's work log. Unrelated changes, ordinary play and FPS profiling do not require it.
- **Binding reviews:** when investigating a suspected stub or binding error, or changing which guest functions are
  replaced, establish the original contract independently (Ghidra, the XDK library code in the binary) and cite the
  evidence in the work log. Missing evidence stays unresolved; a clean review does not prove gameplay equivalence.
- **GPU backend:** develop and test on the `xenos` plugin with each platform's default backend (Direct3D 12 on
  Windows, Vulkan on Linux); no other backend or renderer work unless asked.
- **The port is not specific to one machine.** Keep fixes portable across supported hardware and the Linux and
  Windows targets. The test machine's CPU/GPU are test equipment, not product requirements. Label performance
  measurements with their test hardware; do not generalize one machine's timings or introduce machine-specific
  fixes. Correctness is defined by Xbox 360 behaviour.
- **The user's desktop is off limits:** never send synthetic input to it, never screenshot it (use frame dumps or
  logs), never unlock it. Starting the game opens a visible window on Windows: visible windows only for a manual
  test session or when asked.
- **One game at a time;** no builds or test runs while the user has a game window open. Test runs use
  `--audio_mute=true`; live play uses the default output. Never change the user's audio defaults.
- **Downloads need explicit permission** (name, source, size).
- **Nothing derived from the game in Git:** no game files (`game/`, `discs/`, `*.iso`, `*.xex`, `*.pe`), Ghidra DBs
  (`ghidra/`), generated C++ (`app/generated/default/`), frame dumps, memory snapshots, captures, screenshots. Scan
  before every commit/push. Hooks in `.githooks/` (`git config core.hooksPath .githooks`); never bypass them
  (`--no-verify`).
- **Releases:** a release contains only our code and freely licensed tools (the builder: setup wizard, recompiler,
  runtime, sources, compiler) — never the compiled game, disc files, an ISO, generated C++ or any other game-derived
  file. The player's PC builds the game. Releases are published only by the user.
- **Commit identity:** author/committer `TheSlackbringer <15327900+TheSlackbringer@users.noreply.github.com>`;
  end messages with `Co-Authored-By: Claude <model> <noreply@anthropic.com>` (or the relevant assistant).
  `rexglue-sdk/` is vendored: an SDK change is an ordinary edit there plus the matching patch in `patches/rexglue/`
  (next number, e.g. `0004-<topic>.patch`), in the same commit. Never patch disc files or generated output by hand.
- **Use the Ghidra MCP whenever you can** (`mcp__ghidra__*` tools: decompile, xrefs, strings, renames/comments)
  for reverse engineering; call `get_program_info` first. Headless only when the MCP is unreachable (GUI closed),
  for batch jobs, or for parallel agents — always on a copy (Reference → Ghidra). If the needed program is not the
  active tab, use `ghidra-bethington` with `program=`.
- **Parallel agents** get self-contained briefs and do analysis/patch-writing in their own worktree or scratch dir
  (Ghidra on their own copy); one integrator builds and runs the game. Each worktree gets its own build directory.
- **Never delete build dirs containing `_deps`;** no `rm` with bare shell variables (use `"${VAR:?}"`).
- Keep `AGENTS.md` current when rules or the way of working change (with the user's permission for each edit).
  Check usage before expensive batches; at ≤10 % left, checkpoint (`work.md`, index) before continuing.

## Essentials
- Build (Linux dev): `cmake --build app/out/build/linux-amd64-relwithdebinfo -j16`; codegen alone:
  `--target dw5e_codegen` (re-runs automatically when `app/config/*.toml` or `app/dw5e_manifest.toml` change).
  Presets for both platforms: `app/CMakePresets.json` (`win-amd64-relwithdebinfo`, `linux-amd64-relwithdebinfo`, …).
- CTests: `ctest` in the build dir (`app/tests/`: setup, setup reliability, safe read, kernel exports, GPU plugin
  loading, keyboard, Vulkan startup, DXC UUID). Release tooling tests:
  `python3 -m unittest discover -s tools/release -p 'test_*.py' -v`.
- Run all fast checks plus checks relevant to the changed area (testing rule above). CI builds and tests both
  platforms; the packages are checked by `tools/release/verify_package.py`.
- Unsure how the original looks or behaves: ask the user for what logs and frame dumps can't show (feel, sound timing).
- Debug code: `DW5E-DEBUG` (kept, cvar-gated) and `TEMP-DEBUG` (remove before committing).

## Reference

### Repository map
- `rexglue-sdk/` — vendored ReXGlue SDK v0.10.0 (BSD-style licence, parts derived from Xenia) with our fixes
  applied; each fix is also kept as a patch in `patches/rexglue/` (`0001-dw5e-runtime-fixes`, `0002-windows-export-
  and-dxc-compat`, `0003-windows-gpu-plugin-name`). Key areas: `src/kernel/` (xboxkrnl/XAM HLE), `src/system/`
  (threads, objects, function dispatcher), `src/graphics/` (GPU system, command processor, `xenos` plugin),
  `src/ui/` (presenters D3D12/Vulkan, debug overlay), `src/audio/` (XMA, SDL output), `src/core/` (cvars, logging,
  perf counters), `thirdparty/` (SDL3, FFmpeg, Tracy, …).
- `app/` — our executable and configuration:
  - `src/main.cpp` (host entry point, plugin registration), `src/dw5e_camera.cpp` (right-stick camera hooks),
    `src/dw5e_input.cpp` (default keyboard controls), `src/dw5e_snapshot.cpp` (F9 memory snapshot),
    `src/dw5e_trace_hooks.cpp` (XMEDIA/sub-system lifecycle traces), `src/dw5e_safe_read.h`, `src/dw5e_app.h`.
  - `config/functions.toml` (functions missing from `.pdata`, e.g. switch-table thunks, unit callbacks),
    `config/camera_hooks.toml`, `config/trace_hooks.toml` (midasm hooks).
  - `dw5e_manifest.toml` (ReXGlue project: game root, entry point `default.xex`, output `generated/default`),
    `generated/rexglue.cmake` (SDK integration, codegen target).
  - `setup/` — the player's setup wizard (disc check, extraction, codegen, build, launch).
  - `tests/` — CTests.
- `tools/` — `release/` (packaging, `make_release.py`, `verify_package.py`, Linux SDK export), `windows/` and
  `linux/` (toolchain files), `docker/`; analysis: `xdvdfs.py` (ISO extractor), `xex_decrypt.py`, `xex_info.py`,
  `find_missing_funcs.py`, `find_const_refs.py`, `disasm.sh`, `snapdiff.py`, `camwatch.py` + `run_camwatch.sh`,
  `ghidra-ext/`.
- `.recomp.json` — project metadata for `recomp.fyi` / `recomp.board`.
- Local, gitignored: `game/` (`dw5e.iso`, `files/` with `default.xex`), `ghidra/` (project `dw5e`),
  `app/generated/default/`, `app/out/`, `app/build*/`, `notes/` (private notes repo: issues, evidence, history).

### How our code reaches the game
ReXGlue's codegen translates every function of `default.xex` (from `.pdata` plus `config/functions.toml`) into C++
in `app/generated/default/`, registered under its guest address; calls go through the function table. Our code
reaches the game in two ways: **midasm hooks** (`[[midasm_hook]]` in `app/config/*_hooks.toml`: address, the named
host function in `app/src`, the guest registers it receives) and host-side services (input, snapshot). SDK-level
behaviour (kernel exports, GPU, audio) is changed in `rexglue-sdk/` plus a patch. Never edit generated output by
hand; regenerate it.

### Running the game by hand (prefer the issue tests)
- Linux dev build, from `app/out/build/linux-amd64-relwithdebinfo`: `./dw5e --gpu_plugin=xenos
  --game_data_root=<repo>/game/files --log_file=<log> --audio_mute=true`. Runtime libraries (`librexruntimerd.so`,
  `libTracyClientrd.so`) are symlinked into the build dir.
- Windows player install: `DW5E.exe` (setup/launcher) builds into `%LOCALAPPDATA%\DW5E` and starts
  `build\dw5e.exe --game_data_root=… --user_data_root=… --cache_root=… --gpu_plugin=xenos`. Game logs:
  `%LOCALAPPDATA%\DW5E\build\logs\dw5e_NNN.log`; setup logs: `%LOCALAPPDATA%\DW5E\logs\`.
- `dw5e` has no `--help`: any start runs the game (without `--gpu_plugin` it runs with no GPU).
- The game window opens visibly; there is no scripted input or hidden-window mode yet, so routes past the title
  screen need a player.

### Debug switches (cvars, `--name=value`, all optional)
Logging: `log_file` (empty = numbered files), `log_level` (trace…off), `log_noisy`, `log_flush_interval`,
`log_max_file_size_mb` (5), `log_max_files` (20), `log_high_frequency_kernel_calls`.
GPU: `gpu_plugin=xenos`, `vsync` (guest vblank pacing, default on), `resolution_scale` / `draw_resolution_scale_x/_y`
(keep 1 for tests), `swap_post_effect` (none|fxaa|fxaa_extreme), `store_shaders`, `async_shader_compilation`,
`dump_shaders=<dir>`, `gpu_debug_markers`; backend-specific `d3d12_*` / `vulkan_*`.
Audio: `audio_mute`, `ffmpeg_verbose`. Camera: `dw5e_rstick_camera_speed`, `dw5e_rstick_camera_pitch_speed`,
`dw5e_rstick_camera_invert`, `dw5e_rstick_camera_invert_y`. Kernel: `cl` (extra guest command line).
In game: **F3** debug overlay (`Guest: X FPS (Y ms)`), **F9** memory snapshot.

### Measuring speed and frame pacing
- F3's guest FPS is the interval between guest `XE_SWAP` packets (`src/graphics/command_processor.cpp`); the game
  presents every 2nd vblank, so normal speed is a steady 30 FPS / ~33.3 ms.
- The guest vblank thread (`src/graphics/graphics_system.cpp`) sleeps 1 ms per loop and catches up missed vblanks in a
  burst; on Windows the default timer resolution is ~15.6 ms and the runtime does not raise it.
- `--perf_log_csv` exists but is not wired up (nothing opens the file). RelWithDebInfo builds mark each swap for
  Tracy 0.13.1 in on-demand mode; a matching `tracy-capture` records the frames.
- Never measure stutter with frame dumps or heavy tracing on.

### Ghidra
- Ghidra 12.1.4, official build (install path, extensions and MCP registration: `notes/PROJECT_SETUP.md`). PPC
  big-endian (`ppc:BE:64:default` / `ppc:BE:32:default`), XEXLoaderWV (patched, see Pitfalls). GhidraMCP serves the
  active CodeBrowser tab at `http://127.0.0.1:8080/`; call `get_program_info` first: the GUI may show another program.
- **Second MCP, `ghidra-bethington`** (port 8089): reaches every program open in the project via `program=<name>`
  (`list_open_programs` shows the names). Use its MCP tools, not `curl`. Switch projects unattended with its
  `open_project` (`path=<absolute .gpr>`, `headless=false`, `program=/<program>`), then `get_program_info`; it saves
  first and refuses while analysis runs (needs the locally patched plugin). Never call `exit_ghidra`, `delete_file`,
  `import_*`, archive/restore or checkout tools; `close_program` only with `save=true`. One CodeBrowser at a time.
- ONE source project `ghidra/dw5e.gpr` (renames/comments go here, via GUI/MCP). Headless never runs on it: copy
  `ghidra/dw5e.gpr` + `ghidra/dw5e.rep` to your scratch dir, run `analyzeHeadless` there (`-readOnly`), then delete
  the copy.
- Quick look at one function: the generated C++ in `app/generated/default/` comments the guest instructions.

### Camera architecture
- **Views array at `0x82846FD0`:** 6 views, 256 bytes per view (`0x822AA950(idx) = 0x82846FD0 + idx * 256`).
  - Active view index at `0x82846FB0` (get `0x822AA940`, set `0x822AA908`).
  - View layout: `+0x00` up-basis vector, `+0x10` forward vector (normalized eye→target), `+0x20`
    `{pitch, yaw, roll, 0}` (yaw = `atan2(dx, dz)`, rad), `+0x30` eye position, `+0x40` target position, `+0x50` eye
    copy, `+0x60` angle copy, `+0x70` focus/player point.
- **Battle camera tick:** `0x822181C8` → `0x82217B10` → `0x822171B0` → `0x82254EF8` → `0x82253C10`:
  - `0x822AE118(viewIdx, focus*, B*, mode)` → `0x822AC7A8` (battle follow camera, clamps eye.y vs terrain, leash).
  - `0x822AE258` → `0x822AC120` (L1 guard/snap reset path).
- **Right-stick hooks (`app/src/dw5e_camera.cpp`):** yaw at `0x822AE118` entry orbits `view.eye` (`+0x30`) and
  `+0x50` around the focus point (`r4`) in XZ by right-stick X; pitch at `0x822AE1F8` (after `0x822AC7A8` returns)
  orbits the eye vertically around the target (`+0x40`) and recomputes pitch `+0x20` via
  `-atan2(target.y - eye.y, horiz)`.

### Pitfalls (details in notes/PHASE2.md and the issue work logs)
- **Direct guest writes to KEVENT:** guest code writes `SignalState = 0` into guest structs without `KeResetEvent`.
  Host event shadows must `SyncFromGuest()` on every native object lookup (`xobject.cpp`), otherwise threads loop on
  destroyed objects (the Delegate / attract-mode crash).
- **XMA frame draining:** subframe decode loops must not stop when input buffers run out while subframes remain;
  otherwise audio streams stall.
- **Hidden functions after jump tables:** early XDK LTCG emits small 0x10-byte callbacks right after `blr`/`bctr` in
  jump-table dispatchers without `.pdata` records (e.g. `0x8213D118`, `0x8213D608`, `0x8231E208`); list them in
  `config/functions.toml`.
- **XEXLoaderWV early-XDK import bug:** early XDK titles have `0xFF` in the high byte of `name_index` (e.g. `0x2B14`,
  `0x2C2C`); XEXLoaderWV `readShort` fails unless patched to mask the low byte (`name_index & 0xFF`).
- **Windows GPU plugin name:** the runtime loader expects `rexgpu-<name><config>.dll`; MinGW's default `lib` prefix
  breaks discovery (issue 004).
- **Timer resolution on Windows:** 1 ms sleeps last up to ~15.6 ms unless a resolution is requested; keep this in
  mind when reading Windows frame timing (issue 006).
