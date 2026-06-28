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
    
    printf("=== NPU Idle State Cycle Test ===\n\n");
    
    printf("Initial state:\n");
    printf("  SFTCON0: 0x%08x, ST: 0x%08x\n", pmu[0x50/4], pmu[0x68/4]);
    
    // Step 1: Disable all auto-idle
    printf("\nStep 1: Disable all auto-idle\n");
    pmu[0x70/4] = 0xffff0000;  // Clear all NOC auto-idle
    pmu[0x40/4] = 0xffff0000;  // Clear all BUS auto-idle
    usleep(100);
    
    // Step 2: Power off NPU first (set power gate)
    printf("\nStep 2: Power off NPU\n");
    pmu[0xa0/4] = 0x00020002;  // Set bit 1 (power off)
    usleep(10000);
    printf("  PWR_DWN_ST: 0x%08x (bit1=%d)\n", pmu[0x98/4], (pmu[0x98/4]>>1)&1);
    
    // Step 3: Request idle explicitly
    printf("\nStep 3: Request idle\n");
    pmu[0x50/4] = 0x00040004;  // Set bit 2 (request idle)
    usleep(10000);
    printf("  SFTCON0: 0x%08x, ST: 0x%08x, ACK: 0x%08x\n", 
           pmu[0x50/4], pmu[0x68/4], pmu[0x60/4]);
    
    // Step 4: Power on NPU
    printf("\nStep 4: Power on NPU\n");
    pmu[0xa0/4] = 0x00020000;  // Clear bit 1 (power on)
    usleep(10000);
    printf("  PWR_DWN_ST: 0x%08x (bit1=%d)\n", pmu[0x98/4], (pmu[0x98/4]>>1)&1);
    
    // Step 5: Request de-idle
    printf("\nStep 5: Request de-idle\n");
    pmu[0x50/4] = 0x00040000;  // Clear bit 2 (de-idle)
    
    for (int i = 0; i < 10; i++) {
        usleep(10000);
        uint32_t st = pmu[0x68/4];
        uint32_t ack = pmu[0x60/4];
        printf("  After %dms: ST=0x%08x, ACK=0x%08x\n", (i+1)*10, st, ack);
        if (!(st & 0x4)) {
            printf("  SUCCESS!\n");
            break;
        }
    }
    
    munmap((void*)pmu, 0x1000);
    close(fd);
    return 0;
}
