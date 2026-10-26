#!/usr/bin/env python3
"""Find camera-like floats across F9 snapshots.

usage: snapdiff.py snap_001.bin snap_002.bin snap_003.bin snap_004.bin

Expected capture protocol (in battle, standing still between steps):
  1  camera behind character, facing direction A
  2  same, a couple of seconds later (noise filter: must equal 1)
  3  turned ~90 degrees and camera reset behind character (must differ from 1)
  4  turned back to A and camera reset (must be close to 1 again)
Prints big-endian float addresses that match the pattern, grouped by 64-byte block.
"""
import math
import struct
import sys
from collections import defaultdict


def load(path):
    data = open(path, "rb").read()
    assert data[:8] == b"DW5SNAP1", path
    chunks, o = {}, 8
    while o < len(data):
        addr, n = struct.unpack_from("<II", data, o)
        chunks[addr] = data[o + 8:o + 8 + n]
        o += 8 + n
    return chunks


def main():
    snaps = [load(p) for p in sys.argv[1:5]]
    common = set(snaps[0]).intersection(*snaps[1:])
    hits = []
    for addr in sorted(common):
        blocks = [s[addr] for s in snaps]
        if blocks[0] == blocks[2]:
            continue  # nothing changed when the camera turned
        n = len(blocks[0]) // 4
        f = [struct.unpack(f">{n}f", b) for b in blocks]
        for i in range(n):
            a, b, c, d = (x[i] for x in f)
            if not all(math.isfinite(v) and abs(v) < 1e5 for v in (a, b, c, d)):
                continue
            if a != b:                       # noise
                continue
            if abs(c - a) < 1e-3:            # did not change when turning
                continue
            if abs(d - a) > max(0.05, 0.05 * abs(a)):  # did not come back
                continue
            hits.append((addr + 4 * i, a, c, d))
    by_block = defaultdict(list)
    for h in hits:
        by_block[h[0] & ~0x3F].append(h)
    print(f"{len(hits)} candidate floats in {len(by_block)} 64-byte blocks")
    for blk in sorted(by_block, key=lambda b: -len(by_block[b]))[:40]:
        vals = " ".join(f"@{h[0]:08X}:{h[1]:.3f}->{h[2]:.3f}->{h[3]:.3f}" for h in by_block[blk][:4])
        print(f"0x{blk:08X} ({len(by_block[blk])}) {vals}")


if __name__ == "__main__":
    main()
