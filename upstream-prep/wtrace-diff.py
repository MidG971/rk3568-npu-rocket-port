#!/usr/bin/env python3
"""
wtrace-diff.py — diff BSP RKNPU vs rocket CPU->NPU register-write traces.

Both traces log absolute NPU offsets (PC 0x0-based, CNA 0x1000, CMAC 0x2000,
CORE/ACCU 0x3000, DPU 0x4000, DPU_RDMA 0x5000), so they compare directly.

Input formats (either accepted, per line):
  RKNPU_WTRACE off=0x.. val=0x..            (BSP, drivers/rknpu/rknpu_job.c)
  ROCKET_WTRACE <BLOCK> off=0x.. val=0x..   (rocket, our 3 write macros)

Usage:
  ./wtrace-diff.py rocket.log                  # normalize/inspect one trace
  ./wtrace-diff.py --bsp bsp.log --rkt rocket.log   # full diff

The diff answer we want: offsets BSP writes that rocket NEVER writes (the missing
RK3568 init), plus same-offset different-value writes. BSP probe/reset writes
appear BEFORE the first submit; pass --bsp-probe-only to keep only the pre-first-
OP_EN (0x8) writes if the BSP log includes a job.
"""
import re, sys, argparse

BLOCKS = [(0x0000,0x1000,"PC"),(0x1000,0x2000,"CNA"),(0x2000,0x3000,"CMAC"),
          (0x3000,0x4000,"CORE/ACCU"),(0x4000,0x5000,"DPU"),(0x5000,0x6000,"DPU_RDMA")]
NAMES = {0x08:"PC.OP_ENABLE",0x10:"PC.BASE_ADDRESS",0x14:"PC.REG_AMOUNTS",
         0x20:"PC.INT_MASK",0x24:"PC.INT_CLEAR",0x30:"PC.TASK_CON",
         0x34:"PC.TASK_DMA_BASE",0x1000:"CNA.S_STATUS",0x1004:"CNA.S_POINTER",
         0x1008:"CNA.OP_ENABLE",0x1020:"CNA.DATA_SIZE0",0x1024:"CNA.DATA_SIZE1",
         0x3004:"CORE.S_POINTER",0x3008:"CORE.OP_ENABLE"}

def block(off):
    for lo,hi,n in BLOCKS:
        if lo<=off<hi: return n
    return "?"

def label(off):
    return NAMES.get(off, "%s+0x%x"%(block(off), off - next(lo for lo,hi,n in BLOCKS if lo<=off<hi) if block(off)!="?" else off))

LINE = re.compile(r'off=0x([0-9a-fA-F]+)\s+val=0x([0-9a-fA-F]+)')

def parse(path):
    """return ordered list of (off,val) ints."""
    out=[]
    with open(path) as f:
        for ln in f:
            m=LINE.search(ln)
            if m: out.append((int(m.group(1),16), int(m.group(2),16)))
    return out

def one_submit(seq):
    """collapse repeated identical submit cycles -> first cycle.
       a cycle is detected as the run up to and including the 2nd OP_ENABLE(0x8) write."""
    if not seq: return seq
    # find first repetition of the opening offset
    first=seq[0][0]
    for i in range(1,len(seq)):
        if seq[i][0]==first:
            return seq[:i]
    return seq

def show(seq, title):
    print("== %s : %d writes, %d after collapse ==" % (title, len(seq), len(one_submit(seq))))
    for off,val in one_submit(seq):
        print("   %-18s off=0x%-5x val=0x%x" % (label(off), off, val))

def diff(bsp, rkt):
    b=one_submit(bsp); r=one_submit(rkt)
    boff={o for o,_ in b}; roff={o for o,_ in r}
    bmap=dict(b); rmap=dict(r)
    print("\n### BSP-only offsets (rocket never writes — PRIME SUSPECTS for missing init) ###")
    for o in sorted(boff-roff):
        print("   %-18s off=0x%-5x val=0x%x" % (label(o), o, bmap[o]))
    print("\n### same offset, DIFFERENT value ###")
    for o in sorted(boff&roff):
        if bmap[o]!=rmap[o]:
            print("   %-18s off=0x%-5x BSP=0x%x  ROCKET=0x%x" % (label(o), o, bmap[o], rmap[o]))
    print("\n### rocket-only offsets (rocket writes, BSP doesn't) ###")
    for o in sorted(roff-boff):
        print("   %-18s off=0x%-5x val=0x%x" % (label(o), o, rmap[o]))

if __name__=="__main__":
    ap=argparse.ArgumentParser()
    ap.add_argument("logs", nargs="*")
    ap.add_argument("--bsp"); ap.add_argument("--rkt")
    a=ap.parse_args()
    if a.bsp and a.rkt:
        bsp=parse(a.bsp); rkt=parse(a.rkt)
        show(bsp,"BSP "+a.bsp); show(rkt,"ROCKET "+a.rkt)
        diff(bsp,rkt)
    elif a.logs:
        for p in a.logs: show(parse(p), p)
    else:
        ap.print_help()
