#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#define PMU_BASE 0xfdd90000

int main() {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("open"); return 1; }
    
    volatile uint32_t *pmu = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, 
                                   MAP_SHARED, fd, PMU_BASE);
    if (pmu == MAP_FAILED) { perror("mmap"); return 1; }
    
    printf("=== PMU Idle Control Registers ===\n\n");
    printf("BUS_IDLE_CON0 (0x40): 0x%08x  (auto-idle config)\n", pmu[0x40/4]);
    printf("BUS_IDLE_CON1 (0x44): 0x%08x\n", pmu[0x44/4]);
    printf("BUS_IDLE_SFTCON0 (0x50): 0x%08x  (sw idle request)\n", pmu[0x50/4]);
    printf("BUS_IDLE_SFTCON1 (0x54): 0x%08x\n", pmu[0x54/4]);
    printf("BUS_IDLE_ACK (0x60): 0x%08x\n", pmu[0x60/4]);
    printf("BUS_IDLE_ST (0x68): 0x%08x\n", pmu[0x68/4]);
    printf("NOC_AUTO_CON0 (0x70): 0x%08x  (NOC auto-idle)\n", pmu[0x70/4]);
    
    printf("\nNPU-related bits (bit 2):\n");
    printf("  CON0 bit2 (auto-idle enable): %d\n", (pmu[0x40/4] >> 2) & 1);
    printf("  SFTCON0 bit2 (sw idle req): %d\n", (pmu[0x50/4] >> 2) & 1);
    printf("  ST bit2 (is idle): %d\n", (pmu[0x68/4] >> 2) & 1);
    printf("  NOC_AUTO bit2 (noc auto-idle): %d\n", (pmu[0x70/4] >> 2) & 1);
    
    // Try disabling CON0 auto-idle for NPU
    printf("\nDisabling BUS_IDLE_CON0 bit 2 (NPU auto-idle)...\n");
    pmu[0x40/4] = 0x00040000;  // mask=bit2, value=0
    usleep(100);
    printf("BUS_IDLE_CON0 after: 0x%08x\n", pmu[0x40/4]);
    printf("CON0 bit2: %d\n", (pmu[0x40/4] >> 2) & 1);
    
    // Also disable NOC auto-idle
    printf("\nDisabling NOC_AUTO_CON0 bit 2...\n");
    pmu[0x70/4] = 0x00040000;
    usleep(100);
    printf("NOC_AUTO_CON0 after: 0x%08x\n", pmu[0x70/4]);
    
    // Now request de-idle
    printf("\nRequesting de-idle via SFTCON0...\n");
    pmu[0x50/4] = 0x00040000;  // mask=bit2, value=0
    
    for (int i = 0; i < 10; i++) {
        usleep(10000);
        uint32_t st = pmu[0x68/4];
        printf("  After %dms: ST=0x%08x (bit2=%d)\n", (i+1)*10, st, (st>>2)&1);
        if (!(st & 0x4)) {
            printf("  SUCCESS!\n");
            break;
        }
    }
    
    munmap((void*)pmu, 0x1000);
    close(fd);
    return 0;
}
