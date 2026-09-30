#!/bin/sh
# NX679J slot-B minimal gadget probe, v5.
#
# WHY v5 EXISTS (measured on the phone):
#   v4 = the same stock 5.10.66 kernel, same slot, same vendor_boot_b, but a
#   12 MB ramdisk carrying the OpenWrt rootfs + all 327 vendor modules + this
#   init. On hardware it produced NOTHING: no USB enumeration in 140 s, and
#   after recovery the breadcrumb journal on the rawdump partition was still
#   all zeros - i.e. this init never even reached the ufs_qcom insmod. The
#   known-good Magisk image on the same slot enumerated Android in 26 s. The
#   only variable was the ramdisk (12 MB / OpenWrt+327 modules vs 2.2 MB /
#   Android).
#   v5 discriminates between hypotheses:
#     (a) ramdisk size/content bulk is the problem, or
#     (b) the kernel never reaches /init at all.
#   The ramdisk is therefore shrunk to the minimum that can still prove "the
#   kernel reached userspace and could bring up a gadget": busybox + musl +
#   kmodloader, the 40 modules the USB/UFS chain needs, static device nodes,
#   and this script. No OpenWrt tree, no /sbin/init, no procd.
#
# v5 vs v4 (candidate-init-v4.sh): the journaling helpers (log, find_rawdump,
# flush), the 40-name module loop and the UFS check are carried over unchanged;
# only the ending changes:
#   one thing moves: the gadget bring-up becomes gadget_try() and is
#   re-attempted forever, once every 15 s, with a breadcrumb flush per cycle.
#   + gadget_try(secs) waits for a real UDC (ignores dummy_udc), creates the
#     configfs NCM gadget, binds it, sets usb0 to 10.0.0.1/24
#   + every cycle re-scans for a UDC, re-asserts the gadget and the usb0
#     address, and writes one more record to the rawdump journal
#   + mount return codes are now logged (v4 logged nothing about them)
#   + usb_role/role-switch paths are poked, guarded: a phone may hand the
#     controller to the gadget stack only in peripheral role (UNPROVEN - see
#     the "not proven" list in VERIFY-v5.md)
#   - the exec /sbin/init handover is GONE by design: this ramdisk has no
#     userspace to hand over to; the boot path under test ends with the gadget
#   - telnetd is GONE (no /dev/ptmx, no devpts mount, not needed for the
#     success signal)
#
# Channels, in order of reliability:
#   1. ramoops pmsg (/dev/pmsg0)  -> readable as /sys/fs/pstore on next boot
#   2. kmsg / console             -> visible only if a console is attached
#   3. rawdump partition          -> survives reset, readable from Android
#      (rawdump = the 256 MiB partition, found by size = 524288 sectors, all
#       zero on 2026-09-16; nothing in the stock Android setup touches it
#       during a normal boot)
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

# Write the whole journal to rawdump, flushed to media. One record occupies
# 4 KiB; a torn write still leaves the previous record readable.
flush() {
    [ -n "$RD" ] || find_rawdump || return 0
    dd if="$J" of="$RD" bs=4096 count=1 conv=notrunc 2>/dev/null
    $BB sync 2>/dev/null
}

# ---------------------------------------------------------------- stage 0
$BB mkdir -p /tmp /dev /proc /sys /sys/kernel/config /sys/fs/pstore
$BB mount -t tmpfs tmpfs /tmp 2>/dev/null
log "mount tmpfs /tmp rc=$?"
log "init v5 entered"
$BB mount -t proc proc /proc 2>/dev/null
log "mount proc /proc rc=$?"
$BB mount -t sysfs sysfs /sys 2>/dev/null
log "mount sysfs /sys rc=$?"
$BB mount -t devtmpfs devtmpfs /dev 2>/dev/null
log "mount devtmpfs /dev rc=$?"
$BB mount -t configfs configfs /sys/kernel/config 2>/dev/null
log "mount configfs /sys/kernel/config rc=$?"
$BB mount -t pstore pstore /sys/fs/pstore 2>/dev/null
log "mount pstore /sys/fs/pstore rc=$?"
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
    hit=""
    for cand in "$M/$ko.ko" "$M/${ko//_/-}.ko" "$M/${ko//-/_}.ko"; do
        if [ -f "$cand" ]; then
            f="$cand"
            hit="$(basename "$cand")"
            break
        fi
    done
    if [ -z "$f" ]; then
        # Files on this device whose name is neither all-hyphen nor all-underscore.
        case "$ko" in
            nvmem_qcom_spmi_sdam) cand="$M/nvmem_qcom-spmi-sdam.ko" ;;
            *) cand="" ;;
        esac
        if [ -n "$cand" ] && [ -f "$cand" ]; then
            f="$cand"
            hit="$(basename "$cand") (mixed spelling)"
        fi
    fi
    if [ -z "$f" ]; then
        log "insmod $ko RESOLUTION-FAILED tried $ko.ko ${ko//_/-}.ko ${ko//-/_}.ko"
        continue
    fi
    out=$($INSMOD "$f" 2>&1)
    log "insmod $ko rc=$? resolved=$hit ${out:+msg=$out}"
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
G=/sys/kernel/config/usb_gadget/g1
UDC=""
CYCLE=0

