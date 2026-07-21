"""
Test: comment out CNA/CORE S_POINTER pre-writes in rocket_job_hw_submit.

Rocket currently writes CNA_S_POINTER and CORE_S_POINTER (each with PP_EN=1 +
PP_MODE=1) before issuing PC OP_EN. BSP rknpu_job_subcore_commit does NOT do
this — it relies entirely on the regcmd to configure sub-units. The pre-write
may leave CNA/CORE in ping-pong mode expecting a second buffer that never
arrives, OR it may conflict with regcmd entries.

Hypothesis: removing the pre-writes lets the regcmd configure CNA/CORE the
way the userspace mesa stack intends. PC's mmu_error bit 14 may stop firing.

If true → BSP-style "regcmd is the source of truth" approach.
If unchanged → suspect moves to OP_EN single-write-vs-pulse.
"""
import sys

PATH = '/home/radxa/npu-port/linux-mainline/drivers/accel/rocket/rocket_job.c'
with open(PATH) as f:
    src = f.read()

old = (
    '\t/* From rknpu, high bits select which NPU core.\n'
    '\t * PP_EN=1 in S_POINTER required by reference driver even in single-buffer mode. */\n'
    '\textra_bit = 0x10000000 * core->index;\n'
    '\trocket_cna_writel(core, S_POINTER, CNA_S_POINTER_POINTER_PP_EN(1) |\n'
    '\t\t\t\t\t   CNA_S_POINTER_EXECUTER_PP_EN(1) |\n'
    '\t\t\t\t\t   CNA_S_POINTER_POINTER_PP_MODE(1) |\n'
    '\t\t\t\t\t   extra_bit);\n'
    '\trocket_core_writel(core, S_POINTER, CORE_S_POINTER_POINTER_PP_EN(1) |\n'
    '\t\t\t\t\t    CORE_S_POINTER_EXECUTER_PP_EN(1) |\n'
    '\t\t\t\t\t    CORE_S_POINTER_POINTER_PP_MODE(1) |\n'
    '\t\t\t\t\t    extra_bit);\n'
)
new = (
    '\t/* TEST (May 12 PM): pre-writes of CNA/CORE S_POINTER removed. BSP rknpu\n'
    '\t * does not pre-write sub-unit S_POINTER — it relies entirely on the regcmd.\n'
    '\t * The pre-write may put CNA/CORE into ping-pong mode expecting a second\n'
    '\t * buffer, contributing to PC mmu_error at ~10ms. */\n'
    '\textra_bit = 0x10000000 * core->index;\n'
    '\t(void)extra_bit;\n'
)

if old not in src:
    print('Anchor NOT FOUND', file=sys.stderr); sys.exit(1)
src = src.replace(old, new, 1)
with open(PATH, 'w') as f:
    f.write(src)
print(f'Wrote {PATH}')
