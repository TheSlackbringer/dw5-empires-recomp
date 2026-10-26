#!/usr/bin/env python3
"""Find code that materializes a 32-bit constant with lis + addi/ori/lwz/stw (PowerPC).

usage: find_const_refs.py default.pe 0x82092C18 [0x...]
Prints each site and the .pdata function that contains it.
"""
import bisect
import struct
import sys

BASE = 0x82000000
TEXT = (0x820B0000, 0x824A8A34)


def main():
    img = open(sys.argv[1], "rb").read()
    targets = [int(t, 16) for t in sys.argv[2:]]
    w = lambda a: struct.unpack_from(">I", img, a - BASE)[0]

    pd = []
    for o in range(0x95400, 0x95400 + 0x17AE8, 8):
        b, p = struct.unpack_from(">II", img, o)
        if b:
            pd.append((b, b + ((p >> 8) & 0x3FFFFF) * 4))
    pd.sort()
    starts = [b for b, _ in pd]

    def owner(a):
        i = bisect.bisect_right(starts, a) - 1
        return pd[i][0] if i >= 0 and pd[i][0] <= a < pd[i][1] else None

    for target in targets:
        lo = target & 0xFFFF
        hi = (target >> 16) + (1 if lo & 0x8000 else 0)  # addi sign-extends
        hi_ori = target >> 16
        print(f"== 0x{target:08X}")
        lis_regs = {}
        for a in range(TEXT[0], TEXT[1], 4):
            ins = w(a)
            op = ins >> 26
            rd = (ins >> 21) & 31
            ra = (ins >> 16) & 31
            imm = ins & 0xFFFF
            if op == 15 and ra == 0:  # lis rd, imm
                lis_regs[rd] = (imm, a)
                continue
            if op in (14, 24, 32, 36) and ra in lis_regs:  # addi / ori / lwz / stw rX, imm(ra)
                hi_val, lis_at = lis_regs[ra]
                if a - lis_at > 0x80:
                    continue
                want_hi = hi_ori if op == 24 else hi
                if hi_val == want_hi and imm == lo:
                    kind = {14: "addi", 24: "ori", 32: "lwz", 36: "stw"}[op]
                    f = owner(a)
                    print(f"  0x{a:08X} {kind:<4} (lis @0x{lis_at:08X})  in function "
                          + (f"0x{f:08X}" if f else "<not in .pdata>"))
            if op == 18 or ins == 0x4E800020:  # branch / blr: forget stale lis
                lis_regs.clear()


if __name__ == "__main__":
    main()
