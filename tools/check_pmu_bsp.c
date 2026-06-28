/* check_pmu_bsp.c - PMU register state checker for RK3568 NPU debugging */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>

#define PMU_BASE 0xfdd90000
#define PMU_SIZE 0x1000
#define CRU_BASE 0xfdd20000
#define CRU_SIZE 0x1000

int main() {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem");
        return 1;
    }

    void *pmu = mmap(NULL, PMU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, PMU_BASE);
    void *cru = mmap(NULL, CRU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, CRU_BASE);

    if (pmu == MAP_FAILED || cru == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    volatile uint32_t *pmu_regs = (volatile uint32_t *)pmu;
    volatile uint32_t *cru_regs = (volatile uint32_t *)cru;

    printf("=== PMU Register State (BSP) ===\n");
    printf("BUS_IDLE_CON0  (0x40): 0x%08x\n", pmu_regs[0x40/4]);
    printf("BUS_IDLE_SFTCON0(0x50): 0x%08x\n", pmu_regs[0x50/4]);
    printf("BUS_IDLE_ACK   (0x60): 0x%08x\n", pmu_regs[0x60/4]);
    printf("BUS_IDLE_ST    (0x68): 0x%08x\n", pmu_regs[0x68/4]);
    printf("NOC_AUTO_CON0  (0x70): 0x%08x\n", pmu_regs[0x70/4]);
    printf("NOC_AUTO_CON1  (0x74): 0x%08x\n", pmu_regs[0x74/4]);
    printf("PWR_DWN_ST     (0x98): 0x%08x\n", pmu_regs[0x98/4]);
    printf("PWR_GATE_SFTCON(0xa0): 0x%08x\n", pmu_regs[0xa0/4]);
    printf("VOL_GATE_SFTCON(0xa8): 0x%08x\n", pmu_regs[0xa8/4]);

    uint32_t bus_idle_st = pmu_regs[0x68/4];
    uint32_t pwr_dwn_st = pmu_regs[0x98/4];
    uint32_t vol_gate = pmu_regs[0xa8/4];

    printf("\n=== NPU Status ===\n");
    printf("NPU voltage domain (VD_NPU): %s (bit1=%d)\n",
           (vol_gate & (1 << 1)) ? "ENABLED" : "DISABLED",
           (vol_gate >> 1) & 1);
    printf("NPU power domain: %s (bit1=%d)\n",
           (pwr_dwn_st & (1 << 1)) ? "OFF" : "ON",
           (pwr_dwn_st >> 1) & 1);
    printf("NPU bus: %s (bit2=%d)\n",
           (bus_idle_st & (1 << 2)) ? "IDLE" : "ACTIVE",
           (bus_idle_st >> 2) & 1);

    /* Check if NPU is fully accessible */
    if ((vol_gate & (1 << 1)) && !(pwr_dwn_st & (1 << 1)) && !(bus_idle_st & (1 << 2))) {
        printf("\n** NPU should be ACCESSIBLE **\n");
    } else {
        printf("\n** NPU NOT accessible - missing:\n");
        if (!(vol_gate & (1 << 1)))
            printf("   - VD_NPU voltage domain not enabled\n");
        if (pwr_dwn_st & (1 << 1))
            printf("   - Power domain is OFF\n");
        if (bus_idle_st & (1 << 2))
            printf("   - Bus is IDLE\n");
    }

    printf("\n=== CRU NPU Clocks ===\n");
    printf("CLKGATE_CON26 (0x368): 0x%08x\n", cru_regs[0x368/4]);
    printf("CLKGATE_CON32 (0x380): 0x%08x\n", cru_regs[0x380/4]);
    printf("SOFTRST_CON26 (0x468): 0x%08x\n", cru_regs[0x468/4]);

    munmap(pmu, PMU_SIZE);
    munmap(cru, CRU_SIZE);
    close(fd);
    return 0;
}