# Wait up to $1 seconds for a real UDC. dummy_udc* is the always-present dummy
# controller: binding to it proves nothing and would hide the real one.
find_udc() {
    UDC=""
    i=0
    while [ "$i" -lt "$1" ]; do
        for c in /sys/class/udc/*; do
            [ -e "$c" ] || continue
            n=$($BB basename "$c")
            case "$n" in dummy_udc*) continue ;; esac
            UDC="$n"
            break
        done
        [ -n "$UDC" ] && return 0
        i=$((i + 1))
        $BB sleep 1
    done
    return 1
}

# UNPROVEN (see VERIFY-v5.md): the dwc3 controller may still be in host/none
# role when this runs, in which case the gadget bind fails with -ENODEV and no
# host sees anything. Poke every role-switch that exists, guarded and logged.
poke_role() {
    n=0
    for r in /sys/class/usb_role/*/role; do
        [ -e "$r" ] || continue
        n=$((n + 1))
        printf 'device' > "$r" 2>/dev/null
        log "role $r -> $(cat "$r" 2>/dev/null)"
    done
    log "usb_role nodes found=$n"
}

# $1 = seconds to wait for a UDC before giving up on this attempt.
gadget_try() {
    find_udc "$1" || log "udc=NONE after ${1}s cycle=$CYCLE"
    [ -n "$UDC" ] || return 1
    $BB mkdir -p "$G/strings/0x409" "$G/configs/c.1/strings/0x409" \
        "$G/functions/ncm.usb0" 2>/dev/null
    printf 0x18d1 > "$G/idVendor" 2>/dev/null
    printf 0x4ee7 > "$G/idProduct" 2>/dev/null
    printf 'OpenWrt' > "$G/strings/0x409/manufacturer" 2>/dev/null
    printf 'NX679J' > "$G/strings/0x409/product" 2>/dev/null
    printf 'nx679j-v5' > "$G/strings/0x409/serialnumber" 2>/dev/null
    printf 'NCM' > "$G/configs/c.1/strings/0x409/configuration" 2>/dev/null
    printf 250 > "$G/configs/c.1/MaxPower" 2>/dev/null
    $BB ln -sf "$G/functions/ncm.usb0" "$G/configs/c.1/" 2>/dev/null
    bound=$(cat "$G/UDC" 2>/dev/null)
    if [ "$bound" != "$UDC" ]; then
        printf '%s' "$UDC" > "$G/UDC" 2>/dev/null
        rc=$?
        log "gadget bind udc=$UDC rc=$rc state=$(cat /sys/class/udc/$UDC/state 2>/dev/null)"
    else
        log "gadget already bound udc=$UDC state=$(cat /sys/class/udc/$UDC/state 2>/dev/null)"
    fi
    $BB sleep 3
    $BB ip link set usb0 up 2>/dev/null
    rc1=$?
    $BB ip addr add 10.0.0.1/24 dev usb0 2>/dev/null
    rc2=$?
    log "usb0 up rc=$rc1 addr rc=$rc2 $(ip -o addr show usb0 2>/dev/null)"
    return 0
}

poke_role
log "gadget attempt 1 (wait up to 30s for a real UDC)"
gadget_try 30
flush

# ---------------------------------------------------------------- hold
# This ramdisk has no userspace to hand over to, so PID 1 stays here forever:
# once every 15 s re-scan for a UDC, re-assert the gadget and the usb0 address,
# and append a breadcrumb record to rawdump, so even a partial run is visible
# from Android after a reset.
while true; do
    CYCLE=$((CYCLE + 1))
    log "cycle $CYCLE uptime=$(cat /proc/uptime 2>/dev/null) modules=$(cat /proc/modules 2>/dev/null | wc -l)"
    poke_role
    gadget_try 3
    flush
    $BB sleep 15
done
