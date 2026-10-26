#!/usr/bin/env python3
"""Decrypt + decompress a retail XEX2 (AES, basic compression) to its PE image.

usage: xex_decrypt.py default.xex out.pe
Uses the openssl CLI for AES (no Python crypto module needed).
"""
import struct
import subprocess
import sys

RETAIL_KEY = "20B185A59D28FDC340583FBB0896BF91"
DEVKIT_KEY = "00000000000000000000000000000000"


def aes(data, key_hex, mode):
    return subprocess.run(
        ["openssl", "enc", "-d", f"-aes-128-{mode}", "-K", key_hex, "-iv", "0" * 32, "-nopad"],
        input=data, capture_output=True, check=True).stdout


def main():
    xex_path, out_path = sys.argv[1], sys.argv[2]
    x = open(xex_path, "rb").read()
    _, pe_offset, _, sec_offset, count = struct.unpack_from(">IIIII", x, 4)
    headers = dict(struct.unpack_from(">II", x, 24 + i * 8) for i in range(count))

    ffi = headers[0x000003FF]
    info_size, enc, comp = struct.unpack_from(">IHH", x, ffi)
    if comp != 1:
        raise SystemExit(f"compression type {comp} not supported (only basic)")
    blocks = [struct.unpack_from(">II", x, o) for o in range(ffi + 8, ffi + info_size, 8)]

    image_size = struct.unpack_from(">I", x, sec_offset + 4)[0]
    file_key = x[sec_offset + 0x150:sec_offset + 0x160]
    payload = x[pe_offset:]
    payload = payload[:len(payload) - len(payload) % 16]

    for name, key in (("retail", RETAIL_KEY), ("devkit", DEVKIT_KEY)):
        session = aes(file_key, key, "ecb").hex() if enc else None
        data = aes(payload, session, "cbc") if enc else payload
        image = bytearray()
        pos = 0
        for data_size, zero_size in blocks:
            image += data[pos:pos + data_size]
            image += bytes(zero_size)
            pos += data_size
        if image[:2] == b"MZ":
            image = image[:image_size] + bytes(max(0, image_size - len(image)))
            open(out_path, "wb").write(image)
            print(f"{name} key OK: wrote {len(image)} bytes to {out_path}")
            return
    raise SystemExit("decryption failed (no MZ header with retail or devkit key)")


if __name__ == "__main__":
    main()
