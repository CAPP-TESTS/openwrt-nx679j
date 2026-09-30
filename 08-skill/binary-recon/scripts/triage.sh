#!/usr/bin/env bash
# Triage di un artefatto binario: produce la scheda dei fatti descritta in SKILL.md.
#
# Esiste perche' le prime dieci domande su un binario sono sempre le stesse, e
# riscriverle a mano a ogni sessione fa perdere tempo e salta passaggi. Qui sono
# in un comando solo, con la fonte accanto a ogni dato.
#
# Uso:  triage.sh <file> [--json]
#
# Sola lettura: non esegue mai il target, non monta nulla, non richiede privilegi.

set -uo pipefail

TARGET="${1:-}"
JSON=0
[[ "${2:-}" == "--json" ]] && JSON=1

if [[ -z "$TARGET" || ! -f "$TARGET" ]]; then
    echo "uso: $(basename "$0") <file> [--json]" >&2
    exit 2
fi

have() { command -v "$1" >/dev/null 2>&1; }

# Mappa simbolo kernel -> sottosistema. E' la firma piu' informativa di un .ko:
# dice a quale parte del kernel il driver si aggancia senza leggere una riga di
# disassemblato.
classify_symbols() {
    local syms="$1" hits=""
    add() { grep -qE "^$2$|^$2\b" <<<"$syms" && hits+="$1
"; }
    add "USB"          "usb_register_driver|usb_register_device_driver|usb_submit_urb"
    add "PCI"          "__pci_register_driver|pci_register_driver|pcim_enable_device"
    add "platform/OF"  "__platform_driver_register|platform_driver_register|of_match_device"
    add "I2C"          "i2c_register_driver|i2c_add_driver|i2c_transfer"
    add "SPI"          "__spi_register_driver|spi_register_driver|spi_sync"
    add "regmap"       "__devm_regmap_init.*|regmap_read|regmap_write|regmap_update_bits"
    add "IIO (sensori)" "__devm_iio_device_register|iio_device_register|devm_iio_device_alloc"
    add "input"        "input_register_device|devm_input_allocate_device|input_event"
    add "hwmon"        "devm_hwmon_device_register_with_info|hwmon_device_register"
    add "netdev"       "register_netdev|alloc_etherdev_mqs|netif_rx|napi_schedule"
    add "V4L2/media"   "v4l2_device_register|video_register_device|vb2_queue_init"
    add "ALSA/audio"   "snd_card_new|snd_soc_register_card|snd_pcm_new"
    add "LED"          "devm_led_classdev_register|led_classdev_register"
    add "RTC"          "devm_rtc_device_register|devm_rtc_allocate_device"
    add "watchdog"     "devm_watchdog_register_device|watchdog_register_device"
    add "GPIO"         "devm_gpiod_get|gpiod_set_value|gpiochip_add_data"
    add "PWM"          "pwmchip_add|devm_pwmchip_add"
    add "MTD/flash"    "mtd_device_parse_register|nand_scan"
    add "block"        "blk_mq_alloc_disk|add_disk|blk_alloc_queue"
    add "DRM/grafica"  "drm_dev_alloc|drm_dev_register|devm_drm_dev_alloc"
    add "crypto"       "crypto_register_alg|crypto_register_skcipher"
    add "chardev"      "cdev_add|alloc_chrdev_region|misc_register"
    add "DMA"          "dma_alloc_coherent|dma_map_single|dmaengine_prep_slave_sg"
    add "IRQ"          "devm_request_irq|request_threaded_irq|__request_region"
    add "clock"        "devm_clk_get|clk_prepare_enable|devm_clk_get_enabled"
    add "regolatori"   "devm_regulator_get|regulator_enable"
    add "PM runtime"   "pm_runtime_enable|__pm_runtime_resume"
    add "firmware"     "request_firmware|firmware_request_nowarn"
    add "debugfs"      "debugfs_create_dir|debugfs_create_file"
    printf '%s' "$hits"
}

echo "=============================================================="
echo " TRIAGE: $TARGET"
echo "=============================================================="

echo
echo "## Artefatto"
printf '  %-14s %s\n' "path"   "$(readlink -f "$TARGET")"
printf '  %-14s %s bytes\n' "dimensione" "$(stat -c '%s' "$TARGET")"
have sha256sum && printf '  %-14s %s\n' "sha256" "$(sha256sum "$TARGET" | cut -d' ' -f1)"
printf '  %-14s %s\n' "tipo" "$(file -b "$TARGET")"

IS_ELF=0
file -b "$TARGET" | grep -q '^ELF' && IS_ELF=1

if (( IS_ELF )); then
    echo
    echo "## Identificazione ELF"
    readelf -h "$TARGET" 2>/dev/null | awk '
        /Class:|Data:|Type:|Machine:|Entry point/ {
            sub(/^ +/,""); printf "  %s\n", $0
        }'

    ELF_TYPE=$(readelf -h "$TARGET" 2>/dev/null | awk -F: '/Type:/{gsub(/^ +| +$/,"",$2); print $2}')
    IS_KO=0
    readelf -SW "$TARGET" 2>/dev/null | grep -q '\.modinfo' && IS_KO=1

    if (( IS_KO )); then
        echo
        echo "## Modulo kernel"
        if have modinfo; then
            modinfo "$TARGET" 2>/dev/null | awk -F: '
                /^(license|author|description|vermagic|depends|name|srcversion|intree|retpoline):/ {
                    key=$1; sub(/^[^:]*: */,"",$0); printf "  %-14s %s\n", key, $0
                }'
            echo
            echo "  Alias hardware (a quale dispositivo si lega):"
            modinfo "$TARGET" 2>/dev/null | awk '/^alias:/ {sub(/^alias: */,""); print "    " $0}' | head -20
            ALIAS_N=$(modinfo "$TARGET" 2>/dev/null | grep -c '^alias:')
            (( ALIAS_N > 20 )) && echo "    ... e altri $((ALIAS_N - 20))"
            (( ALIAS_N == 0 )) && echo "    (nessuno: probabilmente si registra a mano o e' una libreria)"

            PARAMS=$(modinfo "$TARGET" 2>/dev/null | awk '/^parm:/ {sub(/^parm: */,""); print "    " $0}')
            if [[ -n "$PARAMS" ]]; then
                echo
                echo "  Parametri del modulo:"
                printf '%s\n' "$PARAMS"
            fi
        fi

        UNDEF=$(nm -u "$TARGET" 2>/dev/null | awk '{print $NF}')
        if [[ -n "$UNDEF" ]]; then
            echo
            echo "  Sottosistemi kernel usati (dedotti dai simboli non definiti):"
            SUBSYS=$(classify_symbols "$UNDEF")
            if [[ -n "$SUBSYS" ]]; then
                printf '%s' "$SUBSYS" | sed 's/^/    - /'
            else
                echo "    (nessun pattern noto riconosciuto)"
            fi
            echo
            printf '  %-14s %s\n' "simboli est." "$(wc -l <<<"$UNDEF")"
        fi

        DEFINED=$(nm --defined-only "$TARGET" 2>/dev/null | awk '$2 ~ /^[Tt]$/ {print $NF}')
        if [[ -n "$DEFINED" ]]; then
            echo "  Funzioni definite:"
            grep -E '(probe|remove|init|exit|open|release|read|write|ioctl|irq|suspend|resume|shutdown)' \
                <<<"$DEFINED" | head -15 | sed 's/^/    /'
        fi
    else
        echo
        echo "## Superficie"
        DEF=$(nm -C --defined-only "$TARGET" 2>/dev/null | head -15)
        [[ -n "$DEF" ]] && { echo "  Simboli definiti (primi 15):"; sed 's/^/    /' <<<"$DEF"; } \
                        || echo "  (binario strippato o senza tabella dei simboli)"
        UND=$(nm -uC "$TARGET" 2>/dev/null | awk '{print $NF}' | head -20)
        [[ -n "$UND" ]] && { echo; echo "  Import (primi 20):"; sed 's/^/    /' <<<"$UND"; }
    fi
else
    echo
    echo "## Non-ELF: analisi contenitore"
    if have binwalk; then
        echo "  Firme rilevate:"
        binwalk "$TARGET" 2>/dev/null | sed -n '4,25p' | sed 's/^/    /'
    else
        echo "  binwalk non installato: impossibile mappare il contenitore"
    fi
fi

echo
echo "## Stringhe notevoli"
if have strings; then
    strings -n 6 "$TARGET" 2>/dev/null | grep -aE \
        '^/(dev|sys|proc|lib/firmware|etc)/|firmware|[0-9]+\.[0-9]+\.[0-9]+|password|secret|http://|https://' \
        | sort -u | head -20 | sed 's/^/    /'
fi

echo
echo "## Passi successivi suggeriti"
if (( IS_ELF )) && [[ "${ELF_TYPE:-}" == *REL* ]]; then
    echo "    - Modulo kernel: usa driver-protocol-recovery per estrarre registri e sequenze."
    echo "    - Parti da probe/init nel disassemblato: objdump -d --disassemble='*probe*'"
elif (( IS_ELF )); then
    echo "    - Userspace: cerca ioctl e open su /dev per capire quale driver pilota."
else
    echo "    - Estrai in una directory dedicata: binwalk -e -C /tmp/fw-extract '$TARGET'"
    echo "    - Tratta il contenuto come non fidato: nessuna esecuzione, mount in sola lettura."
fi
echo
