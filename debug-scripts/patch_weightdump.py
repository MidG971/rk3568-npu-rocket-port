#!/usr/bin/env python3
# Extend the WEIGHT_BO dump in rocket_job.c from 16 bytes to 512 (32 lines x16),
# so mesa's runtime weight-buffer layout can be compared against the vendor's.
import re, sys, time, shutil
PATH="/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_job.c"
src=open(PATH).read()
if "WEIGHT_BO[%03x]" in src:
    print("ALREADY PATCHED"); sys.exit(0)

# 1) widen the memremap window 128 -> 512
n1=src.count("memremap((phys_addr_t)wpa, 128, MEMREMAP_WB)")
if n1!=1:
    print("memremap anchor count=%d, abort"%n1); sys.exit(1)
src=src.replace("memremap((phys_addr_t)wpa, 128, MEMREMAP_WB)",
                "memremap((phys_addr_t)wpa, 512, MEMREMAP_WB)",1)

# 2) replace the single 16-byte dev_err with a 512-byte loop
pat=re.compile(r'dev_err\(core->dev,\s*"WEIGHT_BO iova=0x%x pa=0x%llx:.*?b\[15\]\);', re.DOTALL)
m=pat.search(src)
if not m:
    print("WEIGHT_BO dev_err not found, abort"); sys.exit(1)
loop=('{ int _wi; for (_wi = 0; _wi < 512; _wi += 16) dev_err(core->dev, '
      '"WEIGHT_BO[%03x] iova=0x%x: '
      '%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", '
      '_wi, weight_iova, '
      'b[_wi+0], b[_wi+1], b[_wi+2], b[_wi+3], b[_wi+4], b[_wi+5], b[_wi+6], b[_wi+7], '
      'b[_wi+8], b[_wi+9], b[_wi+10], b[_wi+11], b[_wi+12], b[_wi+13], b[_wi+14], b[_wi+15]); }')
src=src[:m.start()]+loop+src[m.end():]

shutil.copy(PATH, PATH+".bak-wdump-"+time.strftime("%Y%m%d-%H%M%S"))
open(PATH,"w").write(src)
print("PATCHED OK")
