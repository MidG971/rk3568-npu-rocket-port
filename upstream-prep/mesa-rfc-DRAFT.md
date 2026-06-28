# Mesa rocket: RK3568 NPU regcmd fixes (RFC DRAFT)

To: mesa-dev@lists.freedesktop.org
Cc: Tomeu Vizoso <tomeu@tomeuvizoso.net>
Subject: [RFC] rocket: regcmd fixes for RK3568 NPU support

## Summary

Add RK3568 NPU support to `src/gallium/drivers/rocket/`. The Rocket driver
currently targets RK3588; RK3568 has the same NVDLA-derived IP but differs
in register offsets and several encoded values. This RFC describes empirical
findings from 12 days of bisecting against the vendor BSP RKNPU driver on a
Radxa ROCK 3B board, accompanied by kernel-side rocket driver work (separate
patch series).

I'm sending as an RFC because the final piece — actually firing conv
execution — is not yet solved despite the NPU's MMIO state matching BSP
byte-for-byte. Multiple findings here are empirically verified and should
help anyone else porting rocket to RK356x.

## Methodology

- Captured BSP RKNPU regcmd byte-by-byte during working YOLOv5s and
  MobileNet inferences (instrumented `rknpu_job.c::rknpu_job_subcore_commit_pc`
  with kmap_local_page + flush_dcache_page).
- Captured rocket's regcmd same way for direct diff.
- All register reads verified at AT10MS after PC OP_EN pulse via separate
  kernel-side ioremap diagnostics.

## Finding 1: Sub-unit OP_ENABLE register offset is 0x_00c, not 0x_008

Mesa's current `registers.xml` defines OPERATION_ENABLE at offset 0x_008
for each sub-unit. BSP regcmd captures (across two distinct conv shapes)
show NO writes at any 0x_008 offset for any sub-unit. The actual OP_ENABLE
registers are at 0x_00c with multi-bit values, not boolean bit-0 flags.

| Sub-unit  | Mesa offset | BSP offset | BSP value |
|-----------|-------------|------------|-----------|
| CNA       | 0x1008      | (none — PC manages internally) | n/a |
| CMAC      | (not emitted) | 0x200c   | 0x1 |
| ACCU      | 0x3008      | 0x300c     | 0x0 (yes, zero) |
| DPU       | 0x4008      | 0x400c     | 0x108 |
| DPU_RDMA  | 0x5008      | 0x500c     | 0x13f (YOLOv5s) / 0x6f (MobileNet) |

For DPU, 0x108 encodes BURST_LEN(2) | OUTPUT_MODE(4) — the existing patch
captured this correctly. For the others, mesa's current code is writing
to the wrong register.

CNA has no software OP_ENABLE: PC manages CNA's pipeline state internally.
The corresponding emit can be dropped.

**Files affected**: `registers.xml` (4 reg32 offset changes), `rkt_regcmd.c`
(remove CNA emit, add CMAC emit, change others' offsets).

**Verification**: with kernel-side regcmd patching that rewrites 0x_008
entries to 0x_00c with these values, the affected MMIO registers
correctly land BSP values at AT10MS readback. Without this patch,
the registers stay at HW defaults.

## Finding 2: CONV_CON1 = 0xa000 missing from emit path

BSP writes `0x100c = 0xa000` (CONV_CON1) — constant across YOLOv5s
(6×6 stride 2 input 640×640×3) AND MobileNet (3×3 stride 2 input
224×224×3). Mesa writes 0.

This appears to be a conv-mode enable register required for CNA's pipeline
to engage. Adding the emit unblocks CNA's ping-pong group swap (visible at
MMIO: CONV_CON2/CONV_CON4/DATA_SIZE registers all become readable after
this is set).

## Finding 3: CBUF_CON0 wrong value

