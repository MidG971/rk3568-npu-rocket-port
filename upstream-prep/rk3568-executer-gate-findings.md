# RK3568 NPU executer-engage gate — findings summary

Self-contained technical summary for sharing (GitHub issue to @gahingwoo; also Tomeu / Finley Xiao).
All data is public NPU register info (same as in the kernel RFC + Mesa MR). Attach raw decoded
cmdstreams alongside if useful: `mesa_conv2d.decoded.txt`, `bsp_conv2d.decoded.txt`.

## Setup
- HW: Radxa ROCK 3B (RK3568, 8 GB). Stack: mainline **rocket** accel driver + **Mesa Teflon**.
- Workload: `conv2d.tflite` (input 80×80×16 → output 40×40×128, 5×5, stride 2, int8, zero-point 128).
- Reference: vendor `rknpu` driver runs the same conv correctly on the same board (full output).

## Symptom (the gate)
rocket probes, submits, and the CNA reads the full input + weights — but the MAC/output stage
never completes. Specifically, during the (timing-out) job:
- NPU DMA bandwidth counters: `dt_rd=0x328d`, `wt_rd=0x1900` (full input + weights read) but
  **`dt_wr=0`** (DPU writes nothing).
- All five sub-units (CNA/CMAC/ACCU/DPU/DPU_RDMA): `S_STATUS=0x05`, `S_POINTER=0x0e`
  ("executer 0 operating", ping-pong loaded) for the entire ~540 ms, none advances.
- **`S_POINTER` bit 16 (0x10000, HW-set "executer engaged") NEVER sets.** The vendor reaches
  `S_POINTER=0x1000e` / `S_STATUS=0x0c` and writes the full output; rocket never sets bit 16.
- No completion IRQ; `PC_TASKST=0xf000`; IOMMU idle (`ST=0x19`), no fault. Output = uniform
  zero-point (out_sum 26214400 = 204800 × 128).

## Ruled out (extensively)
- **Command-list content**: byte-exact replay of the vendor's regcmd on rocket → still `dt_wr=0`.
- **Every driver MMIO write**: traced all 12 writes the vendor issues per submit and reproduced
  exactly that set in rocket (see below) → still no engage. So it's **not a register write we're
  missing or issuing wrongly**.
- Clocks (CRU clk_npu/aclk_npu now == BSP 200 MHz; SCMI/PVTPLL 1 GHz both), reset (full
  rknpu_soft_reset ported), IOMMU (per-device ops; attach-once), power/NoC de-idle
  (BUS_IDLE_ST=0x1fa), output address (DST_BASE_ADDR 0x4020 = correct register, vendor uses same).

## Vendor RK3568 per-submit MMIO write trace (12 writes, captured live via instrumented rknpu)
```
0x10 = 0x1          (PC_DATA_ADDR slave pre-write)
0x10 = <regcmd iova>(PC_DATA_ADDR)
0x14 = 0x88         (PC_DATA_AMOUNT)
0x20 = 0x10300      (INT_MASK, BIT16 | 0x300)
0x24 = 0x300        (INT_CLEAR)
0x30 = 0x7001       (PC_TASK_CONTROL: task_pp_en=1, task_number=1)
0x34 = 0x0          (PC_DMA_BASE_ADDR)
0x8010 = 0x80000101 (perf-counter clear+enable)
0x8010 = 0x101      (perf-counter enable)
0x08 = 0x1          (PC_OP_EN)
0x08 = 0x0          (PC_OP_EN, pulse off)
0x24 = 0x1ffff      (INT_CLEAR, post-completion)
```
rocket now issues exactly this set (TASK_CON 0x7001, single OP_EN pulse, pc_slave_pre, no extra CPU
S_POINTER writes) → executer still does not engage.

## DPU config comparison (decoded, mesa vs vendor, same conv)
`DST_BASE_ADDR` (output ptr) is the same register in both (each its own output BO) — output
address is correct. Remaining mesa-side value bugs (for correct output, separate from the gate):
| reg | mesa | vendor |
|-----|------|--------|
| DST_SURF_STRIDE | 1600 | 800 (mesa 2×) |
| SURFACE_ADD | 3200 | 1600 (mesa 2×) |
| BS_CFG | BS_ALU_ALGO(2) | (sets a different bit) |
| BS_OW_OP | 0x0000fffb | 0xfffffffb (sign-extended) |
| OUT_CVT_SCALE | 21285 | 21284 (rounding) |

## The question
What "arms" the executer (makes `S_POINTER` bit 16 set, the MAC compute → DPU write start) that is
**not** a value in the command stream nor one of the driver's MMIO register writes? On RK3576 the
vendor rknpu config has a `state_init` (pokes 0x1004/0x1024/0x1e) that RK3568's leaves NULL and
mainline rocket doesn't do — is that the executer-arm, and does RK3568 need an equivalent?
