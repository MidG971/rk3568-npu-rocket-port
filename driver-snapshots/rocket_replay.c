/* rocket_replay.c — Tomeu Method B, rocket side.
 *
 * Replay a known-good vendor conv payload (cmdstream + weights + input) through the
 * mainline ROCKET kernel driver (/dev/accel/accel0) and compare the output to the
 * vendor golden. If rocket produces the zero-point rail (distinct=2) on a byte-exact
 * known-good payload, the engage gate is isolated to the rocket kernel submission path.
 *
 * Payload (captured from a WORKING rknn run of conv2d-bsp.rknn on l1):
 *   regcmd.bin  : 133 64-bit regcmd words (vendor cmdstream), addresses to be patched
 *   weights.bin : NVDLA-packed conv weights (conv2d-bsp.rknn blob @0x1bc0, 51200 B)
 *   input       : ramp byte[i]=(i*7)&0xff, 102400 B  (matches the golden run)
 *   golden.bin  : 204800 B vendor output (distinct=24)
 *
 * The 4 address slots in the cmdstream (by register offset) are patched to OUR BO IOVAs:
 *   0x1070 -> input BO, 0x1110 -> weight BO, 0x4020 -> output BO, 0x5020 -> rdma scratch BO
 * (0x4054=0xfffffffb and 0x4080=0xffffff80 are CONFIG constants, NOT addresses — do not patch.)
 *
 * Build (on board): gcc rocket_replay.c -o rocket_replay
 * Run  (on l3):     ./rocket_replay regcmd.bin weights.bin golden.bin
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

/* ---- rocket UABI (self-contained, from include/uapi/drm/rocket_accel.h) ---- */
#define DRM_COMMAND_BASE 0x40
struct drm_rocket_create_bo { uint32_t size; uint32_t handle; uint64_t dma_address; uint64_t offset; };
struct drm_rocket_prep_bo   { uint32_t handle; uint32_t reserved; int64_t timeout_ns; };
struct drm_rocket_fini_bo   { uint32_t handle; uint32_t reserved; };
struct drm_rocket_task      { uint32_t regcmd; uint32_t regcmd_count; };
struct drm_rocket_job {
    uint64_t tasks; uint64_t in_bo_handles; uint64_t out_bo_handles;
    uint32_t task_count; uint32_t task_struct_size;
    uint32_t in_bo_handle_count; uint32_t out_bo_handle_count;
};
struct drm_rocket_submit { uint64_t jobs; uint32_t job_count; uint32_t job_struct_size; uint64_t reserved; };

#define DRM_IOCTL_ROCKET_CREATE_BO _IOWR('d', DRM_COMMAND_BASE+0x00, struct drm_rocket_create_bo)
#define DRM_IOCTL_ROCKET_SUBMIT    _IOW ('d', DRM_COMMAND_BASE+0x01, struct drm_rocket_submit)
#define DRM_IOCTL_ROCKET_PREP_BO   _IOWR('d', DRM_COMMAND_BASE+0x02, struct drm_rocket_prep_bo)
#define DRM_IOCTL_ROCKET_FINI_BO   _IOWR('d', DRM_COMMAND_BASE+0x03, struct drm_rocket_fini_bo)

#define IN_SIZE   102400u   /* 80*80*16 */
#define OUT_SIZE  204800u   /* 40*40*128 */
#define RDMA_SIZE 262144u   /* scratch for 0x5020 (size unknown; zeroed) */

typedef struct { uint32_t handle; uint64_t dma; uint64_t moff; void *cpu; uint32_t size; } bo_t;

static int g_fd;

static int create_bo(bo_t *b, uint32_t size)
{
    struct drm_rocket_create_bo a = { .size = size };
    if (ioctl(g_fd, DRM_IOCTL_ROCKET_CREATE_BO, &a)) { perror("CREATE_BO"); return -1; }
    b->handle = a.handle; b->dma = a.dma_address; b->moff = a.offset; b->size = size;
    b->cpu = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, g_fd, a.offset);
    if (b->cpu == MAP_FAILED) { perror("mmap"); return -1; }
    if (a.dma_address >> 32) fprintf(stderr, "WARN: BO dma 0x%llx > 32-bit\n",
                                     (unsigned long long)a.dma_address);
    fprintf(stderr, "  BO size=%u handle=%u dma=0x%08llx\n", size, b->handle,
            (unsigned long long)b->dma);
    return 0;
}
static void fini_bo(bo_t *b){ struct drm_rocket_fini_bo a={.handle=b->handle};
    if (ioctl(g_fd, DRM_IOCTL_ROCKET_FINI_BO, &a)) perror("FINI_BO"); }
static void prep_bo(bo_t *b, int64_t ns){ struct drm_rocket_prep_bo a={.handle=b->handle,.timeout_ns=ns};
    if (ioctl(g_fd, DRM_IOCTL_ROCKET_PREP_BO, &a)) perror("PREP_BO"); }

static void *load_file(const char *p, long *outlen)
{
    FILE *f = fopen(p, "rb"); if (!f){ perror(p); exit(1); }
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    void *m = malloc(n); if (fread(m,1,n,f)!=(size_t)n){ perror("read"); exit(1); }
    fclose(f); if (outlen) *outlen=n; return m;
}

