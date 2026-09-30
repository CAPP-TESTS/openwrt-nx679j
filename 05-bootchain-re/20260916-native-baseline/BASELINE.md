# NX679J — baseline acquisita il 16 settembre 2026

## Stato osservato

Telefono raggiungibile con ADB seriale `0123456789ABCDEF`, modello NX679J, USB `05c6:908c`. Android è avviato su `_a`, kernel `5.10.66-android12-9-00005-gf6e6376090be-ab8060604`, root Magisk disponibile, bootloader dichiarato unlocked/orange. Non sono stati eseguiti reboot, flash, modifiche a GPT/BCB o caricamenti di moduli in questa acquisizione.

Fonti aggiornate: `runtime-v2/identity.txt`, `runtime-v2/security.txt`, `runtime-v2/host-usb.txt`, `adb-props.txt`, `current-readback/device-state.txt`.

## Backup verificato

`current-readback/manifest.tsv` contiene 13 partizioni: boot_a/b, vendor_boot_a/b, dtbo_a/b, vbmeta_a/b, misc, rawdump, logdump, xbl_ramdump_a/b. Totale: 1.263.665.152 byte.

Ogni lettura è stata confrontata con la dimensione sysfs (settori da 512 byte) e con SHA-256 calcolato separatamente sul block device remoto. `sha256sum --check SHA256SUMS` ha verificato nuovamente tutti i 13 file sul PC. I digest remoti sono conservati in `current-readback/remote-SHA256SUMS`.

Non è un backup completo di userdata o di tutte le partizioni firmware. Non sostituisce una verifica del recovery EDL al momento dell'uso.

Le GPT primarie e secondarie di sda..sdf sono salvate in `runtime-v2/`. CRC di header e entry validi; tabelle primaria e secondaria identiche per ciascuna LUN. La geometria e gli attributi grezzi sono in `runtime-v2/gpt.json`; nessuna geometria è stata ricostruita o scritta sul telefono.

## Differenze importanti dall'handoff

1. **A confermato:** `boot_a.img` coincide con `magisk_patched-30700_Zx2eF.img` e `recovery-v311-magisk-20260711/boot_a-magisk-30700.img`:
   `0cd94d5627501f3c65c94542093ad18b1ec6c1c215dc6a7ecab4f941057e8364`.
2. **B diverso dal readback storico:** SHA-256 attuale `b8ab71f95a40e2445c8f3413a2b22a3a6766a5e9651186fadaf50d21936004f4`; boot header v4, kernel 35.312.128 byte, ramdisk 57.120.448 byte, signature_size 0, cmdline vuota. Fonte: `current-boot-headers.json`, derivato dai byte della nuova immagine. Questo NON è il kernel/ramdisk identificato nell'handoff di luglio.
3. **BCB pendente:** i byte 0..31 di `misc.img` contengono `bootonce-bootloader` seguito da NUL. È un possibile confondente per il prossimo riavvio, non una prova della causa dei vecchi reset. Non è stato cancellato.
4. **DT selezionato:** proprietà `ro.boot.dtb_idx=5` e `ro.boot.dtbo_idx=35`. Nuovo FDT runtime salvato in `current-device/running_fdt_20260916.dtb`, SHA-256 `0cdf7536cbf08a13f445bf1dbc0be9afcaa1986def7ff163cbf7e3de7e16fee3`. Il DTS generato presenta warning dtc sui binding vendor: non è una validazione dt-schema.
5. **Diagnostica:** pstore attualmente vuoto. I readback completi di rawdump e logdump contengono zero byte non nulli. Non offrono evidenze di un precedente ingresso nel kernel OpenWrt.
6. **USB:** UDC fisico `a600000.dwc3` configured, associato a `/config/usb_gadget/g1/UDC`; esiste anche `dummy_udc.0`. La sola presenza di `usb0` non dimostra un collegamento di rete fisico: occorre risalire all'UDC e verificare il trasporto host. Fonte: `runtime-v2/usb.txt`, `runtime-v2/network.txt`.

## Integrità della raccolta

La prima cattura `capture-baseline.sh` presentava errori di quoting `adb shell su -c` nei loop e usava `lsusb -nn`, opzione non supportata. Gli output con rc=1 non sono evidenza di assenza o di diniego root. La raccolta `capture-runtime-v2.py` mantiene il quoting fino alla shell remota e sostituisce quella telemetria; il log comandi è `runtime-v2/commands.jsonl`.

La prima lettura binaria con `dd` aveva 83 byte aggiunti su stdout dal riepilogo stderr di dd. Il controllo di dimensione l'ha respinta. La lettura è stata ripetuta silenziando solo lo stderr remoto di dd e confrontando un digest remoto indipendente; solo i file `.img` nel manifest sono backup validati.

## Prossimo passo

Identificare la provenienza esatta dell'attuale B e ricostruire gli esiti storici prima di preparare l'esperimento minimo. La prova fisica deve avere immagini con hash, modifiche esatte, readback, osservazione USB e percorso di recupero documentati. Il ritorno a fastboot, da solo, resta insufficiente a diagnosticare watchdog, AVB o panic.
