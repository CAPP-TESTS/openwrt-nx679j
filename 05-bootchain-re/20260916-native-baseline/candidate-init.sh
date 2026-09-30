#!/bin/sh
# NX679J slot-B diagnostic init: stock 5.10 kernel + OpenWrt userspace.
#
# Purpose, in order of priority:
#   1. leave evidence that survives a reset (pmsg0 -> pstore on next boot);
#   2. bring up a USB control channel (NCM) so the host can reach the device;
#   3. only then hand over to the OpenWrt init.
#
# Every step is logged before it is attempted, so a truncated log localises
# the failure. Nothing here is allowed to abort the boot.

PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
BB=/bin/busybox
MARK="nx679j-diag"

log() {
    msg="[$MARK] $*"
    # Three independent early channels. /dev/console is visible on the panel
    # if the framebuffer/console path is alive; kmsg and pmsg survive farther
    # into the boot and reset respectively.
    echo "$msg" > /dev/console 2>/dev/null || true
    echo "$msg" > /dev/kmsg 2>/dev/null || true
    echo "$msg" > /dev/pmsg0 2>/dev/null || true
    echo "$msg" >> /tmp/$MARK.log 2>/dev/null || true
}

log "init entered"
$BB mount -t proc proc /proc 2>/dev/null || log "mount proc failed"
$BB mount -t sysfs sysfs /sys 2>/dev/null || log "mount sysfs failed"
$BB mount -t devtmpfs devtmpfs /dev 2>/dev/null || log "devtmpfs unavailable"
$BB mkdir -p /tmp /dev/pts /sys/kernel/config /sys/fs/pstore
$BB mount -t tmpfs tmpfs /tmp 2>/dev/null || log "mount tmpfs failed"
$BB mount -t configfs configfs /sys/kernel/config 2>/dev/null || log "mount configfs failed"
$BB mount -t pstore pstore /sys/fs/pstore 2>/dev/null || log "mount pstore failed"

log "stage1 reached kernel=$($BB uname -r)"
log "cmdline=$($BB cat /proc/cmdline 2>/dev/null)"

# --- Qualcomm module chain -------------------------------------------------
# Order matters: glink/pmic before the PHYs, PHYs before dwc3-msm.
# Taken from the stock vendor ramdisk; the kernel is the stock 5.10.66.
# Modules live under the kernel-release directory that kmodloader expects.
M=/lib/modules/$($BB uname -r)
[ -d "$M" ] || M=/lib/modules
# FACT (QEMU run 2): this busybox has no insmod applet (every call returned
# rc=127). OpenWrt ships kmod as /sbin/insmod, so use that.
if [ -x /sbin/insmod ]; then
    INSMOD=/sbin/insmod
else
    INSMOD="$BB insmod"
fi
log "module dir=$M insmod=$INSMOD"
# Load the dependency-resolved order from the offline symbol analysis.
for ko in \
    smem.ko minidump.ko qcom_ipc_logging.ko qcom_glink.ko qcom_glink_smem.ko \
    qcom_smd.ko rproc_qcom_common.ko qmi_helpers.ko pdr_interface.ko \
    pmic_glink.ko ucsi_glink.ko debug-regulator.ko proxy-consumer.ko \
    gdsc-regulator.ko clk-qcom.ko ssusb-redriver-nb7vpq904m.ko \
    dwc3-msm.ko qcom-scm.ko phy-msm-snps-hs.ko phy-msm-ssusb-qmp.ko \
    repeater.ko phy-msm-snps-eusb2.ko altmode-glink.ko \
    qti-regmap-debugfs.ko fsa4480-i2c.ko
do
    if [ -f "$M/$ko" ]; then
        out=$($INSMOD "$M/$ko" 2>&1)
        log "insmod $ko rc=$? ${out:+msg=$out}"
    else
        log "insmod $ko SKIPPED (absent)"
    fi
done

# --- wait for a real UDC (dummy_udc does not count) ------------------------
UDC=""
i=0
while [ $i -lt 30 ]; do
    for c in /sys/class/udc/*; do
        [ -e "$c" ] || continue
        n=$($BB basename "$c")
        case "$n" in dummy_udc*) continue ;; esac
        UDC="$n"
        break
    done
    [ -n "$UDC" ] && break
    i=$((i + 1))
    $BB sleep 1
done
log "udc=${UDC:-NONE} after ${i}s"

# --- NCM gadget -----------------------------------------------------------
if [ -n "$UDC" ]; then
    G=/sys/kernel/config/usb_gadget/g1
    $BB mkdir -p "$G/strings/0x409" "$G/configs/c.1/strings/0x409" \
                 "$G/functions/ncm.usb0"
    echo 0x18d1 > "$G/idVendor"
    echo 0x4ee7 > "$G/idProduct"
    echo "OpenWrt"  > "$G/strings/0x409/manufacturer"
    echo "NX679J"   > "$G/strings/0x409/product"
    echo "nx679j-1" > "$G/strings/0x409/serialnumber"
    echo "NCM"      > "$G/configs/c.1/strings/0x409/configuration"
    echo 250        > "$G/configs/c.1/MaxPower"
    $BB ln -s "$G/functions/ncm.usb0" "$G/configs/c.1/" 2>/dev/null
    echo "$UDC" > "$G/UDC" 2>/dev/null
    log "gadget bind rc=$? udc_state=$($BB cat /sys/class/udc/$UDC/state 2>/dev/null)"
    $BB sleep 2
    $BB ip link set usb0 up 2>/dev/null
    $BB ip addr add 10.0.0.1/24 dev usb0 2>/dev/null
    log "usb0: $($BB ip -o addr show usb0 2>/dev/null)"
else
    log "no usable UDC; USB channel unavailable"
fi

# --- hand over ------------------------------------------------------------
if [ -x /sbin/init ]; then
    log "exec /sbin/init"
    export INITRAMFS=1
    exec /sbin/init
fi

log "no /sbin/init; holding for inspection"
while true; do $BB sleep 60; done
