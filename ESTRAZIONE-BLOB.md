# Ricostruire gli artefatti esclusi — procedure complete

Questo repository/documentazione **non include** firmware, boot image, moduli kernel o
partizioni del dispositivo: sono artefatti **proprietari del produttore**, non
ridistribuibili, e — dove possibile — sono comunque **ri-estraibili dalla propria unità**
o dal pacchetto di aggiornamento ufficiale.

Qui c'è la procedura **completa e verificata** del progetto per ricostruire ogni
componente escluso. Ogni sezione indica: cosa serve, da dove si ottiene, i comandi, e
dove viene usato nel progetto (i riferimenti puntano a `DOCUMENTAZIONE.md` e ai file in
questo archivio).

> ⚠️ **Premessa legale e pratica.** Esegui queste operazioni solo sul **tuo** dispositivo.
> Le immagini vendor (kernel, moduli, firmware, partizioni) restano soggette alle licenze
> del produttore: non ridistribuirle. L'EDL su questa unità è **inaffidabile** (vedi
> Fase 0b): il piano di recupero del progetto è lo slot A.

---

## 1. Pacchetto firmware stock (OTA ufficiale Nubia)

**Cosa dà**: `boot`, `vendor_boot`, `dtbo`, `vbmeta*`, `recovery`, `super` e il resto del
firmware di fabbrica — la base per ogni strumento successivo.

**Dove**: pacchetto di aggiornamento ufficiale Nubia/ZTE per NX679J (nel progetto:
`NX679J_Z69_UN_ZML1S_V311`; esiste anche la V411).

**Come** (usato il 2 luglio 2026, Fase 0d):

```sh
# 1) estrazione del payload OTA
payload-dumper-go -o extracted payload.bin
# → extracted/boot.img, vendor_boot.img, dtbo.img, vbmeta.img, vbmeta_system.img,
#   recovery.img, super.img, ...  (verifica con payload_properties.txt)

# 2) (in alternativa) l'OTA completo è un update .zip con payload.bin dentro:
unzip -o NX679J-V311-update.zip payload.bin payload_properties.txt META-INF/com/android/otacert
```

**Nel progetto**: Fase 0d (prima estrazione, `extracted/`), Fase 0c (la firmware volume
UEFI estratta da `uefi.img`), i DTB (`vendor_boot`), il DTBO (44 voci).

---

## 2. Immagini di partizione dal dispositivo (root)

**Cosa dà**: copie **reali** delle partizioni (compresi i settori che i pacchetti OTA non
contengono: `misc`, `modemst*`, `persist`, `rawdump`…).

**Prima**: root (Magisk) attivo su slot A; tabella nomi↔device:

| Partizione | Device | Note |
|---|---|---|
| `boot_a` / `boot_b` | `sde13` / `sde41` | 96 MiB; `boot_b` è la partizione del port |
| `vendor_boot_a` / `_b` | `sde24` / `sde52` | header v4, contiene i 9 DTB |
| `dtbo_a` / `dtbo_b` | `sde17` / `sde45` | 24 MiB, 44 voci |
| `vbmeta_a` / `_b`, `vbmeta_system_*` | `sde16` / `sde44`, … | 64 KiB |
| `abl_a` / `abl_b` | `sde10` / `sde38` | `LinuxLoader` (PE32+ LZMA) |
| `xbl_a` / `_b` | `sdb1` / `sdc1` | — |
| `uefi_a` / `_b` | `sde1` / `sde29` | firmware EDK2 |
| `super` | `sda7` | 9 GiB, partizioni logiche |
| `userdata` | `sda12` | `/data` |
| `misc` | `sda3` | BCB (`bootonce-bootloader`…) |
| `rawdump` | `sda11` | 256 MiB, volatile |
| `modem_a` / `_b` | `sde6` / … | firmware modem (vfat) |
| `modemst1` / `modemst2` / `fsg` / `fsc` | `sdf2` / `sdf3` / `sdf4` / `sdf5` | EFS del modem |
| `persist` | `sda2` | calibrazioni |

