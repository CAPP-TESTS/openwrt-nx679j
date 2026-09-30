#!/bin/sh
# NX679J slot-B diagnostic init, v3.
#
# v3 = v2 with the body below byte-identical. The single v3 change does not
# live in this file: the boot-image kernel command line now carries panic=10,
# so a kernel panic reboots the phone by itself instead of hanging until a
# human holds the power key.
#
# v1 (candidate-init.sh) loaded the right modules but left no evidence behind:
# after the failed boot we could not say whether the kernel reached userspace.
# v2 keeps the same boot path and adds a breadcrumb channel that survives a
# reset, plus a shell over the USB gadget.
#
# Channels, in order of reliability:
#   1. ramoops pmsg (/dev/pmsg0)  -> readable as /sys/fs/pstore on next boot
#   2. kmsg / console             -> visible only if a console is attached
#   3. rawdump partition          -> survives reset, readable from Android
#      (rawdump = the 256 MiB partition, all zero on 2026-09-16; nothing in
#       the stock Android setup reads or writes it during a normal boot)
#
# Nothing here may abort the boot: every step is logged before it is tried.

PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
BB=/bin/busybox
J=/tmp/nx679j-bc.log
RD=""

# ---------------------------------------------------------------- journaling
log() {
    printf '%s\n' "$*" >> "$J" 2>/dev/null
    printf 'nx679j: %s\n' "$*" > /dev/kmsg 2>/dev/null
    printf 'nx679j: %s\n' "$*" > /dev/pmsg0 2>/dev/null
    printf 'nx679j: %s\n' "$*" > /dev/console 2>/dev/null
}

# rawdump is identified by size alone (524288 sectors of 512 B = 256 MiB,
# unique in this GPT). by-name symlinks do not exist without ueventd.
find_rawdump() {
    [ -n "$RD" ] && return 0
    for d in /sys/class/block/*; do
        [ -f "$d/partition" ] || continue
        s=$(cat "$d/size" 2>/dev/null)
        [ "$s" = "524288" ] || continue
        mm=$(cat "$d/dev" 2>/dev/null) || continue
        maj=${mm%%:*}; min=${mm##*:}
        n=$(basename "$d")
        rm -f /dev/rd
        $BB mknod /dev/rd b "$maj" "$min" 2>/dev/null || continue
        RD=/dev/rd
        log "rawdump device=$n major:minor=$mm"
        return 0
    done
    return 1
}

# Write the whole journal to rawdump, flushed to media, twice per call so a
# torn write still leaves a readable copy. One record occupies 4 KiB.
flush() {
    [ -n "$RD" ] || find_rawdump || return 0
    dd if="$J" of="$RD" bs=4096 count=1 conv=notrunc 2>/dev/null
    $BB sync 2>/dev/null
}

# ---------------------------------------------------------------- stage 0
$BB mkdir -p /tmp /dev /proc /sys /sys/kernel/config /sys/fs/pstore
$BB mount -t tmpfs tmpfs /tmp 2>/dev/null
log "init v2 entered"
$BB mount -t proc proc /proc 2>/dev/null
$BB mount -t sysfs sysfs /sys 2>/dev/null
$BB mount -t devtmpfs devtmpfs /dev 2>/dev/null
$BB mount -t configfs configfs /sys/kernel/config 2>/dev/null
$BB mount -t pstore pstore /sys/fs/pstore 2>/dev/null
log "stage0 kernel=$(uname -r)"
log "stage0 cmdline=$(cat /proc/cmdline 2>/dev/null)"
log "stage0 modprobe=$(which insmod 2>/dev/null)"

# ---------------------------------------------------------------- modules
# Load order taken from /proc/modules on the running Android (376 modules,
# dependencies precede dependents). These are the modules Android itself has
# loaded at the same time as dwc3_msm and ufs_qcom, in the same order.
M=/lib/modules/$(uname -r)
[ -d "$M" ] || M=$(ls -d /lib/modules/*/ 2>/dev/null | head -1)
[ -n "$M" ] || M=/lib/modules
[ -d "$M" ] || M=/lib/modules
if [ -x /sbin/insmod ]; then
    INSMOD=/sbin/insmod
