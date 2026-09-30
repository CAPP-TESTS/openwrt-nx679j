# Componenti di terzi — licenze e attribuzione

Questo archivio contiene codice e documentazione di terzi. Il file è la **attribuzione** delle parti
incluse; le licenze dichiarate sono state lette dai file `LICENSE` presenti nel pacchetto o dagli header
SPDX dei sorgenti (non da memoria). Generato il **29/09/2026**.

**I testi integrali delle licenze citate qui sono nella cartella [`LICENSES/`](LICENSES/LEGGIMI.md)**
(GPL-2.0, LGPL-2.1, Apache-2.0, BSD-3-Clause, musl, svg-spinners), con la provenienza di ciascuno.

> Questo documento è informativo e non è consulenza legale.

## 1. Codice di terzi incluso in questo archivio

| Componente | Path nel pacchetto | Licenza | Come verificata |
|---|---|---|---|
| `qrtr` (Sony Mobile Communications) | `09-albero-originale/experiments-extra/refs/qrtr-master/`, `09-albero-originale/experiments-extra/refs/qrtr-0.3/` | BSD-3-Clause | file `LICENSE` nel tree |
| `rmtfs` (Linaro) | `09-albero-originale/experiments-extra/refs/rmtfs-master/` | BSD-3-Clause | file `LICENSE` nel tree |
| `pd-mapper` (Linaro) | `09-albero-originale/experiments-extra/refs/pd-mapper/` | BSD-3-Clause | file `LICENSE` nel tree |
| `tqftpserv` (Linaro) | `09-albero-originale/experiments-extra/refs/tqftpserv-master/` | BSD-3-Clause | file `LICENSE` nel tree |
| `qdlrs` (Linaro / Bjorn Andersson) | `05-bootchain-re/nx679j-openwrt-clean/research/cve-2026-25262-sm8450/tools/qdlrs/` | BSD-3-Clause | file `LICENSE` nel tree |
| `UEFIReader` (WOA Project) | `06-ricerche/port-work/uefi-extract-tools/UEFIReader/` | MIT | file `LICENSE` nel tree |
| `qc-signature-inspector` | `06-ricerche/port-work/qc-signature-inspector/` | GPL-3.0 | file `LICENSE` nel tree |
| `init.rc` di Android (AOSP, arrivato dal ramdisk vendor) | `09-albero-originale/phase2-failure-analysis/init.rc.current` | Apache-2.0 | header `Copyright (C) 2012 The Android Open Source Project` nel file |
| Sorgenti display vendor Qualcomm (`techpack`, `LA.UM.9.14.1.c30`, `LA.VENDOR.13.2.1.c25`) | `06-ricerche/re-dsi-research/techpack/` | GPL-2.0-only | header `SPDX-License-Identifier` (16 file) e avviso breve GPL-2.0 negli header (altri 8); 2 file senza header (`op_tree.json`, `qcom_dsi_dt.txt`) |
| Estratti di sorgente per confronto: `ipa-lineage20/ipa.c` (Linux Foundation/Qualcomm Innovation Center), `mm-bearer-qmi.c` (ModemManager), `linux-5.10-ns.c` (Sony/Linaro/Linux Foundation), `qrtr-master/lib/qmi.c` (Linux Foundation/Linaro) | `09-albero-originale/experiments-extra/refs/` | `GPL-2.0-only` · `GPL-2.0-or-later` · `GPL-2.0 OR BSD-3-Clause` · BSD-3-Clause | header di copyright/SPDX nei singoli file |
| Sorgenti `libqmi` per confronto (`libqmi-qmi-endpoint-qrtr.c`, `libqmi-qmi-message.c`, `qmi-enums*.h`, `qmi-service-*.json`) | `09-albero-originale/experiments-extra/refs/` | LGPL-2.1+ | header di provenienza nei singoli file |
| Sorgenti display vendor per studio (`idlepc_research`: 532 file, in gran parte `GPL-2.0-only` SPDX con header Qualcomm Innovation Center / Linux Foundation; alcuni BSD-style) | `06-ricerche/idlepc_research/` | GPL-2.0-only / BSD secondo il file | header SPDX nei singoli file (304 con SPDX su 510 sorgenti) |
| Sorgenti DRM/kernel mainline per confronto (27 file) | `06-ricerche/re-dsi-research/mainline/` | GPL-2.0-only e dual MIT-style | header SPDX nei singoli file |
| Estratti ModemManager per confronto (`mm-broadband-modem-qmi.c`, `mm-port-qmi.c`, `mm-broadband-modem-foxconn.c`) | `09-albero-originale/experiments-extra/refs/` | GPL-2.0+ | header di copyright nei file |
| `linux-5.10-af_qrtr.c`, `qrtr-lookup.c` | `09-albero-originale/experiments-extra/refs/` | GPL-2.0-only | header SPDX nei file |
| Patch per ModemManager (`qti-patches/`, 14 file: contesto di codice GPL-2.0+ con autori upstream citati nei patch) | `02-sorgenti/wifi-luci/qti-patches/` | GPL-2.0+ (righe di contesto) | autori/commit upstream citati nei patch |
| File di integrazione ModemManager/OpenWrt (`modemmanager.sh`, `25-modemmanager-net`; copyright Aleksander Morgado / Velocloud) | `02-sorgenti/wifi-luci/mm-final/` | da verificare a monte (nessun header nel file) | copyright nel file, licenza non dichiarata |
| Script e config di LuCI (Apache-2.0) e dell'albero `/etc` di OpenWrt (`init.d`, `rc.common`, `uci-defaults`: GPL-2.0) | dentro `04-persistenza/luci.tar` e `04-persistenza/etc.tar` | Apache-2.0 / GPL-2.0 | header e struttura dei file; i **binari** del rootfs restano esclusi |
| ~~Materiale di ricerca di terzi su CVE-2026-25262~~ (articolo EN/RU, evidence, estratti del tool `edl` di B. Kerler) | `05-bootchain-re/.../cve-2026-25262-sm8450/` | — | **rimosso in questa revisione** (era materiale di terzi): nella cartella restano i nostri probe/analisi e `tools/qdlrs/` |
| Copia parziale del progetto ROCKNIX-ABL | `06-ricerche/port-work/rocknix-abl-upstream/` | non dichiarata | nessun file `LICENSE` nel materiale copiato |
| Testa dell'`init.rc` di Android (AOSP, dal ramdisk vendor) | `02-sorgenti/20260917-boot-chain/live/init_rc_head.txt` | Apache-2.0 | header `Copyright (C) 2012 The Android Open Source Project` |
| Binari dei tool Linaro (`rmtfs`, `tqftpserv`, `pd-mapper`) | dentro `04-persistenza/tools.tar` | BSD-3-Clause (build dai tree in `refs/`) | debug info del build dal tree `refs/`; le `LICENSE` stanno in `refs/` |
| Icona `loading.svg` di LuCI (dentro `luci.tar`), derivata da **svg-spinners** | `04-persistenza/luci.tar` → `www/luci-static/resources/icons/loading.svg` | MIT, Copyright (c) **Utkarsh Verma** | riferimento a `svg-spinners` nell'XML del file; testo in `LICENSES/svg-spinners-MIT.txt` |
| Runtime **musl** incorporato nei binari del progetto compilati staticamente | 25 dei 46 eseguibili | MIT (musl) — Copyright © 2005-2020 Rich Felker, et al. | testo in `LICENSES/musl-COPYRIGHT.txt` |