**Come**:

```sh
adb shell su -c 'dd if=/dev/block/bootdevice/by-name/boot_b  of=/sdcard/boot_b.img  bs=1M'
adb shell su -c 'dd if=/dev/block/bootdevice/by-name/dtbo_a  of=/sdcard/dtbo_a.img  bs=1M'
adb pull /sdcard/boot_b.img && sha256sum boot_b.img
# verifica dimensione attesa: boot/vendor_boot = 100663296 B (0x6000000)
```

**Nel progetto**: Fase 1 (readback `boot_a`/`boot_b` con hash), Fase 0a (`current-readback/`),
`edl-recon-20260717/` per il set completo via EDL (quando funzionava).

---

## 3. Moduli kernel vendor (`.ko`)

**Cosa dà**: display (SDE), touch, PMIC/tasti, Wi-Fi (cnss), modem (rmnet/IPA), … —
i moduli chiusi che il port carica nella chroot.

**Due strade** (entrambe usate):

```sh
# A) dal dispositivo (Android vivo, root):
adb shell su -c 'tar -C /vendor/lib/modules -cf /sdcard/vendor_modules.tar .'
adb pull /sdcard/vendor_modules.tar

# B) dal vendor_boot dell'OTA:
#    header v4: ramdisk vendor a offset variabile → estrazione con
#    unpack_bootimg --boot_img vendor_boot.img   (o gli script in 02-sorgenti/…/re/)
#    il ramdisk contiene lib/modules/<kernel>/
```

**Nel progetto**: `stock-modules/` (283 `.ko`), la catena del display (`panel_event_notifier`,
`gpi`, `i2c-msm-geni`, `goodix_core`…) e la catena PON dei tasti (`qcom-pon`,
`pm8941-pwrkey`, `pmic-pon-log`) — Fase 5.

---

## 4. Firmware del touch Goodix

**Cosa dà**: `goodix_cfg_group.bin` (4614 B) e `goodix_firmware.bin` (182528 B) — senza
questi il driver resta in `-ENOENT` (Fase 2/5).

```sh
# A) dall'OTA vendor.img (richiede e2fsprogs):
debugfs -R 'dump /firmware/goodix_cfg_group.bin /tmp/goodix_cfg_group.bin' vendor.img
debugfs -R 'dump /firmware/goodix_firmware.bin     /tmp/goodix_firmware.bin' vendor.img

# B) dal dispositivo vivo (root):
adb shell su -c 'ls /vendor/firmware/goodix*'
adb pull /vendor/firmware/goodix_firmware.bin
```

**Dove vanno**: in `/lib/firmware` del root attivo — nel progetto via il ramdisk
(iniezione **dopo** i tar di persistenza) con `firmware_class.path=/owrt/lib/firmware`.

---

## 5. DTB e DTBO

**Cosa dà**: l'albero del dispositivo (il DT del NX679J = DTB **#5** di `vendor_boot` +
overlay **#35** di `dtbo`).

```sh
# dal dispositivo:
adb shell su -c 'cat /sys/firmware/fdt' > running_fdt.dtb     # FDT vivo
dtc -I dtb -O dts -o running_fdt.dts running_fdt.dtb

# dal vendor_boot OTA (9 FDT concatenati, campo dtb):
#   script: 02-sorgenti/20260917-boot-chain/re/fdt-enum.py
# dal dtbo OTA (44 voci):
#   script: 02-sorgenti/20260917-boot-chain/re/dtbo-parse.py  (es. entry #35)
```

**Nel progetto**: `running_fdt.dts` (modello Waipio, `board-id 0x10008`), la modifica
dell'entry #35 (`dsi_r66451_amoled_cmd` → `dsi_nubia_r6130_amoled_cmd_dphy`), Fase 0c/5.

---

## 6. Boot image e ricostruzione del contenitore

