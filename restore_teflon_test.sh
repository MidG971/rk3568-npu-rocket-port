#!/bin/bash
# Restore /tmp/teflon_test.py on board after reboot
# Run from local: ./restore_teflon_test.sh
BOARD=radxa@192.168.1.163

ssh $BOARD "cat > /tmp/teflon_test.py" << 'PYEOF'
#!/usr/bin/env python3
"""MobileNetV1 inference test via Teflon (Mesa Gallium Rocket delegate)"""
import sys, time
sys.path.insert(0, '/home/radxa/mesa/src/gallium/frontends/teflon/tests')

import numpy as np
from PIL import Image
import tflite_runtime.interpreter as tflite

MODEL   = '/home/radxa/teflon-test/mobilenet_v1_1_224_quant.tflite'
IMAGE   = '/home/radxa/teflon-test/test.jpg'
LABELS  = '/home/radxa/teflon-test/labels.txt'
DELEGATE = '/home/radxa/mesa/build/src/gallium/targets/teflon/libteflon.so'

print(f"Loading delegate: {DELEGATE}")
ext_delegate = [tflite.load_delegate(DELEGATE)]

interp = tflite.Interpreter(model_path=MODEL, experimental_delegates=ext_delegate)
interp.allocate_tensors()

inp  = interp.get_input_details()
out  = interp.get_output_details()
h, w = inp[0]['shape'][1], inp[0]['shape'][2]

img = np.array(Image.open(IMAGE).resize((w, h))).astype(inp[0]['dtype'])
interp.set_tensor(inp[0]['index'], np.expand_dims(img, 0))

# warmup
interp.invoke()

N = 4
t0 = time.time()
for _ in range(N):
    interp.invoke()
elapsed = (time.time() - t0) * 1000 / N

results = np.squeeze(interp.get_tensor(out[0]['index']))
labels  = open(LABELS).read().splitlines()
top5    = results.argsort()[-5:][::-1]

print("Top-5:")
for i in top5:
    print(f"  {results[i]/255.0:.4f}  {labels[i]}")
print(f"\nAvg inference: {elapsed:.1f} ms")
PYEOF

echo "teflon_test.py restored to /tmp"
