# NPU Bring-up Tools & Debug Methods

> **Status: STAGING — do NOT push.** Reviewed by repo owner before any push;
> push only after the upstream driver series is accepted by maintainers.
>
> A catalog of the tools, scripts, and debugging methods used to bring up the
> Rockchip RK3568 NPU (rocket accel driver + mesa teflon) by reverse-engineering
> against the vendor BSP (rknpu). Aimed at anyone doing NPU/accelerator
> bring-up, mesa/rocket porting, or RK3568/RK3588 IOMMU debugging.

Each entry: **What** · **How** · **Example** · **Use-case**.

---

## 1. Board access & discovery

### `find-rock3b.sh` / `rock3b-ssh.sh`
- **What:** Discover the ROCK 3B board's current IP (USB-eth / LAN / Tailscale)
  and run a command over SSH without re-typing credentials/host keys.
- **How:** `./rock3b-ssh.sh "<command>"` runs `<command>` on the board.
  Caches the IP in `~/.rock3b_ip`.
- **Example:** `./rock3b-ssh.sh "uname -a; lsmod | grep rocket"`
- **Use-case:** Any headless dev board whose DHCP IP changes across reboots.

### Reachability probe (inline)
- **What:** Quickly test if the board's SSH is up across several known IPs.
- **How:** `for ip in $(cat ~/.rock3b_ip) <others>; do timeout 4 bash -c "echo >/dev/tcp/$ip/22" && echo UP:$ip; done`
- **Use-case:** Deciding whether a reboot finished before issuing commands.

---

## 2. Kernel module fast-iteration cycle

### Build + reload a single driver module (no full kernel rebuild)
- **What:** Rebuild only `drivers/accel/rocket/rocket.ko` and hot-reload it.
  ~30s vs ~3–4 min full kernel build.
- **How (on board):**
  ```bash
  cd ~/npu-port/linux-mainline
  make ARCH=arm64 CROSS_COMPILE= -j4 M=drivers/accel/rocket
  KO=/lib/modules/$(uname -r)/kernel/drivers/accel/rocket/rocket.ko
  sudo cp drivers/accel/rocket/rocket.ko "$KO"
  sudo rmmod rocket; sudo modprobe rocket <params>
  ```
- **Gotcha:** copy the `.ko` to the `kernel/` modules path (depmod prefers it
  over `extra/`). `modinfo` may not be on the non-interactive SSH PATH — use the
  explicit `/lib/modules/$(uname -r)/...` path.
- **Use-case:** Iterating on driver behavior without rebooting.

### Module parameters for runtime debugging (no rebuild between tests)
- **What:** Expose debug toggles as `module_param(..., 0644)` so they're
  writable at runtime via sysfs — sweep values without rebuilding.
- **How:** `echo <val> | sudo tee /sys/module/rocket/parameters/<name>`, or set
  at load: `sudo insmod rocket.ko clean_run=1 bsp_cna=1 cmac_open=1 nop_start=0 nop_end=4`.
- **Example:** the rocket debug params added during bring-up:
  `clean_run` (1 = native mesa regcmd, gates OFF the contaminated OP_EN-offset
  fix), `desc_mode` (task descriptor / TASKDMA vs direct), `cmac_open`, `cna_sp`,
  `conv_con1`, `cna_last`, `compound_open`, `cbuf_con0`, `no_block_open`,
  `desc_en_mask`, `nop_start`/`nop_end` (NOP a regcmd-entry slot range;
  `0..4` = ping-pong off, `-1` = keep). Newer HW-bisection toggles (2026-06):
  `bsp_cna` (overlay BSP register *values* onto the mesa regcmd),
  `cmac_bsp` (inject BSP's CMAC setup — S_POINTER `0x2004=0xe` + enable
  `0x200c=1`), `op_en_fix` (use BSP's `0x_00c` enable offset instead of mesa's
  `0x_008` — **HARMFUL on rocket, leave 0**), `pc_slave_pre` (no effect),
  `task_pp` (decouple TASK_CON bit 12 / `task_pp_en` from `desc_mode`).