**Binari statici e LGPL (nota di conformità).** Alcuni binari sono **staticamente linkati**: i tre tool
Linaro in `tools.tar` sono compilati da `09-albero-originale/experiments-extra/build-pd-mapper.sh` con
`aarch64-linux-gnu-gcc -static` (una copia è `sstrip`-ata: `pd-mapper` ha i section header tagliati
per ridurne il peso, non è danneggiato), e tre strumenti host del progetto (`ui-preview`,
`rmnet-inspect-host`, `test-qmi-qrtr-host`) sono statici.
Per il runtime glibc (LGPL-2.1) questo pacchetto fornisce il testo della licenza (`LICENSES/LGPL-2.1.txt`),
il **sorgente applicativo completo** di ogni binario e le **ricette di build** (`build-*.sh` in
`09-albero-originale/experiments-extra/`), cioè il materiale con cui ricompilare e ri-linkare — la
modalità §6(a) della LGPL-2.1. Chi ridistribuisce i binari deve conservare testo e sorgenti insieme.

Le copie dei tool Linaro/Sony e i repository clonati conservano i rispettivi file `LICENSE`: se si
redistribuisce questo archivio, quei file vanno mantenuti. (`refs/qrtr-master/LICENSE`: mancava,
ripristinato in questa revisione — il testo di licenza deve accompagnare il codice ridistribuito.)

