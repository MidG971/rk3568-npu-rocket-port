---
name: NPU Debugging Insights
description: Key technical insights from Rocket driver NPU debugging on RK3568
type: project
originSessionId: 7a1c1119-2b38-4f43-bef7-82226bda1f75
---
## NPU AXI Bus Contention Rule
**Never read CNA/CORE MMIO registers while sub-units are executing.** The NPU AXI interconnect has no timeout for MMIO reads — if CNA/CORE is mid-DMA, CPU reads to those sub-units stall indefinitely → hard lockup. PC MMIO reads are safe while PC is idle after broadcasting regcmd, but not while PC is actively broadcasting.

**Why:** RK3568 NPU uses a shared AXI bus. CPU and NPU sub-units compete as AXI masters to the same register space. When sub-unit DMA is in-flight, bus arbitration stalls CPU reads.

**How to apply:** Add explicit comments near any NPU MMIO read in driver code: "safe only when [sub-unit] is idle." Never add debug readbacks after writing OP_EN without confirming the sub-unit is idle first.

---

## TASK_PP_EN Bug (Rocket driver, RK3568)
**Do NOT set TASK_PP_EN (bit 12) in PC_TASK_CON for RK3568.** BSP uses `(0x6 << 12) | 1 = 0x6001` which sets bits {0,13,14}. Rocket driver had `PC_TASK_CON_TASK_PP_EN(1)` which adds bit 12, giving 0x7001.

**Why:** TASK_PP_EN enables ping-pong (double-buffer) task mode. In ping-pong mode the PC waits for a second task buffer that never arrives, silently preventing sub-units from starting. BSP's `0x6` encodes bits 13+14, not bit 12. For RK3568 `pc_task_number_bits=12` (BSP config), so the "mode" field is at bits 13+14, not at bit 12.

**How to apply:** In `rocket_job.c`, use `PC_TASK_CON_RESERVED_0(1) | PC_TASK_CON_TASK_COUNT_CLEAR(1) | PC_TASK_CON_TASK_NUMBER(1)` = 0x6001. Never add TASK_PP_EN for single-task inference.

---

## OP_EN Pulse: Do NOT Add for Rocket Driver
**Do NOT write `PC_OP_EN=0` immediately after `PC_OP_EN=1`.** BSP does this pulse because BSP's `REG_WRITE` has no DSB barrier. Rocket's `rocket_pc_writel` uses `writel` which includes a full memory barrier (DSB). Back-to-back writes with DSB in between complete so fast the PC hardware sees the pulse as 0 and never starts.

**Why:** PC OP_EN is edge-triggered (0→1 transition starts it). With Rocket's DSB barriers, writing 1 then immediately 0 completes before the PC hardware samples the line — hardware sees the final state (0) rather than the edge. Without barrier (BSP), the AXI write buffer may pipeline the pair differently, and the edge is visible to hardware.

**How to apply:** Single `writel(1)` to OP_EN is correct for the Rocket driver. The BSP pulse pattern must NOT be copied verbatim.

---

## BSP vs Rocket: PC Register Write Sequence Comparison
BSP sequence (RKNPU_OFFSET_*):
1. Write DATA_ADDR = regcmd IOVA (offset 0x10)
2. Write DATA_AMOUNT = (count + EXTRA_AMOUNT + scale - 1) / scale - 1 (offset 0x14)
3. Write TASK_CONTROL = 0x6001 for RK3568 (offset 0x30)
4. Write DMA_BASE_ADDR = task_base_addr (offset 0x34)
5. Write OP_EN = 1 (offset 0x08)
6. Write OP_EN = 0 (offset 0x08) ← pulse, omit in Rocket

Rocket sequence: same offsets confirmed. Key difference: EXTRA_AMOUNT=4 in BSP (reads 4 extra entries beyond regcmd_count). Whether this is needed for RK3568 Rocket: NOT YET CONFIRMED.

---

## REGCMD_TAIL Offset Bug (fixed)
Previous code used `start = regcmd_count - 8` as a 32-bit word index. Correct: `total_words = regcmd_count * 2; start = max(0, total_words - 16)`. Each regcmd entry is 64-bit (2 × 32-bit words); the tail of 8 entries = last 16 32-bit words.

---

## Crash from phys_to_virt in REGCMD_TAIL Dump
The REGCMD_TAIL dump uses `iommu_iova_to_phys` + `phys_to_virt` to read regcmd from DRAM. `phys_to_virt` works for lowmem (sub-4GB) physical addresses on a 32-bit PA mapping. For highmem pages it would return an invalid pointer. Confirmed working when regcmd PA is in lowmem range (0x1592000 etc.).

---

## POST500US Safety Rule
After writing PC_OP_EN=1, wait 500µs then ONLY read PC INTERRUPT_RAW_STATUS (PC is idle by then). Never read CNA/CORE registers in POST500US debug — if PC DMA successfully delivered OP_EN to sub-units, they are executing and their AXI reads will stall the CPU.

