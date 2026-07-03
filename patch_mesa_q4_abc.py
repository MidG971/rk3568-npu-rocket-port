#!/usr/bin/env python3
# Port gahingwoo's byte-exact RK3576 int8 requant fix to the RK3568 (=RK3588-map) LEGACY path.
# See memory: rk3568-q4-port-2026-07-03, coeff-buffer-SOLVED-gahingwoo-2026-07-01.
#
# Two edits, both in the legacy (RK3588) path that RK3568 actually uses:
#  (1) rkt_coefs.c rkt_fill_biases: emit the A/B/C coefficient buffer instead of bias-only.
#        per 8-oc group (64B): [8x int32 A | 8x int16 B | 8x int16 C]
#        A[oc] = biases_in[oc] - (in_zp-0x80)*sw[oc]     (in_zp=0x80 -> = bias)
#        B     = 0x80 - wt_zp                            (per-channel weight-zp correction)
#        C[oc] = round(16 * wscales[oc]/max_sc)          (Q4; per-tensor -> 16 identity)
#      NB the ABSOLUTE scale already rides the (derived) DPU OUT_CVT for plain convs.
#  (2) rkt_regcmd.c BS block: un-bypass BS_MUL, read A(ALU)+C(MUL) from the buffer.
#        env knobs (swept on board, no rebuild): ROCKET_ABC=1 arms the whole path;
#        ROCKET_C_VALUE forces C; ROCKET_BS_MULCFG / ROCKET_BRDMA_CFG sweep the
#        uncertain RK3588-map consumption bits; ROCKET_NO_RELU_BYPASS toggles RELU.
import sys, time, shutil, os

COEFS="/home/radxa/mesa/src/gallium/drivers/rocket/rkt_coefs.c"
REGCMD="/home/radxa/mesa/src/gallium/drivers/rocket/rkt_regcmd.c"

# ---- (1) rkt_coefs.c : calculate_weight_sum helper + A/B/C buffer -------------
c=open(COEFS).read()
if "ROCKET_ABC" in c:
    print("coefs ALREADY PATCHED"); sys.exit(0)

# add the weight-sum helper just before rkt_fill_biases
anchor_fn="struct pipe_resource *\nrkt_fill_biases(struct rkt_ml_subgraph *subgraph,"
helper='''/* Sum over the kernel of (weight - weight_zero_point), per output channel. */
static int32_t
rkt_calc_weight_sum(const struct pipe_ml_operation *poperation, unsigned oc, void *map)
{
   unsigned input_channels = poperation->input_tensors[0]->dims[3];
   unsigned ww = poperation->conv.weight_tensor->dims[1];
   unsigned wh = poperation->conv.weight_tensor->dims[2];
   unsigned wzp = poperation->conv.weight_tensor->zero_point;
   uint8_t(*w)[ww][wh][input_channels] = map;
   int32_t sum = 0;
   if (rkt_is_depthwise(poperation)) {
      for (unsigned x = 0; x < ww; x++)
         for (unsigned y = 0; y < wh; y++)
            sum += w[0][x][y][oc] - wzp;
   } else {
      for (unsigned x = 0; x < ww; x++)
         for (unsigned y = 0; y < wh; y++)
            for (unsigned ic = 0; ic < input_channels; ic++)
               sum += w[oc][x][y][ic] - wzp;
   }
   return sum;
}

'''
assert c.count(anchor_fn)==1, "fill_biases anchor count %d"%c.count(anchor_fn)
c=c.replace(anchor_fn, helper+anchor_fn, 1)

# insert the A/B/C early-return right after `biases = pipe_buffer_map(...)`
anchor_map='''   rsc = pipe_buffer_create(pcontext->screen, 0, PIPE_USAGE_DEFAULT,
                            output_channels * sizeof(uint32_t));
   biases = pipe_buffer_map(pcontext, rsc, PIPE_MAP_WRITE, &transfer_out);'''
abc='''   /* RK3568 Q4 requant: A/B/C coefficient buffer (gahingwoo RK3576 layout ported to
    * the legacy path). Gated on ROCKET_ABC so the old bias-only path stays default. */
   if (getenv("ROCKET_ABC")) {
      unsigned in_zp = poperation->input_tensors[0]->zero_point;
      unsigned wt_zp = poperation->conv.weight_tensor->zero_point;
      const float *wsc = poperation->conv.weight_tensor->scales;
      float max_sc = wsc ? wsc[0] : 1.0f;
      if (wsc)
         for (unsigned oc = 1; oc < output_channels; oc++)
            if (wsc[oc] > max_sc) max_sc = wsc[oc];
      unsigned groups = DIV_ROUND_UP(output_channels, 8);
      unsigned bufsize = groups * 64 + 0x100;
      const char *cval = getenv("ROCKET_C_VALUE");
      *truncate_bits = 0;
      rsc = pipe_buffer_create(pcontext->screen, 0, PIPE_USAGE_DEFAULT, bufsize);
      uint8_t *b8 = pipe_buffer_map(pcontext, rsc, PIPE_MAP_WRITE, &transfer_out);
      memset(b8, 0, bufsize);
      for (unsigned oc = 0; oc < output_channels; oc++) {
         int32_t sw = rkt_calc_weight_sum(poperation, oc, weights);
         unsigned g = oc / 8, i = oc % 8;
         int32_t *A = (int32_t *)(b8 + g * 64 + i * 4);
         int16_t *B = (int16_t *)(b8 + g * 64 + 32 + i * 2);
         int16_t *C = (int16_t *)(b8 + g * 64 + 48 + i * 2);
         float rel = wsc ? (wsc[oc] / max_sc) : 1.0f;
         *A = biases_in[oc] - ((int32_t)in_zp - 0x80) * sw;
         *B = (int16_t)(0x80 - (int)wt_zp);
         *C = cval ? (int16_t)strtol(cval, NULL, 0) : (int16_t)(16.0f * rel + 0.5f);
      }
      fprintf(stderr, "rocket: ABC buffer oc=%u groups=%u bufsize=%u wt_zp=%u in_zp=%u max_sc=%g\\n",
              output_channels, groups, bufsize, wt_zp, in_zp, max_sc);
      if (DBG_ENABLED(ROCKET_DBG_DUMP_BOS)) {
         static int t = 0; rkt_dump_buffer(b8, "biases", 0, t++, 0, bufsize);
      }
      pipe_buffer_unmap(pcontext, transfer_out);
      pipe_buffer_unmap(pcontext, transfer_weights);
      pipe_buffer_unmap(pcontext, transfer_in);
      return rsc;
   }

'''+anchor_map
assert c.count(anchor_map)==1, "map anchor count %d"%c.count(anchor_map)
c=c.replace(anchor_map, abc, 1)

