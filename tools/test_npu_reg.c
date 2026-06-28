/* test_npu_reg.c - Direct NPU register access test via /dev/mem */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>

#define NPU_BASE 0xfde40000
#define NPU_SIZE 0x10000

static sigjmp_buf jump_buffer;
static volatile sig_atomic_t got_signal = 0;

void signal_handler(int sig) {
    got_signal = 1;
    siglongjmp(jump_buffer, 1);
}

int main() {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("Opening /dev/mem...\n");
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem");
        return 1;
    }

    printf("Mapping NPU at 0x%x...\n", NPU_BASE);
    void *map = mmap(NULL, NPU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, NPU_BASE);
    if (map == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    printf("NPU mapped at %p\n", map);

    signal(SIGBUS, signal_handler);
    signal(SIGSEGV, signal_handler);

    if (sigsetjmp(jump_buffer, 1) == 0) {
        volatile unsigned int *reg = (volatile unsigned int *)map;
        printf("Reading NPU_VERSION (offset 0x0)...\n");
        unsigned int version = *reg;
        printf("NPU_VERSION = 0x%08x\n", version);
        printf("SUCCESS: NPU registers accessible!\n");
    } else {
        printf("CRASH: Signal caught - NPU registers not accessible\n");
    }

    munmap(map, NPU_SIZE);
    close(fd);
    return got_signal ? 1 : 0;
}
