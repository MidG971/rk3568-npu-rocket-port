/* Minimal rknn_api runner: feed a raw INT8 input (pass_through) to a .rknn,
 * run on the NPU, print the golden output stats. Used to capture the working
 * conv cmdstream (kernel REGCMD_FULL_BSP dump) + golden output for Method B.
 * Build: gcc rknn_raw_run.c -o rknn_raw_run -I<api include> -l:librknnrt.so -L<lib dir> -Wl,-rpath,<lib dir>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "rknn_api.h"

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s model.rknn [out.bin]\n", argv[0]); return 1; }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("open model"); return 1; }
    fseek(f, 0, SEEK_END); long msz = ftell(f); fseek(f, 0, SEEK_SET);
    void *model = malloc(msz);
    if (fread(model, 1, msz, f) != (size_t)msz) { perror("read model"); return 1; }
    fclose(f);

    rknn_context ctx = 0;
    int ret = rknn_init(&ctx, model, msz, 0, NULL);
    printf("rknn_init ret=%d\n", ret);
    if (ret < 0) return 1;

    rknn_input_output_num ion;
    rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &ion, sizeof(ion));
    rknn_tensor_attr ia = {0}; ia.index = 0;
    rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &ia, sizeof(ia));
    rknn_tensor_attr oa = {0}; oa.index = 0;
    rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &oa, sizeof(oa));
    printf("in size=%u  out size=%u\n", ia.size, oa.size);

    unsigned char *in = malloc(ia.size);
    for (unsigned i = 0; i < ia.size; i++) in[i] = (unsigned char)((i * 7) & 0xff); /* known ramp */

    rknn_input inp = {0};
    inp.index = 0;
    inp.buf = in;
    inp.size = ia.size;
    inp.pass_through = 1;          /* feed raw bytes straight into the input BO */
    inp.type = RKNN_TENSOR_INT8;
    inp.fmt = RKNN_TENSOR_NHWC;
    ret = rknn_inputs_set(ctx, 1, &inp);
    printf("inputs_set ret=%d\n", ret);

    ret = rknn_run(ctx, NULL);
    printf("rknn_run ret=%d\n", ret);

    rknn_output out = {0};
    out.index = 0; out.want_float = 0; out.is_prealloc = 0;
    ret = rknn_outputs_get(ctx, 1, &out, NULL);
    printf("outputs_get ret=%d size=%u\n", ret, out.size);

    unsigned char *o = out.buf;
    long sum = 0; int seen[256] = {0}, distinct = 0;
    for (unsigned i = 0; i < out.size; i++) { sum += o[i]; if (!seen[o[i]]++) distinct++; }
    printf("OUT: sum=%ld distinct=%d first16=", sum, distinct);
    for (int i = 0; i < 16 && i < (int)out.size; i++) printf("%02x ", o[i]);
    printf("\n");

    if (argc >= 3) { FILE *of = fopen(argv[2], "wb"); fwrite(o, 1, out.size, of); fclose(of);
                     printf("wrote golden output -> %s\n", argv[2]); }

    /* Keep ctx + BOs alive so the regcmd/weight BOs can be dumped via /dev/mem
     * (their PAs are printed by the kernel REGCMD_FULL_BSP line). Set RKNN_HOLD=1. */
    if (getenv("RKNN_HOLD")) { printf("HOLDING 90s for /dev/mem dump...\n"); fflush(stdout); sleep(90); }
    return 0;
}
