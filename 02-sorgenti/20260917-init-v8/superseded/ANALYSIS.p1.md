
PART 1 - THE STRUCTURAL FACT (verified locally from the running system's own bytes)

The /init that works on this device is not Android's init: it is magiskinit, a
first stage that mounts partitions and hands over. Extracted from the Magisk
android ramdisk we already have locally
(probe-v7-sonda/inputs/magisk-android-ramdisk.cpio, member "init", sha256
8e26e33c3db284823f5d083588df26781d032ecd9818b8a116490d2f488f75dd,
statically linked, stripped) the string counts are:

    finit_module 0        init_module 0        modules.load 0
    modules.dep 0        /lib/modules 0        insmod 0     modprobe 0

and the strings it does contain are the ones already quoted from the device:
/system_root, Mounting system_root, Cannot mount root partition, abort,
/first_stage_ramdisk, /first_stage_ramdisk/sdcard.

Our init v6 contains and uses exactly the opposite set: /lib/modules/
modules.load, modules.load.recovery, modules.softdep, modules.blocklist and
finit_module/init_module. v5 uses busybox insmod (which is finit_module).

So the working boot never runs a module load inside PID 1. That is the
structural difference, and it is the one that matters, because of how
finit_module(2) works on a GKI 5.10 kernel:
