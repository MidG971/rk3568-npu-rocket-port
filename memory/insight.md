---
name: BUG A debugging insights (Rocket NPU RK3568)
description: Deep-dive investigation notes on BUG A (NPU silent) — TRM-confirmed register bugs, AXI/IOMMU analysis, BOUNCE/IRQ issues, next steps
type: project
---

## AXI Bus / IOMMU Architecture

- IOMMU STATUS=0x19 = PAGING_ENABLED(bit0) + IDLE(bit3) + REPLAY_BUFFER_EMPTY(bit4) = healthy idle
- IOMMU STATUS=0x01 = PAGING_ENABLED only; IDLE=0, REPLAY_BUFFER_EMPTY=0 → has pending DMA translation in replay buffer
- REPLAY_BUFFER_EMPTY=0 means page walk completed and PA is in replay buffer awaiting AXI completion
- IOMMU page table walker uses its OWN AXI port; replayed data transactions use the NPU's AXI master port
- Both DTE/PTE reads (walker) AND regcmd data reads (replay) must succeed for STATUS to return to 0x19

## Page Table Walk Confirmed Correct (May 5)

Via `phys_to_virt` in kernel context (PRE_OP block of rocket_job.c):
- DTE_ADDR register = 0x044c8000 = physical address of page directory
- `dt[0]` = 0x040c5001 → valid, pt_phys=0x040c5000
- `PT[0xd7]` = 0x04190007 → valid, PA=0x04190000, flags=READ|WRITE|VALID
- **Conclusion**: page table IS in DRAM with correct content. NOT a DTE cache coherency problem.

IOVA 0xd7000 indexing: DTE_idx = 0xd7000 >> 22 = 0, PTE_idx = (0xd7000>>12)&0x3FF = 0xD7

## DMA Coherency (fini_bo / dma_sync)

- `rocket_ioctl_fini_bo` calls `dma_sync_sgtable_for_device(dev->dev, sgt, DMA_BIDIRECTIONAL)` (called from `rkt_buffer_unmap` in Mesa when transfer->usage==PIPE_MAP_WRITE)
- `dma_sync` issues ARM64 cache clean to PoC for all pages in SG table → data should be in DRAM before NPU access
- PENDING verification: PRE_OP REGCMD_DATA dump (reads first 8 words of regcmd PA via phys_to_virt) not yet run

## TASK_PP_EN (bit 12 of TASK_CON)

- Setting TASK_PP_EN enables ping-pong mode requiring a second buffer that never arrives
- MUST NOT be set for single-inference; our code writes TASK_CON=0x6001 (bits 14+13+0, PP_EN=0) ✓
- TASK_STATUS=0x7000 (bits 14+13+12 set) is read-only hardware state, NOT a reflection of PP_EN

## OP_EN and AMOUNTS

- Writing OPERATION_ENABLE with OP_EN=1 starts PC DMA
- OP_EN should NOT be pulsed (write once, stays set); BSP confirms OP_EN stays 1 at POST500US ✓
- DATA_AMOUNT = (regcmd_count / scale) - 1 written to REGISTER_AMOUNTS
- scale=1 for RK3568 (each unit = one 64-bit regcmd word)
- AMOUNTS=0x84 (132 decimal = count-1 → count=133 entries) written, readback=0x84 at AT50US
- AMOUNTS readback changes to 0x4084 (bit14 set) between 50µs and 500µs post-OP_EN → hardware-set flag

## PC_SEL Fix (Apr 29)

- Removed `rocket_pc_writel(core, BASE_ADDRESS, 0x1)` which was setting PC_SEL=1 (bit0) before IOVA write
- BSP never writes this; may have put PC DMA into non-IOMMU mode
- After fix: STATUS still 0x01 → PC_SEL was not root cause

## Regcmd Pattern

- Every odd 32-bit word = 0x02010000 (consistent across all 13 MobileNetV1 jobs)
- First words end in 0x1040; words at index 2,4,6: 0x00001104, 0x00001100, 0x0003100c
- 0x02010000 may be a register address [addr,val] 64-bit pair format

## BUG A Root Cause Hypothesis (May 5)

The IOMMU successfully walks the page table (no fault, PA in replay buffer).
The AXI replay (NPU reading from PA=0x04190000 in DRAM) hangs for the entire 480ms timeout.

Remaining hypotheses:
1. **Cache coherency of regcmd DATA**: fini_bo called but cache sync incomplete → DRAM has zeros
   → Test: PRE_OP REGCMD_DATA dump of phys_to_virt(PA)[0..7]
2. **AXI path for NPU data different from IOMMU walker path**: NOC routing or firewall blocks PA access
3. **IOMMU enters silent stall after paging enable**: STATUS shows no stall but hardware stalls data
4. **regcmd buffer at PA in reserved/kernel memory**: 0x04190000 in 0x03fb0000-0x04b6ffff reserved range

