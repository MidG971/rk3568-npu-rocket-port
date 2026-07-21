#!/usr/bin/env python3
# STEP 1: un-bypass SDP BS_MUL + per-tensor requant (mult/shift from operation scales).
# Fixes per-TENSOR int8 conv (conv2d-cal). Per-channel buffer = later STEP 2.
import sys, time, shutil
P="/home/radxa/mesa/src/gallium/drivers/rocket/rkt_regcmd.c"
s=open(P).read()
if "rkt requant STEP1" in s:
    print("ALREADY PATCHED"); sys.exit(0)

old='''   EMIT(REG_DPU_BS_CFG, DPU_BS_CFG_BS_ALU_ALGO(2) | DPU_BS_CFG_BS_ALU_SRC(1) |
                           DPU_BS_CFG_BS_RELU_BYPASS(1) |
                           DPU_BS_CFG_BS_MUL_BYPASS(1));
   EMIT(REG_DPU_BS_ALU_CFG, 0);
   EMIT(REG_DPU_BS_MUL_CFG, 0);'''
if s.count(old)!=1:
    print("BS-block anchor count=%d abort"%s.count(old)); sys.exit(1)
new='''   /* rkt requant STEP1: un-bypass BS_MUL, per-tensor requant ratio. */
   {
      double _r = (double)operation->weights_scale * (double)operation->input_scale /
                  (double)operation->output_scale;
      double _m = _r; int _sh = 0; unsigned _mult, _shift;
      if (_m > 0.0) {
         while (_m < 16384.0 && _sh < 62) { _m *= 2.0; _sh++; }
         while (_m >= 65536.0 && _sh > 0) { _m /= 2.0; _sh--; }
         _mult = (unsigned)(_m + 0.5);
         if (_mult > 0xffff) _mult = 0xffff;
         _shift = (unsigned)_sh;
      } else { _mult = 0; _shift = 0; }
      fprintf(stderr, "RKT_REQUANT: ratio=%g mult=%u shift=%u (ws=%g is=%g os=%g)\\n",
              _r, _mult, _shift, operation->weights_scale, operation->input_scale,
              operation->output_scale);
      EMIT(REG_DPU_BS_CFG, DPU_BS_CFG_BS_ALU_ALGO(2) | DPU_BS_CFG_BS_ALU_SRC(1) |
                              DPU_BS_CFG_BS_RELU_BYPASS(1));
      EMIT(REG_DPU_BS_ALU_CFG, 0);
      EMIT(REG_DPU_BS_MUL_CFG, DPU_BS_MUL_CFG_BS_MUL_OPERAND(_mult) |
                                  DPU_BS_MUL_CFG_BS_MUL_SHIFT_VALUE(_shift));
   }'''
s=s.replace(old,new,1)
# ensure stdio for fprintf
if "#include <stdio.h>" not in s:
    s=s.replace('#include "rkt_regcmd.h"', '#include <stdio.h>\n#include "rkt_regcmd.h"', 1)
shutil.copy(P, P+".bak-step1-"+time.strftime("%Y%m%d-%H%M%S"))
open(P,"w").write(s)
print("PATCHED OK")