## 2. Componenti citati dalla documentazione ma **non inclusi** in questo archivio

Distribuzione di base, servizi e strumenti usati dal port; le licenze sono quelle dichiarate nella
sezione «Crediti e riferimenti» di `DOCUMENTAZIONE.md` (non riverificate una per una in questa revisione).
Le **copie del rootfs OpenWrt** che erano nell'archivio sono state rimosse: per questo non c'è qui
nessun binario di questi componenti, e nessun obbligo di accompagnamento sorgente. Nei tar di
`04-persistenza/` restano però script, config e UI di OpenWrt/LuCI: per quelli vale la §1.

| Componente | Licenza dichiarata |
|---|---|
| OpenWrt 25.12.5 (distribuzione di base, `procd`, `ubus`, `uci`, `netifd`, `rpcd`, `uhttpd`, `libubox`, `libubus`, `libuci`) | GPL-2.0 (ogni pacchetto la propria) |
| LuCI + `luci-proto-modemmanager` | Apache-2.0 |
| `busybox` | GPL-2.0 |
| `dropbear` | (non specificato localmente; vedi upstream) |
| `dnsmasq`, `hostapd`/`wpad-basic-mbedtls`, `iw`, `iwinfo` | GPL-2.0 / BSD-3-Clause secondo il componente |
| `apk`/apk-tools 3 | GPL-2.0 |
| `ModemManager`, `libmm-glib` | GPL-2.0+ / LGPL-2.1+ |
| `libqmi`/`qmicli` **binari**, `libqrtr-glib`, `libmbim` (i *sorgenti* libqmi tenuti per confronto sono in §1) | LGPL-2.1+ |
| `iptables-legacy` (Alpine) | GPL-2.0 |
| `lz4` | BSD-2-Clause |
| Magisk / `magiskboot` | GPL-3.0 |
| Python 3 | PSF-2.0 |
| `qemu-aarch64` | GPL-2.0 |
| `dtc` | GPL-2.0 / BSD-2-Clause (doppia) |
| `avbtool`, header boot image AOSP (`mkbootimg`) | Apache-2.0 |
| GNU `cpio`, `gcc`/binutils, coreutils, `openssh`, `netcat` | GPL-2.0/GPL-3.0 (con runtime exception per gcc) |
| `libqmi-glib.so`, `libgio`/`libglib` (GLib) | LGPL-2.1+ |
| `kmscube`, Weston, cog/WPE WebKit, wlroots, Mesa, libdrm, libinput, LVGL | MIT/custom secondo il progetto (valutati e scartati) |
| `DualBootKernelPatcher` (DuoWoa Authors) | MIT (dalla sua `LICENSE`) — **citato ma non incluso** in questa revisione: tool valutato, non usato dal port |

## 3. Artefatti vendor (non inclusi, non ridistribuibili)

