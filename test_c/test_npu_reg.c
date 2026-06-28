#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>

#define NPU_BASE 0xfde40000

static jmp_buf jump_buf;
static volatile int fault_occurred = 0;

void handler(int sig) {
    fault_occurred = 1;
    longjmp(jump_buf, 1);
}

int main() {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("open"); return 1; }
    
    volatile uint32_t *npu = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, 
                                   MAP_SHARED, fd, NPU_BASE);
    if (npu == MAP_FAILED) { perror("mmap"); return 1; }
    
    signal(SIGSEGV, handler);
    signal(SIGBUS, handler);
    
    printf("=== NPU Register Access Test ===\n\n");
    printf("Trying to read NPU_VERSION at 0xfde40000...\n");
    
    if (setjmp(jump_buf) == 0) {
        uint32_t version = npu[0];
        printf("SUCCESS! NPU_VERSION = 0x%08x\n", version);
    } else {
        printf("FAILED! Got signal (sync abort/bus error)\n");
    }
    
    munmap((void*)npu, 0x10000);
    close(fd);
    return 0;
}
