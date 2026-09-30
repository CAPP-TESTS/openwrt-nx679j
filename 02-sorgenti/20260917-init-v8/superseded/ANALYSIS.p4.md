
PART 3 - EVERY BLOCKING POINT IN v5 (candidate-init-v5.sh, line numbers exact)

v5 is a shell script, so PID 1 is busybox sh. It blocks in a second way: the
shell waits (wait4) for every command it starts, so a child that is stuck in
the kernel is a stuck PID 1 too.

1. line 161  out=$($INSMOD "$f" 2>&1)  inside the loop of line 118-123
   busybox insmod = finit_module(2). Same mechanism as v6 item 2, same class
   of driver probe, and it is the *first* thing that can freeze this init.
2. line 87   dd if="$J" of="$RD" bs=4096 count=1 conv=notrunc
   line 88   $BB sync
   Block device I/O to rawdump: same failure class as v6 item 4.
3. line 57-60  log(): printf >> $J, then printf > /dev/kmsg, > /dev/pmsg0,
   > /dev/console. Three device opens/writes per line, on the console path.
   /dev/pmsg0 is a node shipped in our ramdisk with a fixed major:minor; if
   ramoops is not registered the open fails silently and nothing survives.
4. line 93-105  six mounts: tmpfs, proc, sysfs, devtmpfs, configfs, pstore.
   devtmpfs fails instantly (the kernel has no CONFIG_DEVTMPFS) - which is why
   the whole design needed the mknod path at line 69-76.
5. line 69-76  find_rawdump: cat $d/size, cat $d/dev, $BB mknod.
6. work done by a child the shell waits for, each one a possible freeze:
   line 107 cat /proc/cmdline, line 108 which insmod, line 174 cat
   /proc/partitions, line 191 $BB basename in a loop over /sys/class/udc,
   line 231/235 cat $G/UDC and cat state, line 244 ip -o addr show usb0,
   line 260 cat /proc/uptime and /proc/modules.
7. line 211-244  printf into sysfs/configfs attributes, including line 233
   printf '%s' "$UDC" > $G/UDC. A sysfs write runs the provider's callback in
   the caller's context; for the UDC attribute that is the gadget bind
   (endpoint enable, PHY, role switch).
8. bounded, listed for completeness: line 171 sleep 1, line 198 sleep 1,
   line 239 sleep 3, line 264 sleep 15.
9. journal: same problem as v6. The rawdump flush needs UFS (line 87), and
   before that everything is only in /dev/kmsg and a hardcoded /dev/pmsg0.
