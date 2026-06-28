# Register diff: cold vs primed NPU (rocket probe, l8) — 2026-06-24

Method: kernel-side PRIMEDIFF dump (rocket probe, ioremap, no /dev/mem) of 53 NPU regs.
COLD = modprobe rocket (no prime). PRIMED = rknpu + conv2d-bsp job → rmmod → modprobe rocket.

## Deltas (cold → primed) = what the vendor job LEFT
PC:   0x0014 AMOUNTS 0→0x88 ; 0x0020 INT_MASK 0x8001ffff→0x10300 ; 0x0030 TASK_CON 0→0x1001
CNA:  0x1000 S_STATUS 0→0x08 ; 0x1004 S_POINTER 0x0e→0x1000e(bit16) ; 0x1010 0→0x70 ; 0x1014 0→0x12 ;
      0x1020 DATA_SIZE0 0→0x500050 ; 0x1028 0→0x28 ; 0x102c 0→0x640 ; 0x1030 WEIGHT_SIZE0 0→0xc800 ;
      0x1034 0→0x190 ; 0x1038 0→0x05050080 ; 0x1044 CBUF_CON1 0→0x500028
CMAC: 0x2000 0→0x0c ; 0x2004 0→0x1000e(bit16)
CORE: 0x3000 0→0x0c ; 0x3004 0→0x1000e(bit16)
DPU:  0x4000 0→0x0c ; 0x4004 0→0x1000e(bit16)
RDMA: 0x5000 0→0x0c ; 0x5004 0→0x1000e(bit16)
perf: 0x8034 dt_wr 0→0x6400 ; 0x8038 dt_rd 0→0x32c9 ; 0x803c wt_rd 0→0x1900

## Interpretation
LOAD-BEARING = all 5 units in ENGAGED HW state (S_POINTER bit16, S_STATUS=0x0c/0x08), persisting from
the vendor job (NPU powered). These are HW-SET bits (bit16=EXECUTER, set when a unit RUNS; not
CPU-writable). Config (0x10xx) + perf = leftover job state rocket overwrites anyway.
⇒ NO simple register-write fix; the prime works because the vendor job physically engages the
executers and rocket inherits the state.

## Open question / next
Is bit16 itself the lever (does the engaged state survive rocket's per-job reset, and is that what lets
rocket re-engage?), or something deeper? Test: (1) does the prime survive MULTIPLE rocket jobs / NPU
idle (= one boot-time prime suffices = workaround)? (2) does rocket's job reset the units (clear bit16)
before re-engaging — if yes, the enabler is deeper than bit16. (3) the DEEP mechanism: why vendor
first-job engages from cold, rocket's doesn't (Method B: same cmdstream → runtime-env difference).
