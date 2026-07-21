#!/usr/bin/env python3
"""Reconstruct a byte-exact regcmd.bin from a REGCMD_FULL_BSP text dump.

64-bit regcmd word (little-endian), derived from two confirmed data points:
  entry off=0x1040 val=0x44   routing=0x02 -> pair low=0x00441040 high=0x02010000
  entry off=0x1070 val=0xffee0000 routing=0x02 -> pair low=0x00001070 high=0x0201ffee
=> word64 = (routing<<56) | (1<<48) | ((val & 0xffffffff)<<16) | (off & 0xffff)
The (1<<48) bit matches the historical "odd 32-bit word = 0x02010000" pattern; VALIDATE this
against a /dev/mem byte-exact dump on l1 before trusting a negative replay result.

Usage: regcmd_from_dump.py conv2d-bsp-cmdstream.txt conv2d-regcmd.bin
"""
import re, sys, struct

LINE = re.compile(r'REGCMD_FULL_BSP\[(\d+)\]:\s*off=0x([0-9a-fA-F]+)\s+val=0x([0-9a-fA-F]+)\s+routing=0x([0-9a-fA-F]+)')

def main(src, dst):
    entries = {}
    for ln in open(src):
        m = LINE.search(ln)
        if m:
            idx = int(m.group(1))
            off = int(m.group(2), 16)
            val = int(m.group(3), 16)
            rt  = int(m.group(4), 16)
            entries[idx] = (off, val, rt)
    n = max(entries) + 1
    out = bytearray()
    for i in range(n):
        if i not in entries:
            print("MISSING entry %d" % i); sys.exit(1)
        off, val, rt = entries[i]
        word = (rt << 56) | (1 << 48) | ((val & 0xffffffff) << 16) | (off & 0xffff)
        out += struct.pack('<Q', word)
    open(dst, 'wb').write(out)
    print("wrote %d entries (%d bytes) -> %s" % (n, len(out), dst))
    # report address-bearing entries (value looks like a vendor IOVA >= 0xffe00000)
    print("address-bearing entries (patch these for replay):")
    for i in range(n):
        off, val, rt = entries[i]
        if val >= 0xffe00000:
            print("  [%d] byte_off=%d  off=0x%04x  val=0x%08x  routing=0x%02x" % (i, i*8, off, val, rt))

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(1)
    main(sys.argv[1], sys.argv[2])
