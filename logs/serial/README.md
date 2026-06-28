# Serial Console Logs

Raw serial output from the ROCK 3B debug sessions. Used for post-session analysis,
register value archaeology, and timeline correlation across driver + kernel messages.

## Capture

Serial is on UART2 (3.3V), exposed via the GPIO header debug port. Connect with any
USB-serial adapter at **1500000 baud** (Radxa default for RK3568):

```bash
# minicom
minicom -D /dev/ttyUSB0 -b 1500000

# picocom (easier log capture)
picocom -b 1500000 /dev/ttyUSB0 --logfile logs/serial/YYYY-MM-DD_description.log

# screen with tee
screen /dev/ttyUSB0 1500000 | tee logs/serial/YYYY-MM-DD_description.log

# minicom with capture (Ctrl-A L to start/stop)
# saves to ~/minicom.cap by default — move after session
```

## Naming Convention

```
YYYY-MM-DD_<boot-entry>_<test-or-feature>.log
```

Examples:
```
2026-04-15_l3_trigger-enable-test.log
2026-04-06_l3_detach-before-attach.log
2026-03-29_l1_gfp-dma32-fix.log
```

Boot entries: `l0` (BSP rescue), `l1` (IOMMU default), `l2` (non-IOMMU), `l3` (Rocket driver)

## What to annotate

After capture, add a short header block at the top of the file:
```
# DATE: YYYY-MM-DD
# BOOT: l3 (Image-6.19-rocket + rk3568-rock-3b-6.19-rocket-fixed.dtb)
# KERNEL: 6.19.0-rc5 #50
# MODULE: rocket.ko Apr 15 16:25 (trigger_enable + iommu_dev lookup)
# TEST: python3 /tmp/teflon_test.py (MobileNetV1, 13 layers)
# RESULT: BUG B fixed (no EBUSY), BUG A still present (NPU silent, 500ms timeout)
# NOTES: AMOUNTS=0x0081 clean; DTE=0x04508000; all 13 jobs timeout, CPU fallback 14s
```

## Index

| File | Date | Boot | Test | Key Findings |
|------|------|------|------|--------------|
| `../../log4.txt` | ~2026-03-24 | l1 | rknpu IOMMU probe | soft_reset crash fix |
| `../../log5.txt` | ~2026-03-27 | l1 | rknpu IOMMU IRQ | INT_MASK bit16 wiring |
| `../../log6.txt` | ~2026-03-29 | l1 | rknpu GFP_DMA32 | PTE=0 fix confirmed working, YOLOv5s 56.7ms |
| `../../log_claude_first_boot.txt` | ~2026-02-13 | l0/l1 | first mainline boot | initial RKNPU probe failures |
| `../../last_boot_log.md` | ~2026-04-03 | l3 | Rocket first probe | /dev/accel/accel0 created, DMA_READ_ERROR |
| `../../bootn2.txt` | ~2026-04-05 | l3 | Rocket DTE fix | pc_data_amount_scale=1, AMOUNTS=0x81 |
| `../../bootn3.txt` | ~2026-04-06 | l3 | Rocket detach patch | detach-before-attach fix |
| *(next session)* | 2026-04-?? | l3 | scale=2 + tail dump | BUG A investigation |

## Grep recipes for common patterns

```bash
# All rocket-related lines in a log
grep -i 'rocket\|accel\|iommu\|rk_iommu\|noc\|npu' 2026-04-15_l3_trigger-enable-test.log

# Register dumps at hw_submit
grep -E 'BASE=|AMOUNTS=|OPENA=|TASKCON=|STATUS=|RAW_STATUS=' <log>

# IOMMU domain transitions
grep -E 'rk_iommu|attach|detach|EBUSY|identity' <log>

# Job timeline (timestamps + rocket lines only)
grep -E '^\[.*\].*rocket' <log>

# Timeout events
grep -E 'timeout|timed out|500ms|RAW_STATUS=0' <log>

# DTE address checks (confirm sub-4GB)
grep -i 'DTE\|dte_addr\|0x0[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]' <log>
```
