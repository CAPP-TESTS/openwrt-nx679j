
THE MECHANISM (why one bad probe is enough)

finit_module(2) does not just map code. In kernel/module.c it runs
load_module() and then do_init_module(), and do_init_module() calls
do_one_initcall(mod->init) before the syscall returns. For a GKI module whose
init is module_platform_driver(...), mod->init is
platform_driver_register(), which inside the same call chain reaches
driver_register -> bus_add_driver -> driver_attach -> __driver_attach ->
driver_probe_device. driver_probe_device() only schedules an asynchronous
probe if the driver asked for it (PROBE_PREFER_ASYNCHRONOUS, or the
driver_async_probe= kernel parameter); otherwise it calls really_probe() and
the driver's probe() function directly, in the caller's context.

The caller's context here is PID 1, inside the finit_module(2) syscall. The
driver probe therefore runs with PID 1 as its thread, and any wait inside it
that never completes is a permanent, silent freeze of PID 1: no panic (so with
CONFIG_PANIC_TIMEOUT=-1 there is no restart), no journal, no USB, nothing on
the screen except the still logo. That matches every measurement on hardware.

WHY QEMU NEVER SHOWS IT: under qemu-system-aarch64 -machine virt the device
tree has none of this platform's compatibles (qcom,ufshc, qcom,dwc-usb3-msm,
qcom,glink-smem, ...), so the platform driver never matches a device and
probe() is never called at all. finit_module returns rc=0 in microseconds.
That is exactly what our own QEMU run of v6 showed: 294 of 304 modules
"loaded" with rc=0. On hardware the same call runs the matching probe.