**Evidence:** Boot with TASK_PP_EN bug: PC_RS=0 at 500µs (PC didn't start). Boot with TASK_PP_EN fixed: crash during POST500US CNA read (CNA is running).

---

## REGCMD Address Mapping (conv2d model, count=133)
The regcmd entries use NPU-MMIO-relative addresses. Sub-unit OP_EN writes observed at the tail:
- `0x00011008` = CNA_OPERATION_ENABLE (CNA base at NPU+0x1000, OP_EN at +0x08)
- `0x00013008` = CORE_OPERATION_ENABLE (CORE base at NPU+0x3000)
- `0x00015008` = DPU/PDP at NPU+0x5000
- `0x00014008` = another sub-unit at NPU+0x4000

**Confirmed**: The last 4 regcmd entries (positions 129–132 of 133) contain sub-unit OP_EN writes. PC DMA, if it ran, WOULD broadcast these to CNA/CORE/DPU to start execution.

**Current problem (BUG A)**: PC_RAW_STATUS=0 at 500µs. PC DMA never starts even though OP_EN=1 is written. Regcmd content is correct; the problem is upstream — PC not initiating DMA.

---

## TRM Chapter 9 PC Register Findings (Apr 25 — from RK3568 TRM Part2 p411-414)

### PC_addr (0x10)
- bits[31:3] = base_addr (8-byte aligned DMA instruction address)
- bits[2:1] = RO reserved
- bit[0] = **pc_sel**: 0 = PC/DMA mode, 1 = Slave/AHB mode
- Reset value = 0x0 → pc_sel=0 by default
- **TRM explicit**: "Pc_sel must set to 0, to enable op_en."
- Writing page-aligned IOVA → bit[0]=0 → pc_sel=0 ✓

### PC_interrupt_mask (0x20)
- bits[15:0] = int_mask, **set 1 to ENABLE interrupt** (not suppress)
- mask[8] = Dpu group 0, mask[9] = Dpu group 1
- mask[12] = Dma read error, mask[13] = Dma write error
- "In pc mode, int mask set the last one task's interrupt masking"
- Reset value = 0x0000 (all disabled)

### PC_task_con (0x30)
- bit[14] W1C: task_last_layer_clear — clear before task start ✓
- bit[13] W1C: task_count_clear — clear before task start ✓
- bit[12] RW: task_pp_off — 1=ping-pong ON, 0=ping-pong OFF
- bits[11:0] RW: task_number — total tasks to execute
- 0x6001 = bits[14:13] clear + task_number=1 + pp OFF ✓

### PC_task_dma_base_addr (0x34)
- DMA base for feature/weight/DPU/PPU DMAs: final_addr = base_addr + offset
- Writing 0 means sub-unit DMA addresses in regcmd are absolute IOVAs ✓

### PC_task_status (0x3C)
- bits[11:0]: current task counter
- bit[12]: first task operating / register is fetching
- bit[13]: last task operating / register is fetching
- TASKST=0x7000 (bits 12+13+14 set) BEFORE OP_EN = task loaded, both-task bits set (single task = first=last)

### Regcmd 64-bit entry format (from PC_amount description)
- bit[63:48]: block routing — bit[56]=pc, bit[57]=cna, bit[58]=cmac, bit[59]=accu, bit[60]=dpu, bit[61]=dpu_rdma, bit[62]=ppu, bit[63]=ppu_rdma, bit[55]=set block's op_en
- bit[47:16]: register value
- bit[15:0]: register offset (NPU-MMIO-relative, not block-relative)
- Example: 64'h0081_0000_007f_0008 → write 0x7f to op_en (offset 0x8) of all blocks via bit[55]=1
- **REQUIRED**: "before op_en, 64'h0041_xxxx_xxxx_xxxx must be set" (runtime handles this)

---

## BUG A: PC DMA Completes But Sub-Units Never Finish (Apr 25 — ACTIVE)

### Symptom
`PC_INTERRUPT_RAW_STATUS=0` at 50ms. DPU_0/DPU_1 (bits 8/9) never fire. Job times out.
**PC DMA IS completing**: RS_IMM=0x4000 (bit[14] = "task-sequence DMA done") fires immediately after OP_EN
and self-clears by 50ms. IRQ handler returns IRQ_NONE for bit[14] → job doesn't complete.
Sub-unit execution chain (CNA→CMAC→ACCU→DPU_RDMA→DPU) does not reach DPU completion.

### Confirmed correct (Apr 25 log: 2026-04-25_l3_h3revert-trm.log)
- pc_sel=0 ✓ (BASE_RB=0x000d7000, bit0=0 → DMA mode active)
- MASK=0x0300 ✓ (DPU_0|DPU_1 enabled after H3 revert)
- AMOUNTS=0x4088 ✓ (0x88=136=count(133)+EXTRA(4)-1, H2 formula)
- TASK_CON readback=0x0001 ✓ (bits[14:13] W1C self-cleared, bit12=0 pp_off, bits[11:0]=1 task_number)
- IOMMU DTE sub-4GB, STATUS=0x1 (paging on, no active fault at OP_EN time ✓)
- Regcmd tail confirmed: entry 127=PC_amount clear, 128=0x0041 required entry, 129-132=sub-unit OP_ENs
  - Entry 129: CNA OP_EN (routing 0x0201, value 1, offset 0x1008) ✓
  - Entry 130: ACCU OP_EN (routing 0x0801, value 1, offset 0x3008) ✓
  - Entry 131: DPU_RDMA OP_EN (routing 0x2001, value 1, offset 0x5008) ✓
  - Entry 132: DPU OP_EN (routing 0x1001, value 1, offset 0x4008) ✓
  - NOTE: No CMAC OP_EN in regcmd — CMAC may be triggered internally by CNA
- TASKST=0x7000 unchanged before→after OP_EN at 50ms (task still "in progress"; clears only on DPU done)
- No rk_iommu page fault messages in dmesg → regcmd IOMMU mapping OK

### H3 Correction (Apr 25 — TRM confirmed)
**H3 was wrong.** TRM p412 explicitly: "mask[0]: set 1 to enable interrupt."
- `INTERRUPT_MASK=0` DISABLED all interrupts (H3 applied)
- `INTERRUPT_MASK=DPU_0|DPU_1` ENABLES job-done IRQ ← CORRECT; reverted Apr 25
- **Apr 25 fix**: INTERRUPT_MASK = `PC_INTERRUPT_MASK_DPU_0 | PC_INTERRUPT_MASK_DPU_1` restored

### AXI Safety Fix (Apr 25)
Never read CNA/ACCU/CORE MMIO while sub-units may be executing — hard CPU lockup on AXI stall.
Removed `rocket_cna_readl(S_STATUS)` and `rocket_core_readl(S_STATUS)` from POST50MS block (Apr 25).

### New diagnostics added (Apr 25 23:37 — rocket.ko)
- OPEN_EN_RB: pc_sel + RS_IMM + IOMMU_ST + **PAGE_FAULT_ADDR (FA)** + **INT_RAWSTAT (RAWST)**
- POST1MS: PC_RS + IOMMU_ST + FA + RAWST (1ms after OP_EN — catches CNA DMA faults)
- POST50MS: PC_RS + IOMMU_ST + FA + RAWST + OP_EN + BUS_IDLE + TASKST

### What to look for in next test run (Apr 25 23:37 build)
- `FA=non-zero` in POST1MS or POST50MS → CNA feature/weight DMA hit IOMMU fault → need to check teflon buffer mappings
- `RAWST=non-zero` in POST50MS → latched IOMMU fault even if FA already cleared → confirms CNA DMA fault
- `RAWST=0` everywhere → CNA DMA is NOT faulting → problem is elsewhere (sub-unit init, clock, CMAC missing?)
- `BUS_IDLE` bit2 at POST50MS: if =0 (active) → sub-units still doing DMA 50ms later (stuck loop?); if =1 → gave up

### Remaining hypotheses
a) **CNA DMA IOMMU fault (silent)**: feature/weight buffer not mapped in IOMMU; RAWST will show non-zero FA
b) **CMAC not triggered**: CNA sends data but CMAC (MAC pipeline) needs explicit init not in regcmd
c) **Sub-unit register reset state**: After rocket_reset, sub-units need explicit reset before next OP_EN
d) **NOC re-idle during execution**: BUS_IDLE changing mid-run; need to monitor between 1ms and 50ms

