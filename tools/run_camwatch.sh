#!/usr/bin/env bash
# Launch dw5e under gdb (batch) with the camera-writer logger (tools/camwatch.py).
cd "$HOME/Projects/dw5e-port/app/out/build/linux-amd64-relwithdebinfo" || exit 1
exec gdb -q -batch -x "$HOME/Projects/dw5e-port/tools/camwatch.py" \
  -ex "tbreak sub_822A9700" -ex run -ex camwatch -ex continue --args ./dw5e \
  --gpu_plugin=xenos --game_data_root="$HOME/Projects/dw5e-port/game/files" \
  --log_file="$HOME/Projects/dw5e-port/notes/run11.log"
