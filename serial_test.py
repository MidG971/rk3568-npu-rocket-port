#!/usr/bin/env python3
"""
Automated ROCK 3B Rocket NPU serial test session.
Board must have extlinux default=l3 set before running.
Reboots via SSH, waits for Linux boot, loads rocket, runs teflon test.
"""
import os, sys, time, termios, select, threading, subprocess, fcntl

BOARD_IP   = "192.168.1.163"
BOARD_USER = "radxa"
SERIAL_DEV = "/dev/ttyUSB0"
BAUD       = 1500000
LOG_FILE   = "logs/serial/2026-04-18_l3_buga-scale2-iommufix.log"

# ── serial port setup (termios, no pyserial) ──────────────────────────────────

def open_serial(dev, baud):
    fd = os.open(dev, os.O_RDWR | os.O_NOCTTY)
    attrs = termios.tcgetattr(fd)
    attrs[0] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK |
                  termios.ISTRIP | termios.INLCR | termios.IGNCR |
                  termios.ICRNL | termios.IXON)
    attrs[1] &= ~termios.OPOST
    attrs[2] &= ~(termios.ECHO | termios.ECHONL | termios.ICANON |
                  termios.ISIG | termios.IEXTEN)
    attrs[2] &= ~(termios.CSIZE | termios.PARENB)
    attrs[2] |= termios.CS8
    attrs[3] = 0
    attrs[4] = termios.B1500000
    attrs[5] = termios.B1500000
    attrs[6][termios.VMIN]  = 1
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd

def write_serial(fd, data):
    os.write(fd, data.encode() if isinstance(data, str) else data)

# ── logger thread ─────────────────────────────────────────────────────────────

captured   = []
stop_event = threading.Event()
serial_fd  = None
log_fh     = None

def logger_thread():
    buf = b""
    while not stop_event.is_set():
        r, _, _ = select.select([serial_fd], [], [], 0.1)
        if not r:
            continue
        try:
            chunk = os.read(serial_fd, 256)
        except OSError:
            break
        buf += chunk
        while True:
            for delim in (b'\r\n', b'\n', b'\r'):
                idx = buf.find(delim)
                if idx >= 0:
                    line = buf[:idx].decode(errors='replace').strip()
                    buf = buf[idx + len(delim):]
                    if line:
                        captured.append(line)
                        print(f"  [ser] {line}", flush=True)
                        if log_fh:
                            log_fh.write(line + "\n")
                            log_fh.flush()
                    break
            else:
                break

def wait_for(pattern, timeout=120):
    deadline = time.time() + timeout
    idx = len(captured)
    while time.time() < deadline:
        time.sleep(0.05)
        while idx < len(captured):
            line = captured[idx]; idx += 1
            if pattern.lower() in line.lower():
                return line
    return None

def wait_for_any(patterns, timeout=120):
    deadline = time.time() + timeout
    idx = len(captured)
    while time.time() < deadline:
        time.sleep(0.05)
        while idx < len(captured):
            line = captured[idx]; idx += 1
            for p in patterns:
                if p.lower() in line.lower():
                    return p, line
    return None, None

# ── SSH helper ────────────────────────────────────────────────────────────────

def ssh(cmd, timeout=60):
    r = subprocess.run(
        ["ssh", "-o", "StrictHostKeyChecking=no", "-o", "ConnectTimeout=10",
         f"{BOARD_USER}@{BOARD_IP}", cmd],
        capture_output=True, text=True, timeout=timeout)
    return (r.stdout + r.stderr).strip()

def ssh_available(timeout=5):
    try:
        r = subprocess.run(
            ["ssh", "-o", "StrictHostKeyChecking=no", "-o", f"ConnectTimeout={timeout}",
             f"{BOARD_USER}@{BOARD_IP}", "echo ok"],
            capture_output=True, text=True, timeout=timeout+2)
        return r.returncode == 0
    except Exception:
        return False

# ── main ──────────────────────────────────────────────────────────────────────

def section(title):
    bar = "─" * 60
    print(f"\n{bar}\n  {title}\n{bar}")
    if log_fh:
        log_fh.write(f"\n### {title}\n")

