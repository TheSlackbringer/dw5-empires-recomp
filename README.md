# Dynasty Warriors 5 Empires — Native Port

Static recompilation of **Dynasty Warriors 5: Empires** (Xbox 360 PAL, Title ID `4B4F07D3`) to a native Linux and Windows executable using **ReXGlue** and Vulkan `xenos` GPU plugin.

## Prerequisites
- Linux (x86_64, SSSE3+) or Windows
- CMake 3.25+
- Clang / Clang++ (LLVM 18+)
- Vulkan SDK and drivers
- Legally owned retail disc of Dynasty Warriors 5: Empires (Xbox 360 Europe, XGD2)

## SDK Setup
Clone and patch the ReXGlue SDK (v0.10.0 with Koei Musou event/audio fixes):
```bash
./tools/setup_sdk.sh
```

## Building
```bash
# Configure preset (RelWithDebInfo)
cmake --preset linux-amd64-relwithdebinfo -S app

# Build executable
cmake --build app/out/build/linux-amd64-relwithdebinfo -j$(nproc)
```

## Running
```bash
cd app/out/build/linux-amd64-relwithdebinfo
./dw5e --gpu_plugin=xenos \
       --game_data_root=/path/to/extracted/game/files \
       --log_file=run.log
```

## Game Data Notice
No copyrighted game assets, executables, disc images, or game-derived decompiled code are distributed in this repository. The user must provide their own legally dumped retail disc.
