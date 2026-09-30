/*
 * reboot2.c — riavvia passando una STRINGA al bootloader (come Android).
 *
 * Perche': busybox reboot e sysrq-trigger fanno un reboot cieco: l'ABL non sa
 * cosa vuoi e riparte in Linux.  Android usa la variante RESTART2 della syscall
 * reboot, che consegna la stringa ("bootloader", "recovery", ...) al bootloader:
 * e' cosi' che 'adb reboot bootloader' entra in fastboot.
 *
 *   reboot2 bootloader   -> fastboot
 *   reboot2 recovery     -> recovery
 *   reboot2 ""           -> riavvio normale
 *
 * Uso: syscall(SYS_reboot, MAGIC1, MAGIC2, CMD_RESTART2, "bootloader")
 *   MAGIC1 = 0xfee1dead, MAGIC2 = 672274793, CMD_RESTART2 = 0xa1b2c3d4
 */
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>

#define MAGIC1		0xfee1dead
#define MAGIC2		672274793
#define CMD_RESTART2	0xa1b2c3d4

int main(int argc, char **argv)
{
	const char *why = (argc > 1) ? argv[1] : "bootloader";
	long r;

	printf("reboot2: chiedo al bootloader '%s'\n", why);
	sync();

	r = syscall(SYS_reboot, MAGIC1, MAGIC2, CMD_RESTART2, why);
	/* se arriviamo qui la syscall ha fallito */
	printf("reboot2: errore (r=%ld)\n", r);
	return 1;
}
