#!/bin/bash
# Run Rocket/Teflon inference test on ROCK 3B
# Prerequisites: booted to l3 (Image-6.19-rocket + fixed DTB), rocket.ko loaded

BOARD_IP="${1:-$(cat ~/.rock3b_ip 2>/dev/null || echo 192.168.1.163)}"

echo "=== Rocket accel driver status ==="
ssh "radxa@${BOARD_IP}" "sudo modprobe rocket 2>/dev/null; dmesg | grep -iE 'rocket|accel0' | tail -10"

echo ""
echo "=== DRM accel device ==="
ssh "radxa@${BOARD_IP}" "ls -la /dev/accel/"

echo ""
echo "=== Running MobileNetV1 inference via Teflon delegate ==="
ssh "radxa@${BOARD_IP}" bash <<'REMOTE'
set -e
TEFLON_LIB="/home/radxa/mesa/build/src/gallium/targets/teflon/libteflon.so"
MODEL_DIR="/home/radxa/rknpu2/examples/rknn_common_test/install/rknn_common_test_Linux"

if [ ! -f "$TEFLON_LIB" ]; then
    echo "ERROR: libteflon.so not found at $TEFLON_LIB"
    exit 1
fi

cd /home/radxa/teflon-test 2>/dev/null || cd /home/radxa

python3 - <<'PYEOF'
import ctypes, sys

# Load the teflon delegate
teflon = ctypes.CDLL("/home/radxa/mesa/build/src/gallium/targets/teflon/libteflon.so")
print(f"Teflon loaded: {teflon}")

try:
    import tflite_runtime.interpreter as tflite
except ImportError:
    import tensorflow.lite as tflite

model_path = "/home/radxa/rknpu2/examples/rknn_common_test/install/rknn_common_test_Linux/model/RK3566_RK3568/mobilenet_v1.rknn"

# Try with teflon delegate
delegate = tflite.load_delegate(
    "/home/radxa/mesa/build/src/gallium/targets/teflon/libteflon.so"
)

interpreter = tflite.Interpreter(
    model_path=model_path,
    experimental_delegates=[delegate]
)
interpreter.allocate_tensors()

import numpy as np
import time

inp = interpreter.get_input_details()[0]
out = interpreter.get_output_details()[0]
print(f"Input: shape={inp['shape']} dtype={inp['dtype']}")
print(f"Output: shape={out['shape']}")

# Warmup
data = np.random.randint(0, 255, inp['shape'], dtype=np.uint8)
interpreter.set_tensor(inp['index'], data)
interpreter.invoke()

# Benchmark
N = 5
t0 = time.perf_counter()
for _ in range(N):
    interpreter.set_tensor(inp['index'], data)
    interpreter.invoke()
t1 = time.perf_counter()
print(f"\nMobileNetV1 ({N} runs): {(t1-t0)/N*1000:.1f} ms/frame")
print("SUCCESS — Rocket inference working!")
PYEOF
REMOTE
