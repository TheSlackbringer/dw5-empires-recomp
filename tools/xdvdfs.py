#!/usr/bin/env python3
"""Minimal XDVDFS (Xbox/Xbox 360 game disc) reader.

usage: xdvdfs.py ISO list
       xdvdfs.py ISO extract PATH OUTFILE
       xdvdfs.py ISO extract-all OUTDIR
"""
import struct
import sys

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# Game partition offsets: plain XISO, XGD3, XGD2, XGD1
PARTITION_OFFSETS = [0, 0x2080000, 0xFD90000, 0x18300000]


def find_partition(f):
    for base in PARTITION_OFFSETS:
        f.seek(base + 32 * SECTOR)
        if f.read(len(MAGIC)) == MAGIC:
            return base
    raise SystemExit("no XDVDFS volume descriptor found")


def read_dir(f, base, sector, size):
    """Walk the binary-tree directory table; yield (name, start_sector, size, is_dir)."""
    f.seek(base + sector * SECTOR)
    table = f.read(size)
    seen, stack = set(), [0]
    while stack:
        off = stack.pop()
        if off in seen or off + 14 > len(table):
            continue
        seen.add(off)
        left, right, start, length, attr, nlen = struct.unpack_from("<HHIIBB", table, off)
        if left == 0xFFFF and right == 0xFFFF and start == 0xFFFFFFFF:
            continue  # padding
        name = table[off + 14:off + 14 + nlen].decode("ascii", "replace")
        yield name, start, length, bool(attr & 0x10)
        if left:
            stack.append(left * 4)
        if right:
            stack.append(right * 4)


def walk(f, base, sector, size, prefix=""):
    for name, start, length, is_dir in sorted(read_dir(f, base, sector, size)):
        path = f"{prefix}/{name}"
        yield path, start, length, is_dir
        if is_dir and length:
            yield from walk(f, base, start, length, path)


def main():
    iso, cmd = sys.argv[1], sys.argv[2]
    with open(iso, "rb") as f:
        base = find_partition(f)
        f.seek(base + 32 * SECTOR + len(MAGIC))
        root_sector, root_size = struct.unpack("<II", f.read(8))
        entries = list(walk(f, base, root_sector, root_size))
        if cmd == "list":
            print(f"# partition offset 0x{base:X}")
            for path, start, length, is_dir in entries:
                print(f"{'d' if is_dir else '-'} {length:>12} {path}")
        elif cmd == "extract-all":
            import os
            out_root = sys.argv[3]
            for path, start, length, is_dir in entries:
                dest = os.path.join(out_root, path.lstrip("/"))
                if is_dir:
                    os.makedirs(dest, exist_ok=True)
                    continue
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                f.seek(base + start * SECTOR)
                with open(dest, "wb") as o:
                    remaining = length
                    while remaining:
                        chunk = f.read(min(remaining, 16 << 20))
                        o.write(chunk)
                        remaining -= len(chunk)
            print(f"extracted {sum(1 for e in entries if not e[3])} files to {out_root}")
        elif cmd == "extract":
            want, out = sys.argv[3], sys.argv[4]
            for path, start, length, is_dir in entries:
                if path.lower() == want.lower() and not is_dir:
                    f.seek(base + start * SECTOR)
                    with open(out, "wb") as o:
                        o.write(f.read(length))
                    print(f"extracted {path} ({length} bytes) -> {out}")
                    return
            raise SystemExit(f"not found: {want}")


if __name__ == "__main__":
    main()
