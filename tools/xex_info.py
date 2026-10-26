#!/usr/bin/env python3
"""Print the XEX2 headers that matter for a recomp port.

usage: xex_info.py default.xex [--imports]
"""
import struct
import sys

COMPRESSION = {0: "none", 1: "basic (zero-run)", 2: "normal (LZX)", 3: "delta"}
ENCRYPTION = {0: "none", 1: "AES (retail/devkit key)"}


def be32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def main():
    path = sys.argv[1]
    show_imports = "--imports" in sys.argv
    x = open(path, "rb").read()
    assert x[:4] == b"XEX2", "not an XEX2 file"
    module_flags, pe_offset, _, sec_offset, count = struct.unpack_from(">IIIII", x, 4)
    headers = {}
    for i in range(count):
        key, val = struct.unpack_from(">II", x, 24 + i * 8)
        headers[key] = val
    print(f"module flags      0x{module_flags:08X}  PE data at 0x{pe_offset:X}")

    if 0x00010100 in headers:
        print(f"entry point       0x{headers[0x00010100]:08X}")
    if 0x00010201 in headers:
        print(f"image base        0x{headers[0x00010201]:08X}")
    image_size = be32(x, sec_offset + 4)
    print(f"image size        0x{image_size:X} ({image_size / 1048576:.1f} MB)")

    if 0x000003FF in headers:
        o = headers[0x000003FF]
        enc, comp = struct.unpack_from(">HH", x, o + 4)
        print(f"encryption        {ENCRYPTION.get(enc, enc)}")
        print(f"compression       {COMPRESSION.get(comp, comp)}")

    if 0x000183FF in headers:
        o = headers[0x000183FF]
        n = be32(x, o)
        print(f"original PE name  {x[o + 4:o + n].split(b'\\0')[0].decode()}")

    if 0x00040006 in headers:
        o = headers[0x00040006]
        media, ver, base_ver, title = struct.unpack_from(">IIII", x, o)
        def v(n):
            return f"{n >> 28}.{(n >> 24) & 0xF}.{(n >> 8) & 0xFFFF}.{n & 0xFF}"
        print(f"title id          {title:08X}  version {v(ver)}  disc {x[o + 18]}/{x[o + 19]}")

    if 0x00020104 in headers:
        o = headers[0x00020104]
        slots, raw_addr, data_size, raw_size = struct.unpack_from(">IIII", x, o)
        print(f"TLS               {slots} slots, raw data 0x{raw_addr:08X} ({raw_size} bytes)")

    if 0x00020200 in headers:
        print(f"default stack     0x{headers[0x00020200]:X}")

    if 0x000200FF in headers:
        o = headers[0x000200FF]
        n = be32(x, o)
        print("\nstatic libraries (linked XDK/middleware):")
        for e in range(o + 4, o + n, 16):
            name = x[e:e + 8].split(b"\0")[0].decode()
            major, minor, build, flags = struct.unpack_from(">HHHH", x, e + 8)
            print(f"  {name:<10} {major}.{minor}.{build}.{flags & 0xFF}")

    if 0x000103FF in headers:
        o = headers[0x000103FF]
        size, str_size, lib_count = struct.unpack_from(">III", x, o)
        names = [s.decode() for s in x[o + 12:o + 12 + str_size].split(b"\0") if s]
        e = o + 12 + str_size
        print("\nimported system libraries:")
        for _ in range(lib_count):
            lsize = be32(x, e)
            lib_id, lib_ver, lib_min = struct.unpack_from(">III", x, e + 24)
            name_idx = x[e + 37]  # byte at +36 is 0xFF on this title; the index is the low byte
            records = struct.unpack_from(">H", x, e + 38)[0]
            print(f"  {names[name_idx]:<16} {records:>4} import records")
            if show_imports:
                for r in range(records):
                    print(f"      0x{be32(x, e + 40 + r * 4):08X}")
            e += lsize


if __name__ == "__main__":
    main()