BSP writes `0x1040 = 0x17` (constant across both BSP samples). Mesa writes
0x44, which is the HW default. The 0x17 likely encodes CBUF mode/size
flags. RK3568's CBUF is 256 KiB (8 banks × 32 KiB) vs RK3588's 384 KiB
(12 banks × 32 KiB), so the encoded value differs from RK3588.

## Finding 4: TASK_CON.task_number must be ≥ 6 on RK3568

Mesa sets `task_number = 1`. PC's task-counter state machine stalls at
`TASKST = 0xf000` indefinitely. With `task_number = 6` (BSP MobileNet
value) or `task_number = 0x12f` (BSP YOLOv5s), PC advances to
`TASKST = 0x11000` (BSP "completed" state) within 1 ms.

PC has two state machines: the task counter (gated by `task_number`)
reaches "all tasks done" mechanically with task_number ≥ 6; sub-unit
activation is gated by something else. RK3588 may not have this gate
since its `task_number` semantics may differ.

## Finding 5: DATA_SIZE1 format constant across shapes

BSP YOLOv5s and BSP MobileNet both write `0x1024 = 0x00020008` — identical
value despite very different conv shapes. Mesa derives a shape-specific
value `(input_channels << 16) | (input_channels + 1)`, producing 0xf0010
for 16-channel inputs. This is likely a fixed-config register, not a
shape-derived one.

## Finding 6: DATA_SIZE3 near-constant

BSP YOLOv5s `0x102c = 0x1b80`, BSP MobileNet `0x102c = 0x1b90`. Mesa writes
`0x640` (1600). Likely a cycle-budget or pipeline-timing register
dominated by a fixed value.

## Open question to maintainer

After applying all the above findings via kernel-side regcmd patching
(byte-for-byte BSP-equivalent MMIO state, zero IOMMU page faults, PC
reaching BSP completed state), conv execution still does not fire:
sub-unit `0x_008` run-status registers stay 0, no IRQ fires, output is
uniform zero_point.

We've confirmed:
- PC IS broadcasting the regcmd (test: pre-write DPU OP_EN=0 from CPU,
  observe PC's broadcast set it back during execution)
- CNA's ping-pong group swap DOES fire (configs visible after broadcast)
- All sub-unit OP_ENABLE registers hold BSP values
- All conv shape configs match what BSP would emit for our shape

Is there a known RK3568-specific trigger for sub-unit activation we
might be missing? Some chip-wide enable register outside the
fde40000.npu MMIO region? A required uCode/firmware load? A specific
clock or reset sequence after the regcmd is consumed?

The behavior is reproducible and tooling for further bisection is in
place; happy to test specific hypotheses.

## Related kernel work

Submitted as separate series [link to v2 rocket-rk3568 series]. Most
relevant: the kernel driver implements the BSP task-descriptor BO
architecture (PC_DMA_BASE_ADDR points to a kernel-allocated array of
`struct rknpu_task` descriptors). Mesa does not need to know about this.

## Files affected (rough estimate)

- `src/gallium/drivers/rocket/registers.xml`: ~5 offset/value changes
- `src/gallium/drivers/rocket/rkt_regcmd.c`: drop CNA OP_EN emit, add
  CMAC emit, change offsets for ACCU/DPU/DPU_RDMA, add CONV_CON1=0xa000
  emit, change CBUF_CON0 to 0x17, change task_number to 6

I'm preparing actual code patches based on this RFC; sending the
findings first for maintainer feedback on which fixes belong in this
patch series vs follow-on work.

---

## Notes for ourselves (not part of email)

Status of this RFC document:
- Empirically verified findings only
- Sub-unit activation problem is openly disclosed
- Specific testable hypotheses requested back from maintainer
- Clear separation between this mesa work and the kernel rocket series

When sending:
1. Verify mesa-dev posting protocol
2. Format for plain text email (no markdown)
3. Send to mesa-dev + Tomeu directly
4. Attach test artifacts (BSP regcmd captures, rocket regcmd captures)
   as text files if list policy allows
