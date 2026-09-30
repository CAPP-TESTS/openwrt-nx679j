/*
 * Direct Hypervisor Memory Attack via /dev/mem
 * Attempts to disable Gunyah watchdog by writing to physical memory
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>

/* SM8350 Gunyah addresses (from device tree / reverse engineering) */
#define GH_WATCHDOG_BASE 0x17C00000ULL
#define GH_RM_BASE       0x86700000ULL  /* Gunyah Resource Manager */
#define QTEE_BASE        0x86800000ULL  /* QSEE/TrustZone */

/* Register offsets */
#define WDT_CTRL_REG    0x00
#define WDT_RESET_REG   0x04
#define WDT_BITE_TIME   0x14
#define WDT_BARK_TIME   0x10

void hexdump(const char *desc, void *addr, int len) {
    uint8_t *buf = (uint8_t*)addr;
    printf("%s:\n", desc);
    for (int i = 0; i < len; i += 16) {
        printf("  %04x: ", i);
        for (int j = 0; j < 16 && i+j < len; j++) {
            printf("%02x ", buf[i+j]);
        }
        printf("\n");
    }
}

int try_disable_watchdog(int mem_fd, uint64_t base_addr) {
    void *map_base;
    volatile uint32_t *regs;
    uint32_t val;
    size_t page_size = sysconf(_SC_PAGE_SIZE);
    off_t page_base = (base_addr / page_size) * page_size;
    off_t page_offset = base_addr - page_base;
    
    printf("\n[*] Attempting to map 0x%llx (page_base=0x%llx, offset=0x%llx)\n",
           base_addr, page_base, page_offset);
    
    map_base = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_SHARED, 
                    mem_fd, page_base);
    
    if (map_base == MAP_FAILED) {
        perror("[-] mmap failed");
        return -1;
    }
    
    regs = (volatile uint32_t *)((uint8_t *)map_base + page_offset);
    
    printf("[+] Successfully mapped memory\n");
    
    /* Read current state */
    printf("[*] Reading watchdog registers...\n");
    val = regs[WDT_CTRL_REG / 4];
    printf("    WDT_CTRL:      0x%08x\n", val);
    val = regs[WDT_BARK_TIME / 4];
    printf("    WDT_BARK_TIME: 0x%08x\n", val);
    val = regs[WDT_BITE_TIME / 4];
    printf("    WDT_BITE_TIME: 0x%08x\n", val);
    
    /* Dump first 64 bytes */
    hexdump("First 64 bytes", (void*)regs, 64);
    
    /* Try to disable */
    printf("\n[*] Attempting to disable watchdog...\n");
    
    /* Set bite time to maximum */
    regs[WDT_BITE_TIME / 4] = 0xFFFFFFFF;
    printf("    Set BITE_TIME to 0xFFFFFFFF\n");
    
    /* Set bark time to maximum */
    regs[WDT_BARK_TIME / 4] = 0xFFFFFFFF;
    printf("    Set BARK_TIME to 0xFFFFFFFF\n");
    
    /* Pet the watchdog */
    regs[WDT_RESET_REG / 4] = 0x1;
    printf("    Pet watchdog\n");
    
    /* Try to disable enable bit */
    val = regs[WDT_CTRL_REG / 4];
    regs[WDT_CTRL_REG / 4] = val & ~0x1;
    printf("    Disabled enable bit (wrote 0x%08x)\n", val & ~0x1);
    
    /* Read back */
    printf("\n[*] Reading back registers...\n");
    val = regs[WDT_CTRL_REG / 4];
    printf("    WDT_CTRL:      0x%08x\n", val);
    val = regs[WDT_BARK_TIME / 4];
    printf("    WDT_BARK_TIME: 0x%08x\n", val);
    val = regs[WDT_BITE_TIME / 4];
    printf("    WDT_BITE_TIME: 0x%08x\n", val);
    
    munmap(map_base, page_size);
    return 0;
}

int main(int argc, char **argv) {
    int mem_fd;
    
    printf("=== Gunyah Hypervisor Watchdog Attack ===\n");
    printf("Target: SM8350 (Snapdragon 888)\n\n");
    
    /* Open /dev/mem */
    mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        perror("[-] Failed to open /dev/mem (need root)");
        return 1;
    }
    
    printf("[+] Opened /dev/mem\n");
    
    /* Try different possible watchdog addresses */
    uint64_t candidate_addrs[] = {
        GH_WATCHDOG_BASE,
        0x17C10000,  /* Alternative offset */
        0x17C20000,  /* Alternative offset */
        0x86100000,  /* Seen in some logs */
    };
    
    for (int i = 0; i < sizeof(candidate_addrs)/sizeof(candidate_addrs[0]); i++) {
        printf("\n=== Trying address 0x%llx ===\n", candidate_addrs[i]);
        if (try_disable_watchdog(mem_fd, candidate_addrs[i]) == 0) {
            printf("[+] Successfully accessed memory at 0x%llx\n", candidate_addrs[i]);
        }
    }
    
    /* Also try to find Gunyah Resource Manager */
    printf("\n=== Scanning for Gunyah RM ===\n");
    try_disable_watchdog(mem_fd, GH_RM_BASE);
    
    close(mem_fd);
    
    printf("\n[*] Attack complete. Check dmesg for kernel messages.\n");
    printf("[*] Now try booting your custom kernel!\n");
    
    return 0;
}
