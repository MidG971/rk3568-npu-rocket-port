#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#define PMU_BASE 0xfdd90000
#define CRU_BASE 0xfdd20000

int main() {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("open"); return 1; }
    
    volatile uint32_t *pmu = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, 
                                   MAP_SHARED, fd, PMU_BASE);
    volatile uint32_t *cru = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, 
                                   MAP_SHARED, fd, CRU_BASE);
    if (pmu == MAP_FAILED || cru == MAP_FAILED) { perror("mmap"); return 1; }
    
    printf("=== NPU Clock + Reset + Power Cycle Test ===\n\n");
    
    // Step 1: Disable auto-idle
    printf("Step 1: Disable auto-idle\n");
    pmu[0x70/4] = 0xffff0000;
    
    // Step 2: Gate NPU clocks (set gate bits)
    printf("\nStep 2: Gate NPU clocks\n");
    // CRU_CLKGATE_CON26 (0x368) has NPU clock gates
    // Bit 4: aclk_npu_pre, Bit 5: hclk_npu_pre, Bit 6: clk_npu_src
    printf("  Before: CLKGATE_CON26=0x%08x\n", cru[0x368/4]);
    cru[0x368/4] = 0x00700070;  // Gate clk_npu, aclk, hclk
    usleep(1000);
    printf("  After gate: CLKGATE_CON26=0x%08x\n", cru[0x368/4]);
    
    // Step 3: Assert resets
    printf("\nStep 3: Assert NPU resets\n");
    cru[0x408/4] = 0x18001800;  // Assert reset bits 11,12
    usleep(1000);
    
    // Step 4: Power off
    printf("\nStep 4: Power off NPU\n");
    pmu[0xa0/4] = 0x00020002;
    usleep(10000);
    printf("  PWR_DWN_ST: 0x%08x\n", pmu[0x98/4]);
    
    // Step 5: Power on
    printf("\nStep 5: Power on NPU\n");
    pmu[0xa0/4] = 0x00020000;
    usleep(10000);
    printf("  PWR_DWN_ST: 0x%08x\n", pmu[0x98/4]);
    
    // Step 6: Ungate clocks
    printf("\nStep 6: Ungate NPU clocks\n");
    cru[0x368/4] = 0x00700000;  // Ungate
    usleep(1000);
    printf("  CLKGATE_CON26=0x%08x\n", cru[0x368/4]);
    
    // Step 7: Deassert resets
    printf("\nStep 7: Deassert NPU resets\n");
    cru[0x408/4] = 0x18000000;
    usleep(1000);
    
    // Step 8: De-idle
    printf("\nStep 8: Request de-idle\n");
    pmu[0x50/4] = 0x00040000;
    
    for (int i = 0; i < 10; i++) {
        usleep(10000);
        uint32_t st = pmu[0x68/4];
        printf("  After %dms: ST=0x%08x\n", (i+1)*10, st);
        if (!(st & 0x4)) {
            printf("  SUCCESS!\n");
            break;
        }
    }
    
    munmap((void*)pmu, 0x1000);
    munmap((void*)cru, 0x1000);
    close(fd);
    return 0;
}
