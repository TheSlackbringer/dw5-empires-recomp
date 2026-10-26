# Phase 2: native build running (2026-10-11)

## Status
Playable on Linux: intro movies, menus, Empire mode strategy (incl. Delegate), battles, saving. Clean logs.
Graphics still via ReXGlue's Xenia-derived `xenos` GPU plugin (Vulkan).

## How to build / run
```
cmake --build ~/Projects/dw5e-port/app/out/build/linux-amd64-relwithdebinfo -j16
cd ~/Projects/dw5e-port/app/out/build/linux-amd64-relwithdebinfo
./dw5e --gpu_plugin=xenos --game_data_root=$HOME/Projects/dw5e-port/game/files --log_file=$HOME/Projects/dw5e-port/notes/runN.log
```
Build reruns codegen when `app/config/*.toml` changes. `librexruntimerd.so` / `libTracyClientrd.so` are symlinked into the build dir from `rexglue-sdk/out/linux-amd64/` (SDK writes them there).

## Project-side setup (app/)
- `CMakePresets.json`: clang/clang++ (template said clang-20), `-march=x86-64-v2` (needed for SSSE3 intrinsics in the SDK).
- `CMakeLists.txt`: `GPU_PLUGINS xenos`; imgui include path for rex_app.cpp; `src/dw5e_trace_hooks.cpp`.
- `config/functions.toml`: 2 hand-found + 18 scanned functions missing from .pdata (vtable targets in the D3D/XMEDIA code region 0x822E0000-0x822F2000). SEH funclets inside functions are deliberately NOT declared.
- `config/trace_hooks.toml` + `src/dw5e_trace_hooks.cpp`: lifecycle trace of the XMEDIA player object. Can be removed from manifest includes now.

## Local changes to rexglue-sdk (v0.10.0, c94f5eb); see `git diff` there
1. `src/system/function_dispatcher.cpp`: invalid-call trap logs lr/regs, object vtable dump, fault-free guest stack walk (process_vm_readv).
2. `src/audio/xma_context.cpp`: xenia-canary 6bef1fb (drain partially consumed XMA frame). Not the cause of our crash, kept.
3. **Event fix** (xevent.h/.cpp, xobject.h/.cpp): port of xenia-canary dcf2994 "Reconcile the guest dispatch header on native object lookup", event half only. Semaphore half NOT ported yet.

## Root cause of the Delegate / attract-mode crash
XMEDIA player worker thread (0x822C2730) waits on two guest-built KEVENTs (global 0x82B064A0 +12/+16). After processing, it resets them by writing `SignalState = 0` directly into guest memory, releases the player object (refcount 1 -> 0, destructor leaves base vtable 0x82092C18), and loops. ReXGlue's host-side event shadow never saw the direct write, so the next wait returned immediately and the worker called vtable slot 15 on the destroyed object -> call to 0x00000000. Same bug as Xenia's "freeze after voice line" in all Koei Musou titles, fixed in Canary 2026-09-13.

## Next
- Port the semaphore half of dcf2994 (game uses KeInitializeSemaphore/KeReleaseSemaphore).
- Phase 4 feature: right-stick camera (original DW5E has none; DW6E added it).

## Camera hunt (Phase 4 feature: right-stick camera), state 2026-10-12
Original DW5E has no right-stick camera (DW6E added it). Goal: rotate the follow camera with the right stick.
- Input path: XamInputGetState thunk 0x82487E0C <- XInputGetState 0x8230A220 <- pad read 0x82465928 (raw pad: +12 buttons, +16 pressed, +20/+24 LX/LY, +28/+32 RX/RY, -128..127).
  Pad manager 0x822BABD8 iterates slots at 0x82AAB2E8 (236 bytes each); 0x822B9FF0 stores dead-zoned RIGHT stick in slot+24/+28 (via 0x82465A88), left stick in slot+32/+36 (via 0x82465A48).
