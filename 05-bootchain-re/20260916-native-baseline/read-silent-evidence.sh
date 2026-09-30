#!/usr/bin/env bash
# read-silent-evidence.sh — legge le fonti di evidenza che NON dipendono dal nostro init.
# Uso: ./read-silent-evidence.sh <dir-di-output>
# Richiede: telefono in Android con adb root (su).
# Legge: logdump (log di XBL/ABL), rawdump (nostra journal), pstore, metadati A/B (se in fastboot).
set -u
ADB="adb -s 0123456789ABCDEF"
OUT="${1:-silent-evidence-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT"
log() { printf '%s\n' "$*" | tee -a "$OUT/report.txt"; }

log "=== read-silent-evidence $(date -Is) -> $OUT ==="

if ! $ADB get-state >/dev/null 2>&1; then
    log "!! nessun device adb: serve Android avviato (slot A) per leggere logdump/rawdump"
    exit 1
fi
log "device: $($ADB shell getprop ro.boot.slot_suffix 2>/dev/null | tr -d '\r')"

# 1. logdump: log del bootloader (XBL/ABL). Evidence su quanto lontano e' arrivata la catena PRIMA del kernel.
log ""
log "=== 1. logdump (log XBL/ABL) ==="
$ADB shell su -c 'ls -l /dev/block/by-name/logdump /dev/block/by-name/rawdump 2>&1' | tee -a "$OUT/report.txt"
$ADB shell su -c 'dd if=/dev/block/by-name/logdump of=/data/local/tmp/logdump.bin bs=4096 2>&1 | tail -1'
$ADB shell su -c 'chmod 644 /data/local/tmp/logdump.bin' 2>/dev/null
$ADB pull /data/local/tmp/logdump.bin "$OUT/logdump.bin" >/dev/null 2>&1 && \
    log "logdump: $(stat -c%s "$OUT/logdump.bin") B, sha256 $(sha256sum "$OUT/logdump.bin" | cut -c1-16)"
# estrai le stringhe leggibili: e' un log binario con record testuali
if [ -s "$OUT/logdump.bin" ]; then
    strings -n 6 "$OUT/logdump.bin" > "$OUT/logdump.strings.txt" 2>/dev/null
    log "stringhe estratte: $(wc -l < "$OUT/logdump.strings.txt")"
    log "--- righe rilevanti (boot/kernel/dtb/image/error) ---"
    grep -iE 'boot|kernel|dtb|dtbo|image|avb|verify|error|fail|panic|linux|slot|ramdisk' \
        "$OUT/logdump.strings.txt" | tail -60 | tee -a "$OUT/report.txt"
fi

# 2. rawdump: la journal scritta dal NOSTRO init (se l'init e' arrivato a montare la UFS)
log ""
log "=== 2. rawdump (nostra journal, se l'init e' arrivato alla UFS) ==="
$ADB shell su -c 'dd if=/dev/block/by-name/rawdump of=/data/local/tmp/rawdump.bin bs=4096 2>&1 | tail -1'
$ADB shell su -c 'chmod 644 /data/local/tmp/rawdump.bin' 2>/dev/null
$ADB pull /data/local/tmp/rawdump.bin "$OUT/rawdump.bin" >/dev/null 2>&1 && \
    log "rawdump: $(stat -c%s "$OUT/rawdump.bin") B, sha256 $(sha256sum "$OUT/rawdump.bin" | cut -c1-16)"
if [ -s "$OUT/rawdump.bin" ]; then
    NZ=$(head -c 65536 "$OUT/rawdump.bin" | tr -d '\0' | wc -c)
    log "byte non-zero nei primi 64 KiB: $NZ  $([ "$NZ" -eq 0 ] && echo '(journal ASSENTE: init mai arrivato alla UFS)' || echo '(journal PRESENTE)')"
    head -c 65536 "$OUT/rawdump.bin" | strings -n 4 > "$OUT/rawdump.journal.txt" 2>/dev/null
    [ -s "$OUT/rawdump.journal.txt" ] && head -80 "$OUT/rawdump.journal.txt" | tee -a "$OUT/report.txt"
fi

# 3. pstore
log ""
log "=== 3. pstore ==="
P=$($ADB shell su -c 'ls -la /sys/fs/pstore/ 2>&1' | tr -d '\r')
printf '%s\n' "$P" | tee -a "$OUT/report.txt"
$ADB shell su -c 'cat /sys/fs/pstore/* 2>/dev/null' > "$OUT/pstore.txt" 2>/dev/null
[ -s "$OUT/pstore.txt" ] && { log "pstore NON vuota:"; head -40 "$OUT/pstore.txt" | tee -a "$OUT/report.txt"; } || log "pstore vuota"

# 4. ulteriori tracce del kernel rimaste da un boot precedente
log ""
log "=== 4. dmesg di Android: cerca tracce del boot precedente (ramo non-Android) ==="
$ADB shell su -c 'dmesg' 2>/dev/null | grep -iE 'initramfs|No init|Failed to execute|unpack|ramdisk' | head -20 | tee -a "$OUT/report.txt"

log ""
log "=== fatto: $OUT ==="