def main():
    global serial_fd, log_fh

    os.makedirs(os.path.dirname(LOG_FILE), exist_ok=True)
    log_fh = open(LOG_FILE, "w")
    log_fh.write(f"# Session: {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
    log_fh.write(f"# Test: iommu hw_needs_enable crash fix + scale=2 + REGCMD tail dump\n\n")

    print(f"Opening {SERIAL_DEV} at {BAUD} baud...")
    serial_fd = open_serial(SERIAL_DEV, BAUD)

    t = threading.Thread(target=logger_thread, daemon=True)
    t.start()
    print("Serial reader started.")

    # ── Reboot ────────────────────────────────────────────────────────────
    section("REBOOT")
    print("Sending reboot via SSH...")
    try:
        ssh("sudo reboot", timeout=5)
    except Exception:
        pass  # SSH may close before returning

    # Wait for board to go down
    print("Waiting for board to go offline...")
    deadline = time.time() + 30
    while time.time() < deadline:
        if not ssh_available(3):
            print("  Board offline.")
            break
        time.sleep(2)

    # ── U-Boot / extlinux ─────────────────────────────────────────────────
    section("U-BOOT / EXTLINUX (expecting l3 autoboot)")
    hit = wait_for_any(["Booting `Mainline 6.19 Rocket", "Starting kernel", "Hit any key"], timeout=60)
    if hit[0]:
        print(f"  Boot selection: {hit[1]}")
    else:
        print("  [no boot selection line seen]")

    # ── Linux boot ────────────────────────────────────────────────────────
    section("LINUX BOOT")
    hit = wait_for_any(["login:", "rock-3b", "radxa@"], timeout=120)
    if not hit[0]:
        print("  [WARNING: no login prompt — may have crashed or booted wrong entry]")
    else:
        print(f"  Linux up: {hit[1]}")

    time.sleep(4)  # let udev settle

    # ── Verify new kernel ─────────────────────────────────────────────────
    section("KERNEL VERIFY")
    out = ssh("uname -r && ls -la /boot/Image-6.19-rocket")
    print(out)

    # ── Load rocket module ────────────────────────────────────────────────
    section("ROCKET MODULE LOAD")
    out = ssh("sudo modprobe rocket 2>&1; echo 'modprobe exit:'$?")
    print(out)
    time.sleep(2)

    out = ssh("dmesg | grep -E 'rocket|rk_iommu_attach|trigger_enable|hw_needs_enable|ROCKET|scale|noc_init|PVTPLL' | tail -20")
    print(out)

    # ── Run teflon test ───────────────────────────────────────────────────
    section("TEFLON TEST")
    print("Running python3 /tmp/teflon_test.py ...")
    out = ssh("python3 /tmp/teflon_test.py 2>&1", timeout=120)
    print(out)

    # ── Key dmesg after test ──────────────────────────────────────────────
    section("DMESG DIGEST")
    out = ssh(
        "dmesg | grep -E "
        "'ROCKET|rk_iommu_attach|HW_SUBMIT|REGCMD_TAIL|AMOUNTS|RAW_STATUS|"
        "S_STATUS|trigger_enable|hw_needs_enable|lockup|BUG|WARN|PC_INT|CNA|CORE' "
        "| tail -80"
    )
    print(out)

    # ── IOMMU status at crash point ───────────────────────────────────────
    section("IOMMU STATUS")
    out = ssh("dmesg | grep -E 'rk_iommu|DTE|STATUS|FAULT|PAGE_FAULT' | tail -20")
    print(out)

    # ── restore default to l1 ─────────────────────────────────────────────
    section("RESTORE EXTLINUX DEFAULT=l1")
    out = ssh("sudo sed -i 's/^default l3/default l1/' /boot/extlinux/extlinux.conf && grep '^default' /boot/extlinux/extlinux.conf")
    print(out)

    stop_event.set()
    os.close(serial_fd)
    log_fh.write(f"\n# Session ended: {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
    log_fh.close()
    print(f"\n✓ Log saved: {LOG_FILE}")

if __name__ == "__main__":
    main()