- F9 snapshot key (app/src/dw5e_snapshot.cpp) dumps 0x40000000-0x80000000 + image to notes/snapshots/; tools/snapdiff.py diffs 4 snapshots.
- First capture (camera behind char / same / turned ~180 deg + reset / back): camera X/Z found only in a ring buffer at 0x4241E420.. of {X splat, Z splat, time} records (likely the 3D sound listener history): (-23.21, 302.42) <-> (-209.85, 209.64), orbit radius ~104 around (-116.5, 256). No camera struct with that value in the snapshotted heap, so the real camera likely lives in 0xA0000000+ (physical allocations, not snapshotted) or is stored differently.
- 0x45D35700.. = character bone transforms, not camera.
- Next: Ghidra (12.1.2, XEXLoaderWV + GhidraMCP extensions patched to 12.1.2, MCP registered user-wide as "ghidra"). Find the writer of the listener ring buffer (0x4241E4xx) -> its caller passes camera position -> camera update code. Then gdb hardware watchpoint on host address 0x100000000+guest to confirm, then a midasm hook adding slot+24 (right stick X) to the camera yaw.
- Ghidra project imports `game/default_ghidra.xex`: copy of default.xex with the two import-library name_index high bytes 0xFF -> 0x00 (offsets 0x2B14, 0x2C2C). XEXLoaderWV reads that field with readShort, gets 0xFF00, and fails silently ("Failed to load" / IOException(null)); early-XDK titles have 0xFF there. Worth reporting upstream (fix: read the low byte only).

### Camera hunt update 2026-10-12 (session 2)
- CORRECTION: 0x4241E420.. is NOT a listener history. It is a distance-sorted list of 48-byte records {X splat,1},{Z splat,1},{key,0,0,0}; the two "orbit" points are just two entities. Dead end.
- World units are large (~37000, -5000, 17000); Y axis points DOWN (up vector = (0,-1,0)).
- Engine scene camera: object 0x4010B3D0 (vtable 0x8201CD88, global ptr at 0x82B06A90 = singleton from getter 0x820BF090). Layout from +0x170 is a 480-byte camera block: eye +0x180, target +0x190, up +0x1A0, view +0x1B0 (copy +0x1F0), proj +0x230, viewproj +0x270, near/far/fovy/aspect +0x2B0 (64/9000/0.5741/1.7778 main pass). Filled per render pass by memcpy in 0x8244C7E8(scene, camBlock, ...) called from the renderer (0x820C0C58.. 14 call sites).
- **Game-side camera = view array at 0x82846FD0, 256 bytes per view (6 views)**, accessor 0x822AA950(idx) = 0x82846FD0 + idx*256; active idx at 0x82846FB0 (get 0x822AA940, set 0x822AA908). Init 0x822A9700. View layout:
  +0x00 up-ish basis vec, +0x10 forward (normalized eye->target), +0x20 {pitch, yaw, roll, 0} (yaw = atan2(dx,dz) of eye->target, rad), +0x30 eye, +0x40 target, +0x50 eye copy, +0x60 angle copy, +0x70 focus/player-ish point, +0x98 float (set 0x822A9F60), +0xBC view idx, +0xC0.. viewport ints.
  Accessors: GetFwd 0x822A9B20, GetEye 0x822A9C28, GetTarget 0x822A9C70, GetAngles 0x822A9CB8, Get+0x70 0x822A9D00, SetEye 0x822A9E60, SetTarget 0x822A9EA0 (then recompute 0x822A9348), SetAngles(view0) 0x822A9EE8 (then 0x822A9610).
