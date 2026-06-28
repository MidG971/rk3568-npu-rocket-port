# NPU Debugging Tools

Tools for debugging RK3568 NPU power domain and register access issues.

## Building

On the ROCK 3B board:
```bash
# Copy tools to board
scp -r tools/* radxa@<IP>:~/npu-tools/

# On board, compile
cd ~/npu-tools
make
```

## Tools

### check_pmu_bsp
Reads PMU and CRU registers to show NPU power/idle/clock state.
```bash
sudo ./check_pmu_bsp
```
Output shows:
- PMU bus idle status
- PMU power down status
- CRU clock gate status
- NPU power ON/OFF and bus IDLE/ACTIVE interpretation

### test_npu_reg
Directly reads NPU hardware registers via /dev/mem.
**WARNING**: Will crash/hang if NPU power domain is off!
```bash
sudo ./test_npu_reg
```

### test_open
Tests opening the RKNPU DRM device and calling DRM_IOCTL_VERSION.
Safe to run - doesn't access hardware registers directly.
```bash
./test_open
```

### test_hw_version
Calls RKNPU_GET_HW_VERSION ioctl which triggers driver to read HW version.
On BSP: Should work (driver uses pm_runtime to power on NPU first)
On mainline: May hang if power domain not properly initialized
```bash
./test_hw_version
```

## Expected Results

### BSP Kernel (5.10) - NPU OFF (idle)
```
NPU bus: IDLE (bit2=1)
NPU power: OFF (bit1=1)
CLKGATE_CON32: 0x000087e7  (clocks gated)
```

### BSP Kernel - NPU ON (during access)
```
NPU bus: ACTIVE (bit2=0)
NPU power: ON (bit1=0)
```

### Mainline with our fix - NPU "ON" but doesn't respond
```
NPU bus: ACTIVE (bit2=0)
NPU power: ON (bit1=0)
```
But test_npu_reg causes RCU stall (system hang).
Something beyond PMU power/idle is needed.
