---
name: bsp-engage-headarm-2026-06-19
description: Captured the WORKING BSP executer-engage on l1 (yolov5s) — engage requires arming ALL 5 sub-units' S_POINTER=0xe via regcmd BROADCAST at the head; mesa omits CNA/CMAC/CORE (0x1004/0x2004/0x3004). The concrete missing-regcmd-entry lead.
metadata:
  type: project
---

**🎯 BREAKTHROUGH REFERENCE (l1 BSP yolov5s, 2026-06-19): the working executer-engage signature +
exactly what mesa's regcmd is missing.** Booted l1 (BSP RKNPU built-in, has RKNPU_DUMP_*_OPEN +
PIPE Sstatus sweep + REGCMD_FULL_BSP), ran yolov5s (correct detections = engages+computes), captured:

**Working engage signature (BSP):**
- DUMP_BEFORE_OPEN: CNA `S_POINTER=0`, CORE `S_POINTER=0` — **NOT pre-armed** (S_POINTER starts 0).
- DUMP_AFTER_OPEN: CNA `S_POINTER=0x0e` (S_STATUS 0→**0x5**); **CORE `S_POINTER=0x1000e` (bit16 SET),
  S_STATUS 0→`0x10008` (executer running)**; PC **`TASKST=0x11000` (bit16=DONE)**.
- PIPE Sstatus sweep (IMM/100US/1MS/5MS): **ALL units CNA=CMAC=ACCU=DPU=RDMA = `0x05`**; PERF
  **dt_wr / dt_rd / wt_rd all GROW** (real data movement + compute). `wt_rd`≈0x1170→0x38670 over 5ms.
  ⇒ engage witness = all-units S_STATUS=0x05 + CORE S_POINTER bit16 (0x1000e) + PC TASKST=0x11000 +
  perf counters growing.

**THE MISSING PIECE — BSP regcmd HEAD arms all 5 sub-units' S_POINTER via BROADCAST, mesa omits 3:**
`for_review/bsp-regcmd-yolov5s-20260619-153839.txt`. BSP conv regcmd entries [0..5]:
```
[0] off=0x1004 val=0x0e routing=0x02   (CNA  S_POINTER)   <-- mesa OMITS
[1] off=0x2004 val=0x0e routing=0x04   (CMAC S_POINTER)   <-- mesa OMITS
[2] off=0x3004 val=0x0e routing=0x08   (CORE/ACCU S_POINTER) <-- mesa OMITS
[3] off=0x1040 val=0x17 routing=0x02   (CNA CBUF_CON0)
[4] off=0x4004 val=0x0e routing=0x10   (DPU  S_POINTER)   (mesa HAS)
[5] off=0x5004 val=0x0e routing=0x20   (RDMA S_POINTER)   (mesa HAS)
... then config (CONV_CON1 0x100c=0xa000, etc.)
```
S_POINTER writes appear ONLY at the head. **mesa's regcmd emits only DPU/RDMA (0x4004/0x5004) and
OMITS CNA/CMAC/CORE (0x1004/0x2004/0x3004)** — exactly the "BSP-only offsets mesa doesn't write" from
the 2026-05-19 notes. rocket papers over it with a **CPU pre-write** (pp_init writes S_POINTER=0xe at
resume) — but BSP does it as an **in-stream BROADCAST at the regcmd head** (S_POINTER=0 before, armed
by the PC's broadcast sequence). Even mesa's broadcast-armed DPU/RDMA don't engage on rocket because
the head-arming is INCOMPLETE (CNA/CMAC/CORE missing) → pipeline never starts → NO unit reaches 0x05.

**▶ THE TEST (clean replication, not yet done cleanly amid the 2-yr tangle):** prepend
`{0x1004,0xe,0x02} {0x2004,0xe,0x04} {0x3004,0xe,0x08}` to rocket's regcmd HEAD (broadcast) so all 5
S_POINTER arm at the head like BSP, AND **disable the CPU pre-arm** (pp_init=0, so S_POINTER=0 before,
armed only by the broadcast — matching BSP's BEFORE=0). Then check: all units S_STATUS=0x05? CORE
S_POINTER=0x1000e (bit16)? PC TASKST=0x11000? perf dt_wr/dt_rd/wt_rd growing?
CAVEAT: CNA/CMAC/CORE S_POINTER=0xe broadcast injection was tried before (BSP_INJECT[4], mystery
0x2004) and didn't engage — BUT likely never cleanly: (a) at the HEAD position (BSP slots 0-2),
(b) ALL three together, (c) with the CPU pre-arm DISABLED (CPU pre-write sets sticky bits / blocks PC
per old notes). The BSP reference now pins the exact recipe → worth a clean run.
**Reopens the regcmd-content hypothesis for US** (WTRACE compared CPU PC-writes, not regcmd content;
the missing head-arming entries are in the regcmd payload, invisible to that trace). Witness/reference:
this file + `for_review/bsp-regcmd-yolov5s-*.txt`. Board back l3 baseline. See
[[engage-broadcast-openable-2026-06-18]].

## ✅ HEAD-ARM TEST RUN 2026-06-20 (home, 192.168.1.163) — arming LANDS but NOT the gate
Implemented `head_arm` param (`patch_headarm.py`): PREPENDS CNA/CMAC/CORE S_POINTER=0xe to the regcmd
head via broadcast (memmove shift + insert at p[0..5]). Tested head_arm=1 with pp_init=0 AND pp_init=1:
- HEAD_ARM ran (count 135→138); **arming LANDED**: CNA/ACCU read `S_POINTER=0x0e` from the broadcast.
- **BUT no engage:** units stay `S_STATUS=0` (NOT the BSP's 0x05), no bit16, PC `TASKST=0xf000`
  (stall; BSP=0x11000 DONE), out_sum 26214400 unchanged.
- **⇒ The missing head-arming entries are a real BSP-vs-mesa regcmd diff, but NOT the engage gate.**
  Even with S_POINTER=0xe present (by head-arm broadcast AND/or CPU pp_init), rocket's units never get
  DRIVEN to S_STATUS=0x05 — the PC stalls at 0xf000 and never drives the armed units active. **The gate
  is the PC-stall / units-never-driven-to-active, NOT the arming.** Consistent with the long-standing
  byte-exact-vendor-regcmd-replay-also-fails finding (if the EXACT BSP regcmd doesn't fix it, a subset
  like head-arming won't either). Confound noted (3-entry prepend perturbed DPU/RDMA arming, RDMA→0)
  but "armed-but-S_STATUS=0" is unambiguous.
- **REFINED GATE (precise):** rocket arms units (S_POINTER=0xe present by every method) but PC reaches
  0xf000 (operating/WAIT) and never drives them 0→0x05; BSP's PC reaches 0x11000 (DONE) with units at
  0x05 + perf growing. The 0→0x05 transition (PC driving the armed units to run) is what's missing —
  HW-internal / submission-infra, NOT regcmd content / arming / power / clock (all matched). Board
  restored l3 baseline; head_arm param left gated off. patch_headarm.py local.