### Next session steps
1. Reboot to l3 (fresh module load — Apr 25 23:37 rocket.ko)
2. Run `python3 ~/npu-debug/teflon_test.py`
3. Check POST1MS: is FA non-zero? Is RAWST non-zero?
4. Check POST50MS: RAWST (latched fault indicator), BUS_IDLE (sub-unit DMA activity)
5. If RAWST=0 everywhere: focus on sub-unit init sequence (CMAC, reset state)
6. If RAWST non-zero: check FA for the faulting IOVA → which buffer is unmapped

---

## iommu_attach_group Crash Fix (Apr 19 — FIXED)
**Symptom**: Hard lockup on first job after fresh boot to l3. Last dmesg: `rk_iommu_attach: new_domain=...`
**Root cause**: `DL_FLAG_PM_RUNTIME` device link causes `rk_iommu_enable()` to be called after NPU CRU reset.
`rk_iommu_force_reset` sends FORCE_RESET to an already-CRU-reset IOMMU → AXI bus hangs → CPU stall.
**Fix**: Guard in `rk_iommu_attach_device` (rockchip-iommu.c ~line 1086):
```c
if (iommu->hw_needs_enable) {
    pm_runtime_put(iommu->dev);
    return 0;
}
```
**Status**: Fix applied, Image-6.19-rocket rebuilt (Apr 19 23:21, LXC server), deployed. No more lockup.
**Verification**: 5 consecutive teflon_test.py conv2d runs complete cleanly (timeout+reset cycle, no panic).