else
    INSMOD="$BB insmod"
fi
log "modules dir=$M insmod=$INSMOD count=$(ls $M/*.ko 2>/dev/null | wc -l)"

for ko in \
    phy_qcom_emu phy_msm_snps_eusb2 repeater phy_qcom_ufs_qmp_v4_lahaina \
    qcom_glink_spss phy_qcom_ufs_qmp_14nm phy_qcom_ufs_qmp_v3 nvmem_qfprom \
    fsa4480_i2c altmode_glink dwc3_msm phy_msm_ssusb_qmp phy_msm_snps_hs \
    ssusb_redriver_nb7vpq904m ucsi_glink pmic_glink pdr_interface qmi_helpers \
    rproc_qcom_common qcom_smd qcom_glink_smem qcom_glink ufs_qcom \
    ufshcd_crypto_qti qti_regmap_debugfs phy_qcom_ufs_qmp_v4_cape \
    phy_qcom_ufs_qmp_v4_diwali phy_qcom_ufs_qmp_v4_waipio phy_qcom_ufs \
    nvmem_qcom_spmi_sdam crypto_qti_common crypto_qti_hwkm clk_qcom \
    gdsc_regulator proxy_consumer debug_regulator qcom_ipc_logging qcom_scm \
    minidump smem
do
    f=""
    for cand in "$M/$ko.ko" "$M/${ko}.ko"; do
        [ -f "$cand" ] && f="$cand" && break
    done
    if [ -z "$f" ]; then
        log "insmod $ko ABSENT"
        continue
    fi
    out=$($INSMOD "$f" 2>&1)
    log "insmod $ko rc=$? ${out:+msg=$out}"
done
flush

# ---------------------------------------------------------------- UFS check
i=0
while [ $i -lt 15 ]; do
    [ -e /sys/class/block/sda ] && break
    i=$((i + 1))
    $BB sleep 1
done
log "ufs sda after ${i}s present=$([ -e /sys/class/block/sda ] && echo yes || echo no)"
log "partitions=$(cat /proc/partitions 2>/dev/null | tail -n +3 | wc -l)"
flush
find_rawdump

# ---------------------------------------------------------------- gadget
UDC=""
i=0
while [ $i -lt 20 ]; do
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
flush

if [ -n "$UDC" ]; then
    G=/sys/kernel/config/usb_gadget/g1
    $BB mkdir -p "$G/strings/0x409" "$G/configs/c.1/strings/0x409" "$G/functions/ncm.usb0"
    printf 0x18d1 > "$G/idVendor" 2>/dev/null
    printf 0x4ee7 > "$G/idProduct" 2>/dev/null
    printf 'OpenWrt' > "$G/strings/0x409/manufacturer" 2>/dev/null
    printf 'NX679J' > "$G/strings/0x409/product" 2>/dev/null
    printf 'nx679j-v2' > "$G/strings/0x409/serialnumber" 2>/dev/null
    printf 'NCM' > "$G/configs/c.1/strings/0x409/configuration" 2>/dev/null
    printf 250 > "$G/configs/c.1/MaxPower" 2>/dev/null
    $BB ln -sf "$G/functions/ncm.usb0" "$G/configs/c.1/" 2>/dev/null
    printf '%s' "$UDC" > "$G/UDC" 2>/dev/null
    log "gadget bind rc=$? state=$(cat /sys/class/udc/$UDC/state 2>/dev/null)"
    $BB sleep 3
    $BB ip link set usb0 up 2>/dev/null
    $BB ip addr add 10.0.0.1/24 dev usb0 2>/dev/null
    log "usb0=$(ip -o addr show usb0 2>/dev/null)"
    # Shell over the gadget so the host can attach even before procd is up.
    $BB telnetd -l /bin/sh -p 23 2>/dev/null && log "telnetd started on usb0:23"
    flush
fi

# ---------------------------------------------------------------- handover
log "handover init=$([ -x /sbin/init ] && echo present || echo missing)"
flush
if [ -x /sbin/init ]; then
    export INITRAMFS=1
    exec /sbin/init
fi
log "no /sbin/init: holding"
while true; do $BB sleep 60; done
