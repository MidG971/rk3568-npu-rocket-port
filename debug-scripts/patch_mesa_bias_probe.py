#!/usr/bin/env python3
# DISCRIMINATOR: override all biases to a large constant. If the NPU output shifts/saturates off
# uniform-128, the BS buffer IS consumed + the MAC computes (buffer is the gate). If output stays
# uniform 128, the gate is upstream (MAC not producing accumulators). Gated by env ROCKET_BIASPROBE.
import sys, time, shutil
P="/home/radxa/mesa/src/gallium/drivers/rocket/rkt_coefs.c"
s=open(P).read()
if "BIASPROBE" in s:
    print("ALREADY PATCHED"); sys.exit(0)
anc='''         biases[oc] = (biases_in[oc] - corr) / (1 << *truncate_bits);'''
if s.count(anc)!=1:
    print("anchor count=%d abort"%s.count(anc)); sys.exit(1)
new=anc+'''
         if (getenv("ROCKET_BIASPROBE")) { biases[oc] = (int32_t)strtol(getenv("ROCKET_BIASPROBE"),0,0); }  /* BIASPROBE */'''
s=s.replace(anc,new,1)
if "#include <stdlib.h>" not in s:
    s=s.replace('#include "util/u_inlines.h"', '#include <stdlib.h>\n#include "util/u_inlines.h"',1)
shutil.copy(P, P+".bak-biasprobe-"+time.strftime("%Y%m%d-%H%M%S"))
open(P,"w").write(s)
print("PATCHED OK")
