#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "rknn_api.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s model.rknn\n", argv[0]);
        return 1;
    }

    /* Load model */
    FILE *f = fopen(argv[1], "rb");
    if (!f) { printf("Failed to open model\n"); return 1; }
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *model = malloc(size);
    fread(model, 1, size, f);
    fclose(f);
    printf("Loaded model: %zu bytes\n", size);

    /* Init */
    rknn_context ctx = 0;
    printf("Initializing...\n");
    int ret = rknn_init(&ctx, model, size, 0, NULL);
    if (ret != RKNN_SUCC) {
        printf("rknn_init failed: %d\n", ret);
        return 1;
    }
    printf("Init OK!\n");

    /* Query input/output */
    rknn_input_output_num io_num;
    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    printf("Inputs: %d, Outputs: %d\n", io_num.n_input, io_num.n_output);

    /* Query input attr */
    rknn_tensor_attr input_attr;
    memset(&input_attr, 0, sizeof(input_attr));
    input_attr.index = 0;
    ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &input_attr, sizeof(input_attr));
    printf("Input[0]: %s, dims=[%d,%d,%d,%d], size=%d\n",
           input_attr.name, input_attr.dims[0], input_attr.dims[1],
           input_attr.dims[2], input_attr.dims[3], input_attr.size);

    /* Set input */
    unsigned char *input_data = calloc(1, input_attr.size);
    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].buf = input_data;
    inputs[0].size = input_attr.size;
    inputs[0].type = RKNN_TENSOR_UINT8;
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].pass_through = 0;

    printf("Setting input (%d bytes)...\n", input_attr.size);
    ret = rknn_inputs_set(ctx, 1, inputs);
    if (ret != RKNN_SUCC) {
        printf("rknn_inputs_set failed: %d\n", ret);
        return 1;
    }

    /* Run inference */
    printf("Running inference...\n");
    struct timeval t1, t2;
    gettimeofday(&t1, NULL);
    ret = rknn_run(ctx, NULL);
    gettimeofday(&t2, NULL);
    if (ret != RKNN_SUCC) {
        printf("rknn_run failed: %d\n", ret);
        return 1;
    }
    float ms = (t2.tv_sec - t1.tv_sec) * 1000.0 + (t2.tv_usec - t1.tv_usec) / 1000.0;
    printf("Inference SUCCESS! Time: %.2f ms\n", ms);

    /* Cleanup */
    rknn_destroy(ctx);
    free(model);
    free(input_data);
    return 0;
}