- **The point:** one build, many runtime sweeps — bisect hardware behavior across
  config combinations for the cost of a `tee` instead of a rebuild. The full
  matrix above was driven from a single live module.
- **WARN:** `conv_con1=0xa000` WEDGES the NPU (needs a reboot to recover).
- **BEST-STATE recipe (current frontier):** `clean_run=1 bsp_cna=1 cmac_open=1 nop=0..4`
  → CNA reads full input+weights, 4 units run, but DPU still writes 0.
- **Use-case:** Bisecting hardware behavior across many config combinations
  cheaply — one build, many runtime sweeps.

---

## 3. Regcmd (command-list) capture & patching

### In-kernel regcmd patching primitive (kmap)
- **What:** Modify the GPU/NPU command-list (regcmd) BO from the kernel right
  before submission, to override/inject/NOP register-write entries.
- **How:** resolve the BO's physical page from its IOVA, map it, edit the
  64-bit entries, flush:
  ```c
  phys_addr_t pa = iommu_iova_to_phys(domain, regcmd_iova);
  struct page *pg = pfn_to_page(pa >> PAGE_SHIFT);
  u32 *p = (u32 *)((char *)kmap_local_page(pg) + (pa & ~PAGE_MASK));
  /* each entry = 2x u32: w0=(val<<16)|offset, w1=(routing<<24)|flags|val>>16 */
  ... edit p[i*2], p[i*2+1] ...
  flush_dcache_page(pg); kunmap_local(p);
  ```