**Contenuto derivato da firmware vendor, tenuto come evidenza di interoperabilità.** L'archivio
contiene analisi *derivate* di binari proprietari Qualcomm/Nubia, non i binari stessi: intestazioni e
disassemblaggi (`abl_text.asm`, `*.disasm.txt`, `ipa3-write-stock.asm`), dump di stringhe
(`*-strings.txt`, `*.strings-selected.txt`, `*.xref-strings.txt`), confronti e firme
(`loader-comparison/diff-*.txt`, `static-ufs/nubia-oppo-byte-diff.json`, `*-qcsignature*.txt`) e i
report di readback EDL/AVB. Sono il risultato dell'analisi su un'unità posseduta, prodotta per
interoperabilità e diagnostica, e non ridistribuiscono codice eseguibile né firmware. **Se l'archivio
diventa pubblico**, questa è la parte da rivalutare per prima: va tenuta come evidenza documentale
(citazioni brevi, offset, impronte) senza riprodurre per intero porzioni di binario.

**Rimosso in questa revisione:** le analisi derivate da firmware di **altri dispositivi o produttori**
(19 file, 3.8 MB — `diff-oppo-odin2.txt`, `nubia-oppo-byte-diff.json`, `external-loaders/odin2-*`, `sdm845-header-strings.txt`, `oppo-qcsignature.txt`, …) sono in quarantena:
non riguardano il NX679J e sarebbero state la parte più esposta. Restano le analisi derivate dal firmware
di questo dispositivo (disassemblaggio ABL, stringhe, offset), documentate qui sopra.

**Rimosso in questa revisione (residui di terzi):** le note `sahara_candidate_notes.txt` (che
citavano per esteso l'articolo e i file `evidence/` sul POCO F4 GT), il dump
`loader-comparison/prog_firehose_ddr.elf-94b6df47cd34.txt` (loader di altro produttore), le copie del
progetto **ROCKNIX/abl** (`rocknix-abl-upstream/README.md` e workflow) e il report
`analysis/abl-rocknix-signature-inspection.txt` (riguarda **SM8550**, non SM8450): materiale non
pertinente a questo dispositivo.

**Rimosso in questa revisione (riproduzioni estese):** i due disassemblaggi completi
(`abl_text.asm`, 115.593 righe; `libboot_control_qti.disasm.txt`, 20.760 righe) e i 24 device tree
decompilati dal firmware vendor (12,6 MB, incluse 4 copie del `running_fdt` del dispositivo): sono
riproduzioni estese di opera di terzi, rigenerabili dal proprio dispositivo con `ESTRAZIONE-BLOB.md`.
Nel pacchetto restano le **analisi** (offset, impronte, stringhe citate), che è ciò che serve a
capire e a ripetere il lavoro.

Kernel vendor Qualcomm/Nubia `5.10.66` (immagine `boot`), moduli `.ko`, firmware modem (`.mbn`, `/rfs`,
`modem.mdt`), firmware Wi-Fi `qca6490`, firmware touch Goodix, DTB/DTBO, ramdisk, `init` vendor,
immagini `xbl`/`abl`/`uefi` e dump di partizioni **non sono inclusi** in questo archivio: sono
artefatti proprietari del produttore, e restano soggetti alle licenze Qualcomm/Nubia. La procedura
per ri-estrarli dal proprio dispositivo è in `ESTRAZIONE-BLOB.md`.

## 4. Codice e documentazione del progetto

Il codice scritto per il progetto (payload `nx679j-*`, UI C, builder `build-v*.py`, script, strumenti) è
sotto licenza **MIT** (`LICENSE`); la documentazione sotto **Creative Commons
Attribution 4.0 International — CC BY 4.0** (`LICENSE-docs`). Attribuzione richiesta: «Saddytech —
NX679J OpenWrt» con link al repository.

I marchi di terzi non sono coperti da quelle licenze: «OpenWrt» è un marchio registrato di Software
Freedom Conservancy e questo progetto non è affiliato al progetto OpenWrt.

I componenti di terzi elencati sopra **non** rientrano in quelle licenze: restano sotto le proprie.
