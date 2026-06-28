#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>

#define PMU_BASE 0xfdd90000
#define PAGE_SIZE 4096

int main() {
    int fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) { perror("open"); return 1; }
    
    volatile uint32_t *pmu = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, fd, PMU_BASE);
    if (pmu == MAP_FAILED) { perror("mmap"); return 1; }
    
    printf("PMU Register State:\n");
    printf("  BUS_IDLE_CON0  (0x40): 0x%08x\n", pmu[0x40/4]);
    printf("  BUS_IDLE_SFTCON0(0x50): 0x%08x\n", pmu[0x50/4]);
    printf("  BUS_IDLE_ACK   (0x60): 0x%08x\n", pmu[0x60/4]);
    printf("  BUS_IDLE_ST    (0x68): 0x%08x\n", pmu[0x68/4]);
    printf("  NOC_AUTO_CON0  (0x70): 0x%08x\n", pmu[0x70/4]);
    printf("  NOC_AUTO_CON1  (0x74): 0x%08x\n", pmu[0x74/4]);
    printf("  PWR_DWN_ST     (0x98): 0x%08x\n", pmu[0x98/4]);
    printf("  PWR_GATE_SFTCON(0xa0): 0x%08x\n", pmu[0xa0/4]);
    printf("\nNPU bits (bit 2 for idle, bit 1 for power):\n");
    printf("  NPU auto-idle: %s (NOC_AUTO bit2=%d)\n", 
           (pmu[0x70/4] & 0x4) ? "ENABLED" : "DISABLED", (pmu[0x70/4] >> 2) & 1);
    printf("  NPU idle req:  %s (SFTCON0 bit2=%d)\n",
           (pmu[0x50/4] & 0x4) ? "REQUESTED" : "NOT REQUESTED", (pmu[0x50/4] >> 2) & 1);
    printf("  NPU bus state: %s (IDLE_ST bit2=%d)\n",
           (pmu[0x68/4] & 0x4) ? "IDLE" : "ACTIVE", (pmu[0x68/4] >> 2) & 1);
    printf("  NPU power:     %s (PWR_DWN bit1=%d)\n",
           (pmu[0x98/4] & 0x2) ? "OFF" : "ON", (pmu[0x98/4] >> 1) & 1);
    
    munmap((void*)pmu, PAGE_SIZE);
    close(fd);
    return 0;
}
