/*
 * Kexec Injector - Bypass Gunyah hypervisor watchdog via kexec hook
 * Hooks into kexec_load to disable watchdog before jumping to new kernel
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/kallsyms.h>
#include <linux/io.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Research");
MODULE_DESCRIPTION("Gunyah Hypervisor Bypass via Kexec");

/* Gunyah MMIO base addresses (SM8350 specific) */
#define GH_MMIO_BASE 0x17C00000
#define GH_WDT_CTRL_OFFSET 0x0
#define GH_WDT_BITE_TIME_OFFSET 0x14

/* Physical memory access */
static void __iomem *gh_wdt_base = NULL;

/* Try to disable watchdog via MMIO */
static void disable_gunyah_watchdog(void)
{
    u32 val;
    
    if (!gh_wdt_base) {
        gh_wdt_base = ioremap(GH_MMIO_BASE, 0x1000);
        if (!gh_wdt_base) {
            pr_err("[kexec_inject] Failed to map Gunyah watchdog MMIO\n");
            return;
        }
    }
    
    pr_info("[kexec_inject] Attempting to disable Gunyah watchdog at 0x%llx\n", 
            (u64)GH_MMIO_BASE);
    
    /* Try to disable watchdog control register */
    val = readl(gh_wdt_base + GH_WDT_CTRL_OFFSET);
    pr_info("[kexec_inject] Current WDT_CTRL: 0x%x\n", val);
    
    /* Set bite time to maximum (effectively disabling it) */
    writel(0xFFFFFFFF, gh_wdt_base + GH_WDT_BITE_TIME_OFFSET);
    
    /* Try to disable enable bit (bit 0) */
    writel(val & ~0x1, gh_wdt_base + GH_WDT_CTRL_OFFSET);
    
    val = readl(gh_wdt_base + GH_WDT_CTRL_OFFSET);
    pr_info("[kexec_inject] New WDT_CTRL: 0x%x\n", val);
    
    /* Also try to pet the watchdog continuously */
    writel(0x1, gh_wdt_base + 0x8); /* Pet register usually at offset 0x8 */
}

/* Kprobe for do_kexec_load or machine_kexec */
static struct kprobe kp_machine_kexec = {
    .symbol_name = "machine_kexec",
};

static int handler_pre_machine_kexec(struct kprobe *p, struct pt_regs *regs)
{
    pr_info("[kexec_inject] machine_kexec called - disabling Gunyah watchdog\n");
    disable_gunyah_watchdog();
    msleep(100); /* Give it time */
    return 0;
}

static int __init kexec_injector_init(void)
{
    int ret;
    
    pr_info("[kexec_inject] Loading Gunyah bypass module\n");
    
    /* Try to disable watchdog immediately */
    disable_gunyah_watchdog();
    
    /* Register kprobe to hook kexec */
    kp_machine_kexec.pre_handler = handler_pre_machine_kexec;
    ret = register_kprobe(&kp_machine_kexec);
    if (ret < 0) {
        pr_err("[kexec_inject] Failed to register kprobe on machine_kexec: %d\n", ret);
        /* Try alternative symbol */
        kp_machine_kexec.symbol_name = "__machine_kexec";
        ret = register_kprobe(&kp_machine_kexec);
        if (ret < 0) {
            pr_err("[kexec_inject] Failed alternative kprobe: %d\n", ret);
            return ret;
        }
    }
    
    pr_info("[kexec_inject] Successfully hooked kexec, watchdog bypass active\n");
    return 0;
}

static void __exit kexec_injector_exit(void)
{
    unregister_kprobe(&kp_machine_kexec);
    if (gh_wdt_base)
        iounmap(gh_wdt_base);
    pr_info("[kexec_inject] Module unloaded\n");
}

module_init(kexec_injector_init);
module_exit(kexec_injector_exit);
