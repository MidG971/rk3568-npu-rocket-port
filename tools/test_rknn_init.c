/* Simple RKNN init test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RKNN API types */
typedef unsigned long long rknn_context;
typedef int (*rknn_init_func)(rknn_context*, void*, unsigned int, unsigned int, void*);
typedef int (*rknn_destroy_func)(rknn_context);
typedef int (*rknn_query_func)(rknn_context, int, void*, unsigned int);

#include <dlfcn.h>

int main(int argc, char **argv) {
    void *lib = dlopen("librknnrt.so", RTLD_NOW);
    if (!lib) {
        printf("Failed to load librknnrt.so: %s\n", dlerror());
        return 1;
    }
    printf("Loaded librknnrt.so\n");

    rknn_init_func init = (rknn_init_func)dlsym(lib, "rknn_init");
    rknn_destroy_func destroy = (rknn_destroy_func)dlsym(lib, "rknn_destroy");
    rknn_query_func query = (rknn_query_func)dlsym(lib, "rknn_query");

    if (!init || !destroy || !query) {
        printf("Failed to find rknn functions\n");
        dlclose(lib);
        return 1;
    }
    printf("Found rknn_init, rknn_destroy, rknn_query\n");

    if (argc < 2) {
        printf("Usage: %s model.rknn\n", argv[0]);
        dlclose(lib);
        return 0;
    }

    /* Load model file */
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        printf("Failed to open model file\n");
        dlclose(lib);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *model = malloc(size);
    fread(model, 1, size, f);
    fclose(f);
    printf("Loaded model: %zu bytes\n", size);

    /* Init RKNN */
    rknn_context ctx = 0;
    printf("Calling rknn_init...\n");
    int ret = init(&ctx, model, size, 0, NULL);
    printf("rknn_init returned: %d, ctx=%llu\n", ret, ctx);

    if (ret == 0) {
        /* Query SDK version */
        struct { char api[256]; char drv[256]; } ver = {0};
        query(ctx, 0, &ver, sizeof(ver));  /* RKNN_QUERY_SDK_VERSION = 0 */
        printf("SDK API: %s\n", ver.api);
        printf("SDK DRV: %s\n", ver.drv);
        destroy(ctx);
    }

    free(model);
    dlclose(lib);
    return ret;
}