**Cosa dà**: la base per costruire l'immagine del port (Fase 1/2).

```sh
# readback della base (dal device):
adb shell su -c 'dd if=/dev/block/bootdevice/by-name/boot_b of=/sdcard/boot_b.img bs=1M'
adb pull /sdcard/boot_b.img        # → usata come boot_b-live.img dal builder

# il builder riassembla SOLO il ramdisk (header v4 invariato):
#   ramdisk a offset 49115136 — ramdisk_size a offset 12 — lz4 -l -9 (legacy!)
#   script completo: 02-sorgenti/wifi-luci/build-v90.py
# alternativa: magiskboot unpack/repack  (o patch Magisk per lo slot A)
```

**Nota**: le immagini di boot contengono il kernel vendor → per questo **nessuna** immagine di boot è nel repository, nemmeno di riferimento.

---

## 7. Loader Firehose (EDL)

**Cosa dà**: i `prog_firehose_*.melf` per l'accesso EDL (usati quando l'EDL funzionava,
17/07/2026).

**Dove**: repository pubblico **bkerler/Loaders** (loader per SM8450); nel progetto anche
loader Oppo/Nubia per confronto statico
(`05-bootchain-re/nx679j-openwrt-clean/research/cve-2026-25262-sm8450/loader-comparison/`).

```sh
edl r boot_a boot_a.img --loader=prog_firehose_ddr.melf --memory=ufs   # esempio
```

**Caveat**: su questa unità l'EDL è **rotto** (Sahara degenere, Fase 0b). I loader di
terzi **non vanno ridistribuiti** né inviati a caso.

---

## 8. Sorgenti kernel GPL

**Cosa dà**: i sorgenti del kernel vendor (per studio o ricompilazione).

- Drop **GPL ufficiale Nubia/ZTE** per NX679J (V311/V411): portale open source ZTE/Nubia.
- Repository pubblici del produttore: **github.com/ztemt** (NX679S con `5.10.101`,
  NX729J, NX709S, NX669S) — il kernel del NX679J è `5.10.66`, quindi la versione esatta
  va verificata sul drop GPL (vedi Fase 0c/5 per i caveat).
- Nel progetto: `kernel-patch-test/` (estratto, in 09), `hypervisor-bypass/` (le nostre
  prove, incluse), i confronti con LineageOS/mu_aloha citati nei crediti.

---

## 9. GPT (tabella partizioni)

```sh
# via EDL (quando funzionava):
edl printgpt --loader=… --memory=ufs          # output in 05-bootchain-re/edl-recon-20260717/printgpt.txt

# alternativa dal device (root): copia i settori GPT e decodificali
adb shell su -c 'dd if=/dev/sde of=/sdcard/gpt_head.img bs=512 count=34'
#   script di decodifica: 02-sorgenti/20260917-boot-chain/re/gpt-slots.py
#   oppure: sgdisk -p gpt_head.img
```

---

## 10. EFS e calibrazioni modem

```sh
adb shell su -c 'dd if=/dev/block/bootdevice/by-name/modemst1 of=/sdcard/modemst1.img'
# idem: modemst2 (sdf3), fsg (sdf4), fsc (sdf5)
```

**Perché servono**: `rmtfs` le presenta al modem (`[RMTFS] open /boot/modem_fs1`).
**Attenzione**: non riscriverle senza necessità — contengono calibrazioni uniche.

---

## 11. Ambiente di test headless (opzionale)

Gli script delle prove di boot headless (QEMU/monitor USB) sono in
`09-albero-originale/native-openwrt-usb-build/` — richiedono un kernel e un ramdisk
propri; i log dei tentativi (2-3 giorni di lavoro, Fase 1) sono inclusi come evidenza.

---

*Questo documento fa parte dell'archivio del progetto NX679J. Le procedure sono quelle
effettivamente eseguite nel progetto; i riferimenti alle sezioni puntano a
`DOCUMENTAZIONE.md`.*