- SetEye callers: 0x82110090 0x8211FFB0 0x821206C0 0x82120D70 0x82131158 0x821311A0 0x821311E8 0x821AB0C8(x3, scripted/menu orbit w/ 20-frame lerps) 0x821ABB28 0x8220A730 0x82220F00 0x82254F88 (init cam behind an officer via entity +0x90/+0xA0) 0x822AB050 0x822AD650. The battle follow-camera may write the view directly (camera module 0x822AD000-0x822AF000: 0x822AD650, 0x822ADB50, 0x822ADF28...).
- Ghidra caveat: the savegpr/restgpr helpers (0x82469CA4.. etc.) are 4-byte functions flagged non-returning, so decompiling anything that uses them yields "does not return". Also ~4000 functions missing vs ReXGlue's list (generated/default/dw5e_funcs.h has all 17373; native symbols sub_XXXXXXXX exist in the dw5e binary). Raw asm of .text: tools/disasm.sh 820b0000 3d8600 (llvm-objdump; VMX128 decodes as garbage).
- gdb run (tools/run_camwatch.sh + tools/camwatch.py; batch gdb, hw watchpoints on view0 eye.x/yaw, logs unique writer stacks per phase from notes/camwatch.phase to notes/camwatch.log/.counts) found the **battle camera**:
  per-frame 0x822181C8 -> 0x82217B10 -> 0x822171B0 -> 0x82254EF8 -> 0x82253C10 (battle cam tick) ->
    a) 0x822AE118(viewIdx, focus*, B*, mode) -> 0x822AC7A8 (follow cam, big, with ray tests 0x8211E118) -> 0x822AB570 (eye = focus + horizDir(eye-focus)*dist; keeps current direction = LEASH camera), 0x822AB660 (eases pitch/yaw +0x20/+0x24 toward eye->target, rate limits view+0xB4/+0xB8), 0x822AC520 (eye)
    b) 0x822AE258 -> 0x822AC120 (eye+yaw, then recompute 0x822A9348): likely L1 reset / snap.
  Title/intro camera: 0x82110090 (SetEye/SetTarget). Battle start: 0x82254F88 places the camera behind the officer.
- Pad slot 0 at 0x82AAB2E8: +0x18 RX, +0x1C RY, +0x20 LX, +0x24 LY as u32 0..255, 127 = centre (dead-zoned).
- **Right-stick camera v1**: config/camera_hooks.toml midasm hook at 0x822AE118 entry -> src/dw5e_camera.cpp dw5e_camera_rstick: rotates view.eye (+0x30) and its copy (+0x50) around focus (r4) in XZ by RX. cvars dw5e_rstick_camera_speed (rad/tick, default 0.05, 0 = off) and dw5e_rstick_camera_invert. Tested 2026-10-12: works; sign negated (Y-down world: positive XZ rotation = stick-right turns the wrong way).
- **Right-stick pitch (2026-10-12)**: game owns eye height (0x822AC7A8 clamps eye.y vs focus/terrain at 0x822ACF78/0x822AD580), so pitch is an overlay: hook 0x822AE1F8 (after 0x822AC7A8 returns) orbits eye vertically around target (+0x40) by an accumulated offset (+-0.7 rad, elevation clamped 0.03..1.3) and recomputes pitch +0x20 with 0x822A9348's formula (-atan2(target.y-eye.y, horiz)); hook at 0x822AE118 entry restores the game's eye/pitch first (only if still the values we wrote). 0x822AE258 entry (snap/L1 path) drops the offset. View matrix 0x822AA558 = eye + basis from angles (0x822A9530), so writing eye+pitch is enough. cvars dw5e_rstick_camera_pitch_speed (default 0 = off since user test: free pitch swings past the character, little value in Musou; 0.03 to experiment), dw5e_rstick_camera_invert_y.
- Pad Y is negated by the game (0x822BA030 `not`): slot+0x1C/+0x24 low = stick pushed up. Push up = camera lower, looking up (non-inverted).
- **Crash run13 (2026-10-12 00:34, in battle)**: FATAL invalid call 0x8213D118 from 0x8217D5C8 (walks units via +8, calls unit+280 callback). Not camera-related. 0x8213D118 is a real 0x10-byte function hidden after the jump-table dispatcher 0x8213D0F8 (no .pdata; pointer built in code with lis/addi, so tools/find_missing_funcs.py, which only scans data, missed it). Scanned code for lis/addi targets that sit mid-function right after blr/bctr/b and are not switch-table data: found 0x8213D118, 0x8213D608 (twin unit callback), 0x8231E208 (vtable-slot-34 thunk). All three added to config/functions.toml with size 0x10; codegen emits them with tail calls to the following function.
