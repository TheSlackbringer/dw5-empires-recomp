// Game pacing on shared CPU cores (issue 006 v3, config/pacing_hooks.toml).
//
// Part A: the game waits for the GPU by spinning in D3D_BlockOnFence 0x822D2FE0 until the GPU's
// progress value passes a fence (0x822DAB38: a short delay loop and a check, no kernel call). On
// the Xbox 360 the GPU is separate hardware; here it is the runtime's command processor thread,
// which writes progress back only while it runs. When both threads share a core the spin keeps
// the core from the thread it waits for, until the OS time slice ends (1 core in battle: 25 game
// frames/s, issue 008 counter: 2.7 ms work, 38 ms fence wait). Each spin round therefore offers
// the core to any ready thread; with nothing else ready the yield returns at once. The game's
// registers, memory and checks are untouched.

#include <rex/thread.h>

void dw5e_fence_spin_yield() { rex::thread::MaybeYield(); }
