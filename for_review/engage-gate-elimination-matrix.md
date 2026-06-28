# RK3568 rocket engage-gate: elimination matrix (2026-06-25)

Goal: make a COLD rocket engage the NPU executers (S_POINTER bit16) on its first job, the way the
vendor rknpu does from a cold boot. Method: make rocket identical to the vendor on each axis, test.

| Axis | Matched to vendor? | Cold engage? | Evidence |
|---|---|---|---|
| regcmd content | YES (byte-identical) | NO | Method B replay + this session |
| every PC register write/value | YES | NO | AMOUNTS=0x88, INT_MASK=0x10300, TASK_CON=0x6001, TASKDMA=0 |
| IOVA height (regcmd) | YES (HIGH 0xfff3f000) | NO | strip-to-vendor-exact |
| arming (S_POINTER/pp_init/desc) | YES (vendor-minimal: NONE) | NO | strip-to-vendor-exact |
| power-on clock sequence | YES (raw clk_set_rate 600M two-step on scmi_clk) | NO | vendor_power=1 test |
| BO/IOMMU/DMA path | structurally CORRECT (not a bug) | NO | see below |
| **IOMMU domain identity** | YES — IDENTICAL | NO | rocket reuses device default domain (iommu_get_domain_for_dev, reused=true, rocket_drv.c:166) — SAME as rknpu |
| binding / region layout | non-factor | NO | same physical regs; rocket's writes land (config reads back) |
| raw-writel WTRACE blind spot | none | — | no raw writel in rknpu job path; all via _REG_WRITE (captured) |
| CRU soft_reset | not on engage path | — | rknpu_soft_reset callers = timeout/xlate-fail/ioctl only; cold 1st job engages w/o reset |
| GRF/syscon writes | none active on RK3568 | — | GRF volt_rm = rknpu_devfreq_bsp.c (not compiled, RK3588-only); bw_priority verified matched |

## BO/IOMMU/DMA path is correct (not the gate)
- shmem BOs, GFP_DMA32 (sub-4GB pages for 32-bit NPU AXI), iommu_map_sgtable(READ|WRITE).
- DMA coherency: prep_bo=dma_sync_for_cpu, fini_bo=dma_sync_for_device (replay calls fini before submit).
- Domain CONSISTENT: BOs mapped in file_priv's domain; job attaches THAT SAME domain to the NPU
  (rjob->domain = rocket_iommu_domain_get(file_priv); core->attached_domain = job->domain).
- IOMMU active (ST=0x19), NO page fault (FA=0), and **PC fetched the regcmd THROUGH the domain**
  (dt_rd≈0x88-entry regcmd) → domain works, data reachable.

## Decisive symptom
dt_rd≈0x89 (PC read the regcmd fine) but wt_rd=0 (CNA never read weights). The CNA executer never
STARTS → the failure is UPSTREAM of any buffer read. The BO/IOMMU path is only exercised AFTER a unit
engages, so it cannot be the gate.

## Conclusion
Every software-visible difference between cold-rocket and cold-rknpu is eliminated, and rocket still
does not engage while rknpu does from the same cold state. ⇒ **the engage gate is HW-internal**: the
executer-engage latch (S_POINTER bit16, HW-set only when a unit actually runs) is established by the
vendor driver's job EXECUTION in a way not reproducible from rocket's submission by any software means.
The BSP-primer (run a vendor rknpu job once; the engaged HW state persists while the NPU stays
powered; rocket then inherits it) is the only known working trigger — and is the practical fix path.

## Implications for upstream
- A pure-rocket cold engage on RK3568 is not achievable by matching the vendor's software behaviour;
  it requires either (a) a priming step (vendor-job-equivalent) the rocket driver performs, or
  (b) Rockchip/vendor disclosure of the HW-internal init the rknpu lifecycle performs that we cannot
  observe. Worth raising with gahingwoo (RK3576 engages cold via 0xf008, which RK3568 lacks/hangs) and
  Tomeu (rocket author) as a HW-init question, not a driver-logic one.