## System RAM Map (RK3568, 8GB board)

Physical address space from /proc/iomem:
- 0x00200000 - 0xEFFFFFFF: System RAM bank 0 (DRAM starts at 2MB)
  - 0x02000000 - 0x03FAFFFF: Kernel code
  - 0x03FB0000 - 0x04B6FFFF: reserved (likely freed initrd, can be alloc'd)
  - 0x04B70000 - 0x050FFFFF: Kernel data
- 0x100000000 - 0x1FFFFFFFF: System RAM bank 1 (4GB-8GB range)

GFP_DMA32 allocations land in bank 0 (sub-4GB). DTE at 0x044c8000, PT at 0x040c5000 confirmed healthy.

## TRM-Confirmed Register Programming Bugs (May 7)

See full register reference: `docs/npu-trm-registers.md`

### PC Write Order Violation
- TRM Fig. 9-2 mandates: INTERRUPT_CLEAR + MASK first, THEN addr/amount/task_con/dma_base/op_en
- Current rocket_job.c writes BASE_ADDRESS and AMOUNTS BEFORE interrupt_clear — violates TRM
- Fix: swap order so clear+mask come first

### BOUNCE staging page DMA coherency
- `smp_wmb()` after `memcpy` = CPU write-ordering barrier ONLY; does NOT flush CPU cache to DRAM
- On ARM64 non-coherent NPU DMA: staging page data stays in L1/L2 cache, DRAM has stale data
- Fix: `dma_sync_single_for_device(core->dev, staging_pa, PAGE_SIZE, DMA_TO_DEVICE)` instead

### TASK_DMA_BASE_ADDR semantics (TRM §9.4.3)
- TRM: "Final address on AXI bus = base_addr + offset_addr" for feature/weight/DPU/PPU DMAs
- Current code writes `task->regcmd` (IOVA=0xd7000) to this register
- If Mesa regcmd contains absolute IOVAs in data fields → dma_base_addr must be 0
- If Mesa regcmd contains offsets from regcmd base → current code may be correct
- To verify: dump CNA_feature_data_addr (CNA offset 0x0070) from regcmd and compare to expected IOVAs

### Missing IRQ Handler (rocket_core_init)
- `devm_request_threaded_irq` is never called in rocket_core_init
- PC raises IRQ (PC_RS=0x4000, mmu_error bit) at ~10ms post-OP_EN
- Without handler: IRQ line stays asserted; PC DMA may stall before broadcasting OP_EN to sub-units
- Fix: register IRQ handler; check upstream RK3588 Rocket driver for pattern

## PC Interrupt Bit Summary (from TRM)
- Bits[13:0] exist in ALL four registers (mask, clear, status, raw_status)
- Bit 14 = mmu_error exists ONLY in PC_interrupt_raw_status (no mask/clear bit)
- Bit 14 cannot be cleared by software writing INT_CLEAR
- Hardware clears bit 14 when op_en=1 is written (PC starts new DMA cycle)
- Observed: RAWST=0x4000 (bit14) at ~10ms = AXI timeout (not a page table fault)

## GLOBAL_OP_EN (TRM §9.4.20)
- Offset 0x0008 from GLOBAL base; enables all 7 sub-units simultaneously
- bit0=cna, bit1=cmac, bit2=accu, bit3=dpu, bit4=dpu_rdma, bit5=ppu, bit6=ppu_rdma
- Write 0x7f to enable all; this is the LAST step in every NPU flow (Figs 9-2 to 9-6)
- In PC mode: done by regcmd OP_EN broadcast entry `0x0081_0000_007f_0008`
- The entry before final op_en MUST be `0x0041_xxxx_xxxx_xxxx` (TRM requirement)

## Regcmd 64-bit Entry Format (TRM §9.4.2, PC_amount description)
- bits[63:56] = block routing: [56]=PC, [57]=CNA, [58]=CMAC, [59]=ACCU, [60]=DPU, [61]=DPU_RDMA, [62]=PPU, [63]=PPU_RDMA
- bits[55] = op_en broadcast flag (broadcast to all blocks)
- bits[47:16] = 32-bit register value
- bits[15:0] = 16-bit register offset within target block
- op_en entry must be LAST in regcmd list; entry 0x0041_xxxx must precede it

## Next Debug Steps (May 7)

1. **Fix PC write order** per TRM Fig. 9-2 (interrupt_clear+mask FIRST)
2. **Fix BOUNCE DMA coherency** (smp_wmb → dma_sync_single_for_device)
3. **Register IRQ handler** in rocket_core_init
4. **Investigate TASK_DMA_BASE_ADDR**: dump CNA_feature_data_addr from regcmd, compare to Mesa IOVAs
5. After each fix: run teflon_test.py, check if CNA_S_STATUS changes from 0 at timeout