- **Don'ts:** `memremap+writel+wmb` crashed the board; `drm_gem_shmem_vmap` fails
  (regcmd BO not in in/out_bos). For CONFIG-value patches, modify the value
  field IN-PLACE (preserve the entry's native w1 flags) — rebuilding w1 with a
  guessed flag wedged the NPU.
- **Use-case:** Testing "what if this register held BSP's value" without
  rebuilding userspace (mesa).

### Vendor BSP regcmd capture + offset diff
- **What:** Dump the vendor driver's full command-list during a known-good run,
  then diff against your driver's to find missing/wrong register writes.
- **How:** add a one-shot `dev_err` loop over the regcmd entries in the vendor
  driver (`RKNPU_DUMP`/`REGCMD_FULL`), boot the vendor kernel, run inference,
  capture `dmesg`. Compare offset SETS first (which registers each side writes),
  then values for matching offsets.
- **Example:** `grep -oE 'off=0x1[0-2].. val=0x[0-9a-f]+ routing=0x02' bsp.txt | sort -u`
- **Use-case:** Reverse-engineering the correct command-list for an
  undocumented accelerator by comparing to a working reference.

---

## 4. Reference-vs-port comparison (boot switching)

### A/B boot entries for vendor BSP vs mainline port
- **What:** Keep two boot entries (extlinux) — one for the vendor driver
  (instrumented), one for your port — to capture the SAME hardware under both.
- **How:** edit `default lN` in `/boot/extlinux/extlinux.conf`, `sudo reboot`,
  wait, capture; switch back when done.
  ```bash
  sudo sed -i 's/^default l3/default l1/' /boot/extlinux/extlinux.conf; sudo reboot
  ```
- **Use-case:** Side-by-side register/state capture of vendor vs port on
  identical silicon — the single most valuable RE technique here.

### A/B boot capture workflow (l1 BSP ↔ l3 rocket, scripted)
- **What:** The concrete unattended recipe for an A/B capture: flip the default
  entry, reboot, poll for SSH to return, run, capture, then restore. Wraps the
  reboot + the "is the board back yet?" wait so a capture pair can be driven from
  the host in one go.
- **How:**
  ```bash
  B=radxa@<BOARD-TAILSCALE-IP>                                    # Tailscale
  ssh $B 'sudo sed -i "s/^default l3/default l1/" /boot/extlinux/extlinux.conf; sudo reboot'
  # ^ exits "Terminated" — EXPECTED (the reboot kills the SSH session)
  until timeout 4 bash -c "echo >/dev/tcp/<BOARD-TAILSCALE-IP>/22" 2>/dev/null; do sleep 3; done
  ssh $B 'sudo dmesg -C; ~/run_rknn_152 ~/conv2d-bsp.rknn; sudo dmesg | grep RKNPU_PIPE'
  ssh $B 'sudo sed -i "s/^default l1/default l3/" /boot/extlinux/extlinux.conf; sudo reboot'  # restore
  ```
- **Gotchas:** the `sudo reboot` SSH command always exits with `Terminated` /
  non-zero — that is the expected success path, not a failure. The board returns
  in ~20–60 s. It is REMOTE — a reboot that hangs needs physical/serial access,
  so always restore the default-to-rocket (l3) entry at the end of a BSP capture.
- **Use-case:** Driving a full BSP-vs-port comparison pass (build BSP kernel →
  boot l1 → capture working run → boot l3 → capture port run → diff) without
  babysitting each reboot.

### BSP (vendor rknpu) commit-path instrumentation (mirror the port's probes)
- **What:** Port the *exact same* pipeline-sweep + perf-counter probes into the
  vendor driver's submit path, so a known-good BSP run produces directly
  comparable dmesg lines. This is what turns the A/B boot capture into a true
  apples-to-apples diff — the working run is measured with the identical code as
  the broken one.
- **How:** instrument `rknpu_job_subcore_commit_pc` in `srcdiff/rknpu_job.c`:
  clear+enable the perf counters (`0x80000101` then `0x00000101` → `0x8010`)
  right before the OP_EN pulse, then emit `RKNPU_PIPE_IMM` / `_100US` / `_1MS` /
  `_5MS` samples after it (each reading CNA/CMAC/ACCU/DPU/DPU_RDMA `S_STATUS` +
  `CBUF_CON0` + `dt_wr`/`dt_rd`/`wt_rd`). The vendor driver addresses registers
  via `REG_READ/REG_WRITE(off)` and `RKNPU_OFFSET_*` macros — relevant PC ones:
  `PC_DATA_ADDR=0x10` (= the regcmd base; same reg rocket calls BASE_ADDRESS),
  `PC_OP_EN=0x8`, `PC_DATA_AMOUNT=0x14`, `PC_TASK_CONTROL=0x30`,
  `PC_DMA_BASE_ADDR=0x34`.
- **Gotcha:** the BSP kernel is built with `CONFIG_RKNPU=y` (built-in, not a
  module) → a change needs a full kernel rebuild + `cp Image /boot/Image-6.19`
  (incremental ~min, not the ~30 s rocket module reload). Restore a clean BSP
  `Image` before any upstream work — the instrumented one is debug-only.
- **Example findings:** BSP `dt_wr=0x6400`=204800 B = exactly the full output;
  all units reach `S_STATUS=0x0c` with `S_POINTER=0x1000e` (HW bit 16 set) — the
  single signal the stuck port never reaches.
- **Use-case:** Pinning the remaining BSP↔port gap to one HW-set bit by measuring
  both runs with byte-for-byte identical instrumentation.

---

## 5. Live hardware state dumps

### Timed register snapshots around submission
- **What:** Read key MMIO registers at fixed timepoints (PRE_OP, +50µs, +1ms,
  +5ms, +10ms, TIMEOUT) to watch a state machine evolve.
- **How:** `ioremap(<npu_base>, size)` + `readl(base + off)` in the submit path,
  `dev_err` each sample; grep dmesg by tag.
- **Use-case:** Distinguishing "stuck" vs "transient" hardware states; finding
  exactly when a status bit flips.

### Full-pipeline S_STATUS sweep (`rocket_pipe_sweep`)
- **What:** One kernel helper that reads EVERY NPU sub-unit's `S_STATUS` in a
  single `dev_err` line, called at the same 5 timepoints as the timed snapshots
  above (AT_OPEN / AT50US / AT1MS / AT5MS / AT10MS). Closes the long-standing
  instrumentation gap where only CNA + ACCU were sampled over time and the other
  sub-units got a single AT10MS snapshot — so you can see *which* sub-unit fails
  to advance, and *when*.
- **How:** `ioremap(0xfde40000, 0x10000)` once per call, then read the per-unit
  `S_STATUS` at offset `0x_000`: CNA `0x1000`, CMAC `0x2000`, ACCU `0x3000`,
  DPU `0x4000`, DPU_RDMA `0x5000`, plus `CBUF_CON0` (`0x1040`) and the `0x_008`
  run-bits, all on one line tagged by the timepoint. Lives above
  `rocket_job_hw_submit` in `srcdiff/rocket_job.c`.
  ```c
  dev_err(core->dev, "%s PIPE Sstatus: CNA=%08x CMAC=%08x ACCU=%08x DPU=%08x RDMA=%08x | CBUF0=%08x ...",
      tag, readl(p+0x1000), readl(p+0x2000), readl(p+0x3000),
      readl(p+0x4000), readl(p+0x5000), readl(p+0x1040), ...);
  ```
- **Why S_STATUS (offset `0x_000`), not the `0x_008` run-bit:** `S_STATUS` is the
  HW-set *internal pipeline state* (e.g. `0x05` status_0=1 "executer operating",
  `0x0c` status_0=0 "done") — it is the ground truth for whether a stage actually
  ran. The `0x_008` register merely echoes the *last value written* to it (a known
  trap that made several earlier sessions think a unit was "enabled" when it was
  only "told to be enabled"). Read both; trust `S_STATUS`.
- **Use-case:** Localizing a multi-stage pipeline stall — this is what showed all
  5 units freezing at `S_STATUS=0x1` for the whole 540 ms (none advancing), then
  later showed BSP's units reaching `0x0c` (done) where rocket's stuck at `0x05`.

### NPU perf data-amount counters probe (the "did data actually move" probe)
- **What:** Read the NPU top-level data-amount counters to measure, in bytes,
  exactly how much each engine moved: `dt_wr` = data written by DPU (output),
  `dt_rd` = feature data read by CNA (input), `wt_rd` = weights read by CNA.
  This is the decisive, unambiguous "did data move" signal — independent of the
  ambiguous IOMMU idle/busy bits (which earlier sessions mis-read as "stalled
  reads" when it was just CNA's real reads completing).
- **How:** the counters are **NOT free-running** — they must be enabled+cleared
  with the vendor sequence (`rknpu_clear_rw_amount`) before the run, then read
  after. Counter unit = **8 bytes**.
  ```c
  /* enable + clear, just before the OP_EN pulse: */
  writel(0x80000101, npu + 0x8010);
  writel(0x00000101, npu + 0x8010);   /* clr reads back 0x101 = enabled */
  ...
  /* read after the job (AT10MS / post-timeout): */
  dt_wr = readl(npu + 0x8034);        /* DPU bytes written  (×8)        */
  dt_rd = readl(npu + 0x8038);        /* CNA feature read   (×8)        */
  wt_rd = readl(npu + 0x803c);        /* CNA weight read    (×8)        */
  ```
- **CRITICAL gotcha:** without the enable sequence the counters read **0** —
  which looks exactly like "no traffic". This is a false negative that misled the
  project for a long while (the very first attempt wrote `0xffffffff` to `0x8010`,
  which *disabled* the counters → all-zero reads → "CNA never reads" conclusion).
  Always confirm `0x8010` reads back `0x101` (enabled) before trusting a zero.
- **Use-case:** The big reframe of this bring-up — proved CNA *does* read the full
  input (`dt_rd≈0x328f`) and full weight tensor (`wt_rd=0x1900`=51200 B) while
  the DPU writes nothing (`dt_wr=0`), overturning the multi-year "CNA never
  starts / pipeline starved" model and relocating the gate to the
  compute→output-write path.

### IOMMU fault / state inspection
- **What:** Read IOMMU STATUS/FAULT-ADDR to see if the accelerator's AXI master
  faults, and at which IOVA.
- **How:** exported `rk_iommu_read_status()`; or decode `RK_MMU_*` registers.
  Page-fault dmesg lines give `iova=... dte_index/pte_index` — bucket them to
  find burst boundaries / which BO is mis-mapped.
- **Use-case:** Diagnosing DMA/page-table problems (wrong domain, prefetch
  overruns, sub-4GB requirements).

---

## 6. Recovery

### Wedged-NPU recovery
- **What:** A corrupted command-list or bad register value can wedge the NPU/PC
  so it stays broken across `rmmod`/`modprobe` (HW not reset by module reload).
- **How:** full `sudo reboot` clears it. A latched PMIC (after heavy fault
  testing) may need a physical power-cycle (unplug power+USB ~60s).
- **Use-case:** Know when a "still broken after reload" result is a wedge, not a
  real result — reboot and re-establish the known-good baseline first.
- **Heads-up:** the `conv_con1=0xa000` module param value WEDGES the NPU (needs a
  reboot) — a concrete example of a "bad register value" wedge.

### Tailscale MTU-blackhole vs genuinely-offline (and serial fallback)
- **What:** The board is reached over Tailscale; two distinct failure modes look
  similar but need different fixes — an MTU blackhole on the *host's* tailscale
  link (board is fine, large packets are dropped) vs the board genuinely offline.
- **Symptoms (MTU blackhole):** SSH freezes at KEX (key exchange), `scp` times
  out mid-transfer, small commands sometimes work but large output hangs.
  **Fix on the host:** `sudo ip link set tailscale0 mtu 1000` (a ≤1000-byte
  payload survives the path).
- **Symptoms (genuinely offline):** `tailscale ping <board>` times out,
  `tailscale status` shows the peer offline with `rx 0` (tx>0). No MTU change
  helps — the board is REMOTE, so this needs a physical power-cycle.
- **Recovery / observation path when SSH won't come back:** the serial console at
  **1500000 baud** — `picocom -b 1500000 /dev/ttyUSB0` (optionally
  `--logfile logs/serial/<date>_<desc>.log`). Serial sees the boot and any kernel
  output even when the network is dead, and is how you confirm whether the board
  is hung vs just unreachable.
- **Use-case:** Quickly deciding between "fix my host's MTU and retry" and
  "the board is down, get to the power switch" instead of chasing a phantom.

---

## 7. Byte-exact BSP-vs-port regcmd comparison (the decisive RE tool)

### Capture the vendor's command-list for YOUR exact model shape
- **What:** Run the *identical* model on the vendor BSP runtime and on your
  port, then diff the generated command-list register-by-register. Any non-
  address value that differs is, by definition, a bug in your port's value
  generation. This is the only *like-for-like* comparison (vs diffing against a
  different model shape, where every value differs meaninglessly).
- **How (RK3568 NPU specifics):**
  1. Match the vendor runtime version. Board here = librknnrt **1.5.2** +
     rknpu driver 0.9.8. A 2.x `.rknn` won't load (ret=-6) and forcing the 2.x
     runtime *crashes* the old driver. So convert with the matching toolkit.
  2. rknn-toolkit2 1.5.x is **not on PyPI** (only 2.x) and needs Python ≤3.10.
     Use Docker: `docker run --rm -v $PWD:/work -w /work python:3.10 bash -c
     "apt-get install -y libgl1 libglib2.0-0; pip install ./rknn_toolkit2-1.5.2+
     b642f30c-cp310-cp310-linux_x86_64.whl; python fix_execstack.py; python
     convert.py"`. The 1.5.2 wheel is in `rockchip-linux/rknn-toolkit2@v1.5.2
     /packages/`.
     - Gotcha A: opencv needs `libgl1 libglib2.0-0`.
     - Gotcha B: onnxruntime's `.so` requires an executable stack the hardened
       host kernel (6.x) refuses → clear `PT_GNU_STACK` PF_X with
       `fix_execstack.py` (pure-python ELF edit).
  3. Convert: `RKNN().config(target_platform='rk3568'); load_tflite();
     build(do_quantization=False); export_rknn()`.
  4. Run on the board with a minimal C runner compiled against the matching
     header (`run_rknn.c`): `rknn_init` from file, query input size, feed a
     zero `pass_through` buffer, `rknn_run` once. Output is irrelevant — the
     goal is to make the NPU execute so the kernel dumps the regcmd.
  5. Capture the vendor regcmd from dmesg (needs the BSP kernel instrumented
     with a per-entry `REGCMD_FULL` dump), diff with `diff_regcmd.py`.
- **Example:** `diff_regcmd.py mesa_conv2d_regcmd.txt bsp_conv2d_regcmd.txt`
  → flags non-address value diffs as candidate port bugs.
- **Use-case:** Reverse-engineering correct register values for an
  undocumented accelerator by using the vendor blob as an oracle. This found
  real mesa value bugs (line_stride 4×, CONV_CON4, CONV_CON2, …) that no amount
  of register-poking revealed.
- **Files:** `rknn-convert/` (convert.py, convert152.py, run_rknn.c,
  fix_execstack.py, diff_regcmd.py, the wheels/lib/header, captured regcmds).

### `diff_regcmd.py` — classified BSP-vs-mesa command-list diff
- **What:** The actual diff tool that consumes two captured regcmd dumps (the
  vendor's `bsp_conv2d_regcmd.txt` from a `REGCMD_FULL_BSP` dump on a real BSP
  run, and mesa's `mesa_conv2d_regcmd.txt`) for the IDENTICAL conv, and
  classifies every difference into three buckets: **BSP-only offsets** (registers
  the vendor writes that mesa never does), **mesa-only offsets** (spurious writes
  the port emits), and **value-diffs** for offsets both write. IOVA/address
  registers are flagged separately as benign (they legitimately differ — each
  side has its own BO layout).
- **How:** `diff_regcmd.py mesa_conv2d_regcmd.txt bsp_conv2d_regcmd.txt`. It
  parses lines of the form `... off=0x1010 val=0x00030080 routing=0x02`.
- **Regcmd 64-bit entry encoding** (needed to read/patch the captures by hand):
  each entry is two `u32` words —
  `w0 = (off & 0xffff) | ((val & 0xffff) << 16)` and
  `w1 = (routing << 24) | 0x010000 | ((val >> 16) & 0xffff)`
  (the `0x010000` byte at bits [23:16] is the vendor's consistent flags byte;
  `routing` is the sub-unit bitmask 0x02=CNA, 0x04=CMAC, 0x08=ACCU, 0x10=DPU,
  0x20=DPU_RDMA). Modify the value field IN-PLACE — rebuilding `w1` with a guessed
  flag has wedged the NPU before.
- **Use-case:** This is how "all register VALUES match BSP, only the STRUCTURE
  differs" was established — with `bsp_cna=1` every CNA/ACCU/DPU/DPU_RDMA value
  matches the vendor byte-for-byte, leaving only structural diffs (BSP writes a
  CMAC `0x2004` S_POINTER + `0x200c` enable that mesa omits; BSP's enables sit at
  `0x_00c` not mesa's `0x_008`). It separates value-generation bugs from
  command-list-structure / ordering bugs decisively.

## TODO (keep current each session)
- [ ] Decide final repo layout (one `npu-debug-tools/` repo vs per-tool).
- [ ] Promote the ad-hoc `python3 - <<'PY'` regcmd-edit snippets into a small
      reusable `regcmd_patch.py`-style helper if they recur.
- [ ] Add the teflon conv2d test harness (`~/npu-debug/teflon_test.py`) with its
      reference output (sum=23,252,430) as a regression check.
- [ ] Capture the BSP-kernel instrumentation patch (RKNPU_DUMP_* + the
      `rocket_pipe_sweep`-mirror + perf clear/read in `rknpu_job_subcore_commit_pc`)
      as a reusable diff for future captures.
- [ ] Promote `rocket_pipe_sweep` + the perf-counter probe into a single reusable
      "pipeline state + bytes-moved" snapshot helper shared by both drivers.
- [ ] Script the A/B boot capture loop (l1 BSP ↔ l3 rocket, SSH-poll + restore)
      into a `capture_ab.sh` so a comparison pass is one command.
- [ ] Document the byte-exact BSP regcmd REPLAY harness (overwrite the port's
      regcmd with BSP's entries, substituting only the IOVA fields) once it lands.
