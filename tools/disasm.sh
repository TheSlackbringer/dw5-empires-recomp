#!/usr/bin/env bash
# Disassemble a range of the decrypted image: disasm.sh START_HEX LENGTH_HEX [image]
set -euo pipefail
start=$((16#${1#0x})); len=$((16#${2#0x})); img=${3:-game/default.pe}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
dd if="$img" of="$tmp/blob.bin" bs=1 skip=$((start - 0x82000000)) count=$len status=none
llvm-objcopy -I binary -O elf32-powerpc --rename-section=.data=.text,code "$tmp/blob.bin" "$tmp/blob.elf"
llvm-objdump -d --no-show-raw-insn --triple=powerpc --adjust-vma=$start "$tmp/blob.elf" | grep -E '^\s+[0-9a-f]+:' | sed 's/^\s*/0x/'