# ensure includes
if "#include <string.h>" not in c:
    c=c.replace('#include "util/u_inlines.h"', '#include <stdlib.h>\n#include <string.h>\n#include "util/u_inlines.h"', 1)

shutil.copy(COEFS, COEFS+".bak-q4-"+time.strftime("%Y%m%d-%H%M%S"))
open(COEFS,"w").write(c)
print("coefs PATCHED")

# ---- (2) rkt_regcmd.c : BS block reads A(ALU)+C(MUL) from the buffer ----------
r=open(REGCMD).read()
if "ROCKET_ABC" in r:
    print("regcmd ALREADY PATCHED"); sys.exit(0)
bs_old='''   EMIT(REG_DPU_BS_CFG, DPU_BS_CFG_BS_ALU_ALGO(2) | DPU_BS_CFG_BS_ALU_SRC(1) |
                           DPU_BS_CFG_BS_RELU_BYPASS(1) |
                           DPU_BS_CFG_BS_MUL_BYPASS(1));
   EMIT(REG_DPU_BS_ALU_CFG, 0);
   EMIT(REG_DPU_BS_MUL_CFG, 0);'''
bs_new='''   if (getenv("ROCKET_ABC")) {
      /* Q4 path: A(bias) via BS_ALU_SRC and C(per-channel mul) via BS_MUL_SRC, both
       * streamed from the A/B/C coefficient buffer. Un-bypass BS_MUL. RELU bypass
       * unless a fused relu is wanted (ROCKET_NO_RELU_BYPASS clears it). The exact
       * RK3588-map consumption bits are sweepable without a rebuild. */
      uint32_t bscfg = DPU_BS_CFG_BS_ALU_ALGO(2) | DPU_BS_CFG_BS_ALU_SRC(1);
      if (!getenv("ROCKET_NO_RELU_BYPASS"))
         bscfg |= DPU_BS_CFG_BS_RELU_BYPASS(1);
      EMIT(REG_DPU_BS_CFG, getenv("ROCKET_BS_CFG")
                              ? (uint32_t)strtoul(getenv("ROCKET_BS_CFG"), NULL, 0)
                              : bscfg);
      EMIT(REG_DPU_BS_ALU_CFG, 0);
      /* BS_MUL_SRC=1 (operand from buffer) + BS_TRUNCATE_SRC=1. */
      EMIT(REG_DPU_BS_MUL_CFG, getenv("ROCKET_BS_MULCFG")
                                  ? (uint32_t)strtoul(getenv("ROCKET_BS_MULCFG"), NULL, 0)
                                  : (DPU_BS_MUL_CFG_BS_MUL_SRC(1) |
                                     DPU_BS_MUL_CFG_BS_TRUNCATE_SRC(1)));
   } else {
   EMIT(REG_DPU_BS_CFG, DPU_BS_CFG_BS_ALU_ALGO(2) | DPU_BS_CFG_BS_ALU_SRC(1) |
                           DPU_BS_CFG_BS_RELU_BYPASS(1) |
                           DPU_BS_CFG_BS_MUL_BYPASS(1));
   EMIT(REG_DPU_BS_ALU_CFG, 0);
   EMIT(REG_DPU_BS_MUL_CFG, 0);
   }'''
assert r.count(bs_old)==1, "BS block anchor count %d"%r.count(bs_old)
r=r.replace(bs_old, bs_new, 1)

# widen BRDMA_DATA_USE + add the second BS buffer pointer (sweepable)
brdma_old='''   EMIT(REG_DPU_RDMA_RDMA_BRDMA_CFG, DPU_RDMA_RDMA_BRDMA_CFG_BRDMA_DATA_USE(1));'''
brdma_new='''   EMIT(REG_DPU_RDMA_RDMA_BRDMA_CFG,
        getenv("ROCKET_BRDMA_CFG")
           ? (uint32_t)strtoul(getenv("ROCKET_BRDMA_CFG"), NULL, 0)
           : DPU_RDMA_RDMA_BRDMA_CFG_BRDMA_DATA_USE(getenv("ROCKET_ABC") ? 7 : 1));'''
assert r.count(brdma_old)==1, "BRDMA anchor count %d"%r.count(brdma_old)
r=r.replace(brdma_old, brdma_new, 1)

if "#include <stdlib.h>" not in r:
    r=r.replace('#include "rkt_regcmd.h"', '#include <stdlib.h>\n\n#include "rkt_regcmd.h"', 1)

shutil.copy(REGCMD, REGCMD+".bak-q4-"+time.strftime("%Y%m%d-%H%M%S"))
open(REGCMD,"w").write(r)
print("regcmd PATCHED")
print("OK - build then run with ROCKET_ABC=1")