/* patch the value field (bits[47:16]) of the regcmd word whose register offset (bits[15:0]) == off */
static int patch_addr(uint64_t *rc, int count, uint32_t off, uint32_t iova)
{
    int patched = 0;
    for (int i = 0; i < count; i++) {
        if ((rc[i] & 0xffff) == off) {
            rc[i] = (rc[i] & ~(0xFFFFFFFFULL << 16)) | ((uint64_t)iova << 16);
            fprintf(stderr, "  patched off=0x%04x -> iova=0x%08x (entry %d)\n", off, iova, i);
            patched++;
        }
    }
    return patched;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s regcmd.bin weights.bin [golden.bin]\n", argv[0]); return 1; }
    long rclen=0, wlen=0, glen=0;
    uint64_t *regcmd = load_file(argv[1], &rclen);
    int rc_count = rclen / 8;
    void *weights = load_file(argv[2], &wlen);
    void *golden = argc>=4 ? load_file(argv[3], &glen) : NULL;
    fprintf(stderr, "regcmd %d words, weights %ld B\n", rc_count, wlen);

    g_fd = open("/dev/accel/accel0", O_RDWR);
    if (g_fd < 0) { perror("open /dev/accel/accel0"); return 1; }

    bo_t in={0}, wt={0}, out={0}, rdma={0}, rcbo={0};
    if (create_bo(&in,   IN_SIZE))   return 1;
    if (create_bo(&wt,   (wlen+4095)&~4095u)) return 1;
    if (create_bo(&out,  OUT_SIZE))  return 1;
    if (create_bo(&rdma, RDMA_SIZE)) return 1;
    if (create_bo(&rcbo, (rclen+4095)&~4095u)) return 1;

    /* fill */
    for (uint32_t i=0;i<IN_SIZE;i++) ((uint8_t*)in.cpu)[i] = (uint8_t)((i*7)&0xff); /* same ramp as golden run */
    memcpy(wt.cpu, weights, wlen);
    memset(out.cpu, 0, OUT_SIZE);
    memset(rdma.cpu, 0, RDMA_SIZE);

    /* patch the 4 real address slots, then copy regcmd into its BO */
    patch_addr(regcmd, rc_count, 0x1070, (uint32_t)in.dma);
    patch_addr(regcmd, rc_count, 0x1110, (uint32_t)wt.dma);
    patch_addr(regcmd, rc_count, 0x4020, (uint32_t)out.dma);
    patch_addr(regcmd, rc_count, 0x5020, (uint32_t)rdma.dma);
    memcpy(rcbo.cpu, regcmd, rclen);

    /* hand BOs to the device */
    fini_bo(&in); fini_bo(&wt); fini_bo(&out); fini_bo(&rdma); fini_bo(&rcbo);

    struct drm_rocket_task task = { .regcmd = (uint32_t)rcbo.dma, .regcmd_count = rc_count };
    uint32_t in_handles[4]  = { rcbo.handle, in.handle, wt.handle, rdma.handle };
    uint32_t out_handles[1] = { out.handle };
    struct drm_rocket_job job = {
        .tasks = (uint64_t)(uintptr_t)&task,
        .in_bo_handles = (uint64_t)(uintptr_t)in_handles,
        .out_bo_handles = (uint64_t)(uintptr_t)out_handles,
        .task_count = 1, .task_struct_size = sizeof(task),
        .in_bo_handle_count = 4, .out_bo_handle_count = 1,
    };
    struct drm_rocket_submit submit = {
        .jobs = (uint64_t)(uintptr_t)&job, .job_count = 1, .job_struct_size = sizeof(job),
    };
    fprintf(stderr, "SUBMIT: regcmd_iova=0x%08llx count=%d\n", (unsigned long long)rcbo.dma, rc_count);
    if (ioctl(g_fd, DRM_IOCTL_ROCKET_SUBMIT, &submit)) { perror("SUBMIT"); return 1; }

    prep_bo(&out, 2000000000LL); /* wait up to 2s + sync for CPU read */

    uint8_t *o = out.cpu;
    long sum=0; int seen[256]={0}, distinct=0;
    for (uint32_t i=0;i<OUT_SIZE;i++){ sum+=o[i]; if(!seen[o[i]]++) distinct++; }
    printf("ROCKET OUT: sum=%ld distinct=%d first16=", sum, distinct);
    for (int i=0;i<16;i++) printf("%02x ", o[i]);
    printf("\n");
    if (golden) {
        int diff=0; for (uint32_t i=0;i<OUT_SIZE && i<(uint32_t)glen;i++) if(o[i]!=((uint8_t*)golden)[i]) diff++;
        printf("vs golden: %d / %u bytes differ  => %s\n", diff, OUT_SIZE,
               diff==0 ? "MATCH (rocket replays the vendor conv!)" :
               (distinct<=2 ? "ZERO-POINT RAIL (rocket kernel driver isolated as the gate)" : "PARTIAL"));
    }
    return 0;
}
