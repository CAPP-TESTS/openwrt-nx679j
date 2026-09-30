/* Entra in fastboot. E' cio' che fa "adb reboot bootloader": chiamata diretta
 * a __reboot(RESTART2, "bootloader"), stringa che il bootloader legge dal
 * command line. Serve un binario nostro perche' il reboot di busybox (OpenWrt)
 * non accetta il parametro. */
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/reboot.h>
#include <stdio.h>
int main(void) {
    printf("richiesta reboot in bootloader (fastboot)...\n");
    fflush(stdout);
    syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
            LINUX_REBOOT_CMD_RESTART2, "bootloader");
    perror("reboot fallito");
    return 1;
}
