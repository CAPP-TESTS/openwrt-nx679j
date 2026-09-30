## Crediti e riferimenti: strumenti e codice di terzi

> **Convenzione.** Le licenze indicate sono quelle **note a monte** (non dedotte dai documenti del
> progetto); dove non sono note si scrive `(non specificato localmente)`. Gli artefatti vendor
> Qualcomm/Nubia (kernel, moduli `.ko`, firmware, DTBO, immagini di partizione) sono **copie locali
> estratte dall'unità in nostro possesso**: non vengono redistribuiti e restano soggetti alle
> licenze del produttore. Sono elencate anche le alternative **valutate e scartate**, con la ragione,
> perché la decisione fa parte del lavoro.

---

### 1. Distribuzione di base, rootfs e servizi

- **OpenWrt 25.12.5 (`r33051-f5dae5ece4`), target `armsr/armv8`, `aarch64_generic` (musl)**
  - Uso: distribuzione di base del port sullo slot B; rootfs consegnata dal ramdisk, init, servizi, LuCI, gestione dei pacchetti via `apk`.
  - Origine: <https://openwrt.org> · <https://github.com/openwrt/openwrt> · <https://downloads.openwrt.org/releases/>
  - Licenza: GPL-2.0 (ogni pacchetto mantiene la propria licenza).

- **OpenWrt SDK 25.12.5 armsr/armv8 (`gcc-14.3.0_musl`, `aarch64-openwrt-linux-gcc`)**
  - Uso: cross-compilazione di ModemManager e libqmi patchati, della UI C, dei tool DRM/touch e dei moduli del progetto.
  - Origine: <https://downloads.openwrt.org/releases/25.12.5/targets/armsr/armv8/>
  - Licenza: GPL-2.0.

- **Toolchain di sistema `aarch64-linux-gnu-gcc` (glibc, `-static`)**
  - Uso: fallback usato dai documenti quando l'SDK OpenWrt non era presente; i binari glibc statici girano sul rootfs musl del device (verificato). Tool host per le sonde DRM (`drm-probe`, `drmprops`, `plane78-scanout-once`).
  - Origine: GNU/Linux — cross-toolchain della distribuzione host.
  - Licenza: GPL-3.0 (con runtime exception).

- **Componenti OpenWrt di sistema: `procd`, `ubus`/`ubusd`, `uci`, `netifd`, `rpcd`, `uhttpd`, `libubox`, `libubus`, `libuci`, `libblobmsg-json`, `libjson-c`, `jshn`, `kmodloader`, `ujail`**
  - Uso: init e supervisione dei servizi, bus di stato (`ubus call network.interface.modem status`), configurazione persistente (`uci`), pagine LuCI, hotplug.
  - Nota: la UI sul display legge lo stato via **ubus** (`network.interface.*`, `system.*`, `file`/`rpcd-mod-file`, `luci.setPassword`); `ujail` è stato **disattivato** perché i jail procd falliscono su questo kernel vendor.
  - Origine: <https://github.com/openwrt/openwrt> (repo satellite `openwrt/rpcd`, `openwrt/netifd`, `openwrt/ubox`)
  - Licenza: GPL-2.0 / LGPL-2.1 secondo il componente.

- **`apk` / apk-tools 3 (formato feed OpenWrt ≥ 25.12)**
  - Uso: installazione e allineamento dei pacchetti `luci-*` sul device; il device parte dal 1970, quindi `apk` richiede prima la sincronizzazione NTP e i feed su `http://` (il `wget` busybox non completa il TLS).
  - Origine: <https://gitlab.alpinelinux.org/alpine/apk-tools>
  - Licenza: GPL-2.0.

- **`apk-tools-static` 3.0.8 (Alpine `edge`, x86_64)**
  - Uso: decodifica dei `packages.adb`/`.apk` OpenWrt per ottenere dimensioni e dipendenze reali (usato per la stima di ingestibilità di Cog/WPE).
  - Origine: <https://dl-cdn.alpinelinux.org/alpine/edge/main/x86_64/apk-tools-static-3.0.8-r0.apk>
  - Licenza: GPL-2.0.

- **`busybox` (OpenWrt) e busybox del vendor**
  - Uso: shell del rootfs; applet limitate (niente `stat`, `od`, `timeout`, `pkill`, `top`, `modetest`, `mdev`) — vincolo che ha determinato molti script del progetto.
  - Origine: OpenWrt / <https://busybox.net>
  - Licenza: GPL-2.0.

- **`dropbear` (client/server SSH)** — Uso: accesso al device via USB gadget (10.0.0.1, rtt 3 ms) e Wi-Fi (192.168.77.1); host key rigenerata a ogni boot. Origine: <https://matt.ucc.asn.au/dropbear/dropbear.html> · Licenza: (non specificato localmente).

- **`dnsmasq`, `hostapd`/`wpad-basic-mbedtls`, `iw`, `iwinfo`, `wireless-regdb`, `wifi-scripts`, `ucode-mod-*`**
  - Uso: AP Wi-Fi 5 GHz `NX679J-TEST` sul QCA6490, DHCP LAN, watchdog e riavvio di dnsmasq dopo l'AP.
  - Origine: feed OpenWrt `base`/`packages` · Licenza: GPL-2.0 / BSD-3-Clause (hostapd) secondo il componente.

- **`firewall4` / `nftables`** — valutati e **non usabili**: il kernel vendor non ha `nf_tables` (`nft: cache initialization failed`), quindi `fw4` non parte e il launcher non lo tocca. Origine: <https://github.com/openwrt/firewall4> · Licenza: (non specificato localmente).

- **`iptables-legacy` 1.8.11 da Alpine aarch64/musl (`xtables-legacy-multi`, `/usr/lib/xtables/*.so`, `libxtables`/`libip4tc`/`libip6tc`)**
  - Uso: NAT del WAN condiviso SIM → LAN Wi-Fi (il kernel ha `xtables` built-in ma non il userspace iptables).
  - Origine: pacchetto Alpine Linux · Licenza: GPL-2.0.

- **LuCI + `luci-proto-modemmanager` (versione allineata `26.263.44884~0834d09`)**
  - Uso: interfaccia web (uhttpd bindato **solo** su 10.0.0.1 e 192.168.77.1), pagina `Status → Cellular Network`, descrittore del proto; i file JS sul device sono minificati, quindi le citazioni di riga si fanno sul sorgente upstream.
  - Origine: <https://github.com/openwrt/luci> · Licenza: Apache-2.0.

---

### 2. Strumenti host di build e impacchettamento

- **Python 3** — Uso: tutti i builder (`build-vNN.py`, `build-v90.py`), gli strumenti (`goodix-cmdline-path.py`, `dts-on-command-to-txcmd.py`, `luci-mm-tokcmp.py`) e le verifiche. Origine: <https://www.python.org> · Licenza: PSF-2.0.

- **GNU `cpio`** — Uso: creazione/estrazione degli archivi `newc` del ramdisk e del rootfs (`find . | LC_ALL=C sort | cpio -o -H newc`, `cpio -idmu`); i layer sono **concatenati** (l'ultima voce vince) e le voci `uid/gid` riscritte a 0. Origine: GNU project · Licenza: GPL-3.0.

- **`lz4` (CLI, `lz4 -l -9` = frame *legacy*)**
  - Uso: compressione del ramdisk dell'immagine `boot_b`; distinzione critica fra LZ4 **legacy** (`02 21 4c 18`) e **frame** (`04 22 4d 18`).
  - Origine: <https://github.com/lz4/lz4> · Licenza: BSD-2-Clause.

- **Magisk / `magiskboot` 30.7**
  - Uso: unpack/repack delle immagini boot (`magiskboot unpack -h`, `magiskboot repack`) e compressione `compress=lz4_legacy` nella prima fase del port (`port-work/openwrt-phase4-*`); l'immagine **Magisk-patchata su slot A** è la baseline/oracolo che avvia Android e serve da controllo a una variabile.
  - Origine: <https://github.com/topjohnwu/Magisk> · Licenza: GPL-3.0.

- **Header AOSP di boot image (`system/tools/mkbootimg/include/bootimg/bootimg.h`, `boot_img_hdr_v3/v4`, `vendor_boot_img_hdr_v4`)**
  - Uso: interpretazione/riscrittura dell'header `ANDROID!` (44 byte fissi, cmdline 1536 B su v3 e 4096 B su v4 — sfruttato da `goodix-cmdline-path.py` per `firmware_class.path=`), tabella ramdisk del `vendor_boot`.
  - Origine: AOSP — <https://android.googlesource.com/platform/system/tools/mkbootimg> · Licenza: Apache-2.0.

- **`avbtool`** — Uso: ispezione delle immagini `vbmeta` nella fase di ricostruzione della catena di boot. Origine: AOSP (external/avb) · Licenza: Apache-2.0.

- **`dtc` (device tree compiler)** — Uso: decompilazione del FDT vivo (`dtc -I dtb -O dts -f`) e ricostruzione dell'immagine **DTBO** per il fix `qcom,dsi-default-panel` del fragment 35 (round-trip md5 verificato prima del flash). Origine: <https://git.kernel.org/pub/scm/utils/dtc/dtc.git> · Licenza: GPL-2.0 / BSD-2-Clause (doppia).

- **`qemu-aarch64` / qemu-user** — Uso: prova locale della catena di boot e dei moduli **prima** del flash (`qemu-aarch64 -L <root> busybox echo OK`); i suoi verdetti su shebang non valgono come verdetti del kernel. Origine: <https://www.qemu.org> · Licenza: GPL-2.0.

- **Strumenti host di servizio**: `dd` + `sync -f`/`drop_caches` + `md5sum`/`sha256sum` (protocollo di flash con due letture fredde), `nc` (lettura dello snapshot dal relè v9), `ssh`/`scp -O`, `curl`, `jq`. Licenze: GPL-2.0/GPL-3.0 (coreutils, netcat, openssh) salvo dove diversamente indicato — `(non specificato localmente)` per i singoli pacchetti.

- **`opencli` — Browser Bridge (`/usr/bin/opencli`)**
  - Uso: verifica delle pagine LuCI con un browser reale (`opencli --profile pdc8925e browser <sess> open|state|screenshot`), senza chiedere screenshot all'utente.
  - Origine: `/usr/bin/opencli` (strumento di ambiente) · Licenza: (non specificato localmente).

---

### 3. Modem, QMI e datapath

- **`libqmi` / `qmicli` — versione sul device 1.36.0** (master OpenWrt nel 2026: 1.38.0)
  - Uso: lettura stato modem su QRTR (`qmicli -p -d qrtr://0`, NAS/DMS/UIM), origine della pagina Cellular scritta da noi; `--wds-*` come riferimento per il bearer.
  - **Questioni di versione/collezione** (verificate): i fix di memoria `93e65e4`+`fd79fd3` (OOB read nel path QRTR, issue #133) **non sono in nessuna release**; `--client-cid`/`--client-no-release-cid` **non attraversano i processi** su QRTR (MR !382 chiusa); `qmi-network` è **pericoloso** qui (riscrive il data-format WDA senza verificare il lato kernel) e quindi **non va usato**; `-p`/`qmi-proxy` non è la risposta alla serializzazione (issue #113 aperta, `ClientIdsExhausted` con #62).
  - Origine: <https://gitlab.freedesktop.org/mobile-broadband/libqmi> · <https://github.com/linux-mobile-broadband/libqmi> · Licenza: LGPL-2.1+.

- **`libqrtr-glib` — 1.2.2-3** — Uso: trasporto QRTR dei QMI, timeout della lookup di bus **1 s per processo**, lettura del name service in-kernel. Bug noti non corretti (`f4fd658` UAF in `qrtr_node_remove_service_info`, issue #7) — da trattare come crash possibile. Origine: <https://gitlab.freedesktop.org/mobile-broadband/libqrtr-glib> · Licenza: LGPL-2.1+.

- **`qmi-proxy`** — Uso: valutato per condividere un device QMI fra più client; **non installato** su OpenWrt e **non usato** (le invocazioni `qmicli` separate non condividono il CID: `Unknown client 1`). Origine: parte di libqmi · Licenza: LGPL-2.1+.

- **`ModemManager` 1.24.0 (+ `libmm-glib`, plugin `qcom-soc`) — adottato e poi RIMOSSO**
  - Uso: fase standard (v63→v90) di controllo del modem e pagina LuCI; poi **decisione del 24/09 di eliminarlo** (−179 s sull'oggetto modem, −324 s di cleanup, via il rischio `Carrier: Absent`), con la sola funzione da conservare (riconnessione su deattivazione dell'operatore) riassegnata al nostro supervisor.
  - Patch nostre ispirate a sorgenti terzi: `0100` (net virtuali), `0101` (skip data-format su QRTR), `0102` (rmnet come data port), `0200/0201` (`net_driver`→`ipa`, multiplex REQUIRED), `0202` (link port timeout 10 s), `0203` (`iflink`), `0204` (nome `qmapmux` senza punto); regola udev `80-mm-<device>.rules`.
  - Origine: <https://gitlab.freedesktop.org/mobile-broadband/ModemManager> · <https://github.com/linux-mobile-broadband/ModemManager> · Licenza: GPL-2.0+ / LGPL-2.1+.

- **`qualcomm-linux/meta-qcom` — 6 patch QTI del datapath (`0001`…`0005`)**
  - Uso: **modello** per le nostre patch (fallback EMBEDDED per driver non riconosciuto su QRTR = MR !1452, QRTR+MHI = MR !1443, BindMuxDataPort, DPM open port, `sio_port_per_port_number`); file conservati in `qti-patches/`.
  - Origine: `qualcomm-linux/meta-qcom` (`recipes-connectivity/modemmanager/files/`, branch master) · Licenza: (non specificato localmente).

- **`meizu-m2172-mainline/ModemManager` (fork athbe)** — Uso: fonte dei commit `95882e2` (fallback su generic QMI data port), tag `ID_MM_QMI_FIXED_MUX_ID` (`9e407303`), `ID_MM_QMI_DEFAULT_MULTIPLEX` (`18105f69`), branch `sdx55m-mhi-wds-mux-1.24`; le regole udev meizu confermano il nostro pattern `ID_MM_PHYSDEV_UID=qcom-soc`. Origine: <https://github.com/meizu-m2172-mainline/ModemManager> · Licenza: (non specificato localmente, derivata da ModemManager).

- **`libudev-zero` (OpenWrt, 1.0.5, Daniel Golle)** — valutato e **scartato**: solo `libudev.pc`, nessun `gudev-1.0` (MM richiede ≥ 232), nessun demone; OpenWrt pinna MM con `-Dudev=false` e MM fa il parsing interno delle regole udev (parser dichiaratamente "non completo"). Origine: <https://github.com/openwrt/packages> (`libs/libudev-zero`) · Licenza: (non specificato localmente).

- **`uqmi` (OpenWrt) e il demone `uqmid` (in-tree)** — Uso: **valutati e scartati**: `uqmi` parla QMI solo su `/dev/cdc-wdm*`, **zero hit su QRTR**; `uqmid` idem e non pacchettizzato in 25.12. Origine: <https://github.com/openwrt/uqmi> · <https://lxr.openwrt.org/source/uqmi/uqmid/uqmid.c> · Licenza: GPL-2.0.

- **`wwand` (ddimension, ucode)** — valutato e scartato: solo USB `qmi_wwan*`/MHI, rifiuta i driver non USB/MHI (`discovery.uc`), zero QRTR nel codice. Origine: <https://github.com/ddimension/wwand> · Licenza: (non specificato localmente).

- **Altri candidati OpenWrt valutati e scartati** (letti nel sorgente, non nelle descrizioni): `QModem` (FUjr — manager AT, QRTR compilato fuori), `qminfo` (modemfeed — `g_file_new_for_path()`, nessun URI), `modemdata`/`md_uqmi` (usa `uqmi`), `luci-proto-qmi` (match `/^cdc-wdm/`), `luci-app-5gmodem` (gate su character device). Licenze: (non specificato localmente).

- **`rmtfs`, `tqftpserv`, `pd-mapper` (linux-msm) — USATI**
  - Uso: servitori del modem sul device: `rmtfs` (EFS da `modemst1/2`, `fsg`, `fsc` — nel log `[RMTFS] open /boot/modem_fs1`), `tqftpserv` (root `/rfs`, serve i file che il modem chiede durante il bootup), `pd-mapper` (servizio 64 `tms/servreg` → `msm/modem/root_pd`). Ordine corretto: **tqftpserv prima del `start` del remoteproc**.
  - Origine: <https://github.com/linux-msm/tqftpserv> (e `rmtfs`, `pd-mapper` dello stesso namespace) · Licenza: (non specificato localmente).

- **postmarketOS / pmaports — MR 3269 e port `postmarketos-blackshark-klein` (CaullenOmdahl)**
  - Uso: fonte dell'ordine dei servitori ("Ideally tqftpserv should be started before rmtfs…") e della tecnica di *forced EE transition* su MHI (`docs/modem/troubleshooting.md`).
  - Origine: <https://git.askiiart.net/askiiart/pmaports/src/commit/e60195f8ae51abe422b3ccda685c1e3612209130/modem/tqftpserv> · <https://github.com/CaullenOmdahl/postmarketos-blackshark-klein> · Licenza: (non specificato localmente).

- **`qrtr-ns` userspace, `diag-router`** — valutati e **non necessari**: su kernel 5.10 il name service QRTR è **in-kernel**; `diag-router` serve al routing DIAG, non a QMI+dati (in pmaports disabilitato di default). Origine: namespace `linux-msm` / pmaports · Licenza: (non specificato localmente).

- **`mwan3` e `watchcat`** — valutati come meccanismo di riconnessione e **scartati**: `mwan3` reagisce solo a eventi netifd `ifup/ifdown/connected/disconnected` e fa failover, non conserva il bearer. Origine: <https://github.com/openwrt/packages> (`net/mwan3`, `network/services/watchcat`) · Licenza: (non specificato localmente).

- **ROOter Connection Monitor (`ofmodemsandmen`) e script `10-report-down`**
  - Uso: **prior art** per il supervisor di riconnessione (isteresi ping "Interface Down/Up", azioni log/reboot/reconnect/toggle; monitor basato sullo stato MBIM nella versione successiva). Il dispatcher `10-report-down` di ModemManager+OpenWrt è il riferimento della notifica di disconnessione a netifd.
  - Origine: <https://ofmodemsandmen.com/monitor.html> · <https://github.com/ROOterDairyman/ROOter> · <https://raw.githubusercontent.com/openwrt/packages/master/net/modemmanager/files/usr/lib/ModemManager/connection.d/10-report-down> · Licenza: (non specificato localmente).

- **`libmbim`** — Uso: consultato per la semantica del multiplexing bearer (`MaxActiveMultiplexedBearers`) e per il confronto ECM/MBIM vs ping. Origine: <https://github.com/linux-mobile-broadband/libmbim> · <https://modemmanager.org/docs/libmbim/mbim-protocol> · Licenza: LGPL-2.1+.

---

### 4. Componenti vendor Qualcomm/Nubia riusati (artefatti locali del dispositivo)

- **Kernel vendor Qualcomm/Nubia `5.10.66-android12-9-…` (immagine `boot` stock)** — Uso: **kernel della porta** (nessuna patch al kernel per display e modem); ricostruito solo nell'header/ramdisk. Origine: partizione stock dell'unità (`boot_a`/`boot_b`, vermagic `5.10.66-gki-g491fe99db339`). Licenza: GPL-2.0 (kernel) — artefatto locale non ridistribuito.
- **Moduli vendor estratti dal device** — `panel_event_notifier.ko`, `msm_ext_display.ko`, `gpi.ko`, `i2c-msm-geni.ko`, `goodix_core.ko`, `qcom-pon.ko`, `pm8941-pwrkey.ko`, `pmic-pon-log.ko`, `pmic_glink.ko`, `qti_battery_charger.ko`, `charger-ulog-glink.ko`, `ucsi_glink.ko`, lo stack `cnss`/`qca_cld3_qca6490.ko`, `rmnet_*`/`ipam`/`ipa_*`, `smp2p.ko`, `qcom_q6v5_pas.ko`, `mdt_loader`… — Uso: display, touch, tasto laterale, alimentazione/Type-C, Wi-Fi, modem e datapath. Origine: `vendor_ramdisk00` / `/vendor/lib/modules` dell'unità. Licenze: GPL-2.0 e licenze Qualcomm (artefatti locali, non ridistribuiti).
- **Firmware vendor** — `modem.mdt`/`modem.b*` e firmware X65 (`/rfs` con `modem_pr`, 165 file), `qca6490/{amss20,amss,bdwlan.elf,bdwlang.elf,regdb,m3}.bin`, firmware touch `goodix_firmware.bin` e `goodix_cfg_group.bin` — Uso: avvio di MSS/adsp/cdsp/slpi e del Wi-Fi; il firmware touch è iniettato in `/lib/firmware` del **root vero** (`/proc/1/root/lib/firmware`) con `firmware_class.path=` aggiunto alla cmdline. Origine: partizioni `modem_a` (`/dev/block/sde6`, `/vendor/firmware_mnt`), `/rfs` e rootfs Android stock. Licenza: proprietaria Qualcomm/Nubia — artefatti locali, non ridistribuiti.
- **DTB/DTBO vendor** — `dtb_idx=5`, `dtbo_idx=35`; il DTBO dello slot B è stato modificato in **una sola entry** (`qcom,dsi-default-panel`: `dsi_r66451_amoled_cmd` → `dsi_nubia_r6130_amoled_cmd_dphy`), ricostruito con `dtc` e flashato con round-trip byte-perfect. Origine: partizione `dtbo`/`vendor_boot` dell'unità. Licenza: (artefatto vendor locale).
- **`xbl_a`/`abl_a`/`uefi_a` stock Nubia `NX679J_Z69_UN_ZML1S_V311`** — Uso: studio della catena di boot (LLVM da `android.googlesource.com/toolchain/llvm-project` per l'identificazione del `LinuxLoader`); conferma che **la firma AVB non è applicata** con bootloader sbloccato. Licenza: proprietaria.
- **`crashlog-dump.sh` (vendor)** — Uso: unico canale che sopravvive a un reset silenzioso (scrive il tail di `dmesg` ogni 3 s negli slot del `rawdump`); il progetto ci ha aggiunto blackbox/watcher su slot 409/410.
- **RAWDUMP / misc / GPT** — Uso: canale di diagnosi e di persistenza; nota: il `rawdump` viene **azzerato al boot** e non è affidabile per la persistenza (approccio v60 falsificato).

---

### 5. Display, DRM/KMS e sorgenti Qualcomm consultate

- **Documentazione DRM/KMS e DRM UAPI del kernel**
  - Uso: riferimento normativo per `CREATE_DUMB`/`pitches[]` (il pitch reale è **4352** contro `1080*4=4320`: causa del glitch del vecchio kiosk), flag atomici (`TEST_ONLY`, `NONBLOCK`, `ALLOW_MODESET`, `PAGE_FLIP_EVENT`), fencing esplicito (`OUT_FENCE_PTR`), `lastclose` vs rilascio del master.
  - Origine: <https://docs.kernel.org/gpu/drm-kms.html> · <https://docs.kernel.org/gpu/drm-uapi.html> · <https://www.kernel.org/doc/html/v5.10/gpu/drm-uapi.html> · <https://github.com/gregkh/linux/blob/v5.10.66/drivers/gpu/drm/drm_file.c#L450-L453> · <https://github.com/gregkh/linux/blob/v5.10.66/drivers/clk/qcom/clk-rcg2.c#L100-L147> (handshake RCG / `CMD_UPDATE`, poll 500×1 µs)
  - Licenza: documentazione kernel (GPL-2.0 / CC-BY-SA per la doc).

- **Albero stable Linux `6.12` locale e alberi di riferimento peer (`re-cmdmode/*_510.c`, `lop_sm8450`, `linux-6.8`, `linux-6.11`)**
  - Uso: studio del loop atomico, del kickoff SDE, del fence e del power-collapse **per analogia**, senza attribuire al kernel live ciò che non è verificato byte-per-byte.
  - Origine: <https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git> (checkout locale `/home/user/linux-6.12`) · Licenza: GPL-2.0.

- **Sorgenti display vendor Qualcomm (`techpack` SDE) e comparabili pubblici**
  - Uso: ricostruzione della ricetta di commit dell'HAL (`SetupAtomic`: `PLANE_SET_FB_ID+SET_CRTC` ogni frame, `CRTC_SET_MODE` solo al first-cycle) e delle proprietà CRTC (`idle_pc_state`, `autorefresh`, `frame_trigger_mode`); la sorgente locale dichiara **5.4.242**, la tree pubblica ZTE/NX679S **5.10.101** → usate come ipotesi di flusso, mai come prova del kernel live 5.10.66.
  - Origine: `re-nubia-disp/src` (locale) · <https://github.com/ztemt/NX679S/tree/0280bdce975602ff17161f5de1b8fc63dc96bd47> (`display-drivers.zip`, `msm/dsi/dsi_panel.c`) · <https://git.codelinaro.org/clo/la/platform/vendor/opensource/display-drivers/-/blob/ac17a22157f56a438e24009e4fa91a69100612ef/msm/dsi/dsi_display.c#L1706-1907> · <https://github.com/TheGammaSqueeze/GammaOSNextDistribution/blob/0de890dede9a550da87920d61b4b08da7c80fb02/hardware/qcom-caf/sm8450/display/sdm/libs/core/drm/hw_device_drm.cpp>
  - Licenza: GPL-2.0 / licenze Qualcomm (sorgenti vendor).

- **`kmscube`, `weston`, `cog`/WPE WebKit, `wpebackend-fdo`, `libwpe`, `cage`, `wlroots`, `libsdl2`, Mesa (`llvmpipe`/`softpipe`), `libdrm`, `libgbm`, `libinput`, `libegl`/`libglesv2`, LVGL** — **alternative valutate e scartate** per la UI sul pannello: motore non pacchettizzato per `aarch64_generic` (configure di WebKit fallito nel buildbot), ~50–70 MB compressi contro 6,7 MiB liberi in `boot_b`, GL solo software, e il legacy page-flip di Cog è `ENOTTY` su questo driver; `kmscube` lanciato **senza master attivo** ha causato 154 fault SMMU e il blocco del device (incidente documentato, da non ripetere). Origine: <https://gitlab.freedesktop.org/mesa/kmscube>, <https://github.com/wayland-project/weston>/<https://gitlab.freedesktop.org/wayland/weston>, <https://wpewebkit.org>, <https://github.com/Igalia/cog>, <https://lvgl.io> · Licenze: MIT/custom secondo il progetto `(non specificato localmente)`.

- **Project Mu (`mu_aloha_platforms-main`, `sm8450-mainline/Mu-Qcom`)** — Uso: riferimento per il bootloader/firmware UEFI nella fase di studio della catena; l'unità ha anche una copia locale in `verified-port/`. Origine: <https://github.com/microsoft/mu_aloha_platforms> · Licenza: (non specificato localmente).

- **`linux-msm.github.io/mainline-status` (SM8450)** — Uso: stato del supporto mainline per display DSI/DP/GPU, come orizzonte del lavoro futuro. Origine: <https://linux-msm.github.io/mainline-status/soc/sm8450> · Licenza: (non specificato localmente).

- **`panel-visionox-vtdr6130.c` (mainline `drivers/gpu/drm/panel/`)** — Uso: esistenza e riferimenti del driver **mainline** del pannello (compatible `visionox,vtdr6130`, DSC aggiunto nel 2026, default su SM8550/8650-QRD) come alternativa futura al driver vendor. Origine: <https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git> · Licenza: GPL-2.0.

---

### 6. Touch Goodix — sorgenti consultate

- **Mainline `goodix_berlin_core.c` + `goodix_berlin_{i2c,spi}.c`** (merge 2024, in `kernel-patch-test/linux-6.11/drivers/input/touchscreen/`) — Uso: riferimento per la patch ~3 righe che aggiungerebbe il GT9897 su I2C (`gt9897_data` con `fw_version_info_addr=0x1000C`, `ic_info_addr=0x10068` + compatible in `goodix_berlin_i2c.c`); il bind Berlino-A è già documentato, l'I2C no → sarebbe il primo device pubblico GT9897-su-I2C. Licenza: GPL-2.0.
- **Sorgente driver vendor Nubia `goodix_berlin_driver_v1.0.1` (`goodix_brl_i2c.c`, `goodix_brl_spi.c`)** in `stock-kernel-source/drivers/nubia/touch/` — Uso: confronto **riga per riga** dell'init con lo stock (identico, incluse le due `[GTP-ERR]` benigne `can't find valid panel` e `failed get panel-max-p, use default`) → il guasto è nel **runtime**, non nell'init. Licenza: GPL-2.0 (sorgente vendor pubblicato) — artefatto locale.
- **Driver vendor `goodix_core.ko` e alias `i2c:gtx8_i2c` / `platform:nubia_goodix_ts`** — Uso: il driver **effettivamente** in uso sul device; caricato in catena `gpi.ko → i2c-msm-geni.ko → goodix_core.ko` (senza `gpi.ko` il probe I2C resta in deferred probe per sempre). Stato misurato: l'hardware risponde sul bus ma **non consegna eventi**. Licenza: (driver vendor, artefatto locale).
- **Nodi debugfs vendor del driver touch** (`fwupdate/result`, `get_rawdata`, `esd_info`, `nbsp_mode`, `bsp_mode`) — Uso: sondati e **dichiarati da non leggere** (un read ha coinciso con un crash del kernel). `(non specificato localmente)`.

---

### 7. Input, UI e font

- **Interfaccia kernel `uinput` (`/dev/uinput`, major 10 minor 223)** — Uso: iniezione di un tocco **vero** per verificare il percorso tocco→azione senza dita; `/dev` è una directory del ramdisk (non `devtmpfs`), quindi il nodo si crea con `mknod`. Licenza: GPL-2.0 (kernel).
- **`uinput-touch.c`, `touch-selftest.c`, `ui-preview.c`/`ui-preview2.c`** — **codice del progetto**, scritto su questa ABI: iniettore di tocchi, self-test di `touch_scan`+`poll_touch`+`hit_test` senza aprire il DRM, e rendering delle pagine in PPM per guardarle prima di toccare il pannello. (Non è codice di terzi.)
- **`nx679j-kiosk3.c` (e prima `nx679j-kiosk2.c`, `nx679j-atom10..17.c`, `nx679j-drmtest.c`) — codice del progetto**
  - Uso: client DRM di boot, poi **base della UI** (`nx679j-ui.c` ne riusa l'ossatura: 2 dumb FB ruotanti, commit atomico plane-only `NONBLOCK` + `OUT_FENCE_PTR`, un commit in volo, poll del fence, touch da `/dev/input/eventN`).
  - **Chiarimento**: `kiosk3` non è codice di terzi e non ha licenza esterna; deriva dalle sonde `atom*` del progetto. La UI finale (`ui-1-base.c` … `ui-7-*.c` → `nx679j-ui.c`) e i suoi strumenti (`nx679j-ui-fetch.sh`, `nx679j-ui-gather.sh`, `nx679j-ui-sample.sh`, `drm-probe.c`) sono **opera del progetto**.
- **Font bitmap del kernel — `ui-font8x16.h` (95 glifi)**
  - Uso: disegno testo della UI senza dipendenze grafiche, in dumb buffer DRM.
  - Origine/derivazione: **estratto dal font bitmap del kernel** (`lib/fonts/font_8x16.c`).
  - Licenza: **GPL-2.0** (dichiarata nei documenti del progetto).
- **`libfreetype` + `harfbuzz`** — valutati come alternativa per il testo e **non usati** (scelta il font bitmap). Origine: feed OpenWrt · Licenza: FTL / MIT `(non specificato localmente)`.

---

### 8. Verifica visiva e strumenti di misura

- **OBS Studio + `obs-websocket` v5** — Uso: cattura del video source della webcam puntata sul telefono per giudicare cosa mostra **davvero** il pannello (verifica primaria, pretesa dall'utente). Origine: <https://obsproject.com> · <https://github.com/obsproject/obs-websocket> · Licenza: GPL-2.0.
- **`websockets` (Python, MIT/BSD) + `obs_shot.py`** — Uso: script del progetto che si autentica a `ws://127.0.0.1:4455` e salva lo screenshot del source (`GetInputList` + `SaveSourceScreenshot`). Licenza: WebSocket client library `(non specificato localmente)`.
- **`ffmpeg` (cattura V4L2 diretta)** — Uso: alternativa a OBS per acquisire frame senza dipendere da OBS (`ffmpeg -f v4l2 -input_format mjpeg -video_size 1920x1080 -i /dev/video0 -frames:v 8 …`), conservando il frame **più grande** (il primo esce verde/corrotto e il 4K MJPG è corrotto su questa camera). Origine: <https://ffmpeg.org> · Licenza: LGPL-2.1+ / GPL-2.0+ secondo la build.
- **`vision_analyze` (modello di visione dell'agente)** — Uso: lettura del PNG della webcam (o del dump PPM del frame) per dire "bande", "rosso pieno", "logo", "interfaccia viva" senza disturbare l'utente. Strumento di ambiente Hermes Agent (<https://hermes-agent.nousresearch.com/docs>) · Licenza: (non specificato localmente).
- **Strumenti di misura sul device**: `dmesg`/`/dev/kmsg`, `debugfs` (`dri/0/*`, `clk_summary`, `csd`, `/proc/interrupts`, `state_info`), `crashlog-dump.sh`, blackbox/watcher sul `rawdump` (slot 409/410), `nx679j-ui-watch.sh`/`nx679j-ui-sample.sh` (fence release **e** retire separate). Licenze: kernel/GPL-2.0 e codice di progetto.
- **Manuali e riferimenti consultati per gli strumenti**: `qmicli(1)` (<https://manpages.debian.org/bullseye/libqmi-utils/qmicli.1>, <https://www.freedesktop.org/software/libqmi/man/latest/qmicli.1.html>), documentazione device driver rmnet del kernel (<https://www.kernel.org/doc/html/latest/networking/device_drivers/cellular/qualcomm/rmnet.html>).

---

### 9. Ricerca, forum e thread consultati

- **postmarketOS wiki** e **pmaports** — Uso: stato del supporto mainline SM8450, ordine dei servitori del modem, comportamento `rmtfs`. Origine: <https://wiki.postmarketos.org> · `pmaports` (`modem/tqftpserv`, MR 3269) · Licenza: (non specificato localmente; la wiki è CC-BY-SA a monte).
- **Forum e comunità**: `forum.openwrt.org` (thread 165025, 185556, 188925, 188956 — deattivazione regolare dell'operatore, auto-reconnect LTE/5G, lease DHCP), `ofmodemsandmen.com` (ROOter), `forums.whirlpool.net.au` + `whirlpool.net.au/wiki/router_openwrt`, `community.particle.io` (Tachyon/QCM6490, "the modem takes around 55 seconds"), `paldan.altervista.org` (QMAP/multiple PDN). URL: <https://forum.openwrt.org/t/support-4g-5g-automatic-reconnection-using-modemmanager/165025> · <https://forum.openwrt.org/t/managing-4g-lte-isp-initiated-regular-deactivation/188925> · <https://forum.openwrt.org/t/how-to-auto-reconnect-lte-5g-module/185556> · <https://forum.openwrt.org/t/lte-dhcp-doesnt-renew-ip-after-lease-time/188956> · <https://ofmodemsandmen.com/monitor.html> · <https://forums.whirlpool.net.au/archive/9xwq1573-3> · <https://whirlpool.net.au/wiki/router_openwrt> · <https://community.particle.io/t/tachyon-1-1-43-the-modem-awakens/71005> · <https://paldan.altervista.org/linux-qmap-qmi_wwan-multiple-pdn-setup/> · Licenza: (non specificato localmente).
- **Issue/PR upstream OpenWrt**: `openwrt/openwrt` #8368, #5066; `openwrt/packages` #19794, #23551 (commit `c51a804a63`, hotplug modemmanager e virtuali), commit `bc754f31` (dispatcher `10-report-down`); `dev.openwrt.org` ticket 4108. URL: <https://github.com/openwrt/openwrt/issues/8368> · <https://github.com/openwrt/packages/issues/19794> · <https://github.com/openwrt/packages/commit/bc754f31cfdb004eefa43038f8f0827922107fc6> · Licenza: (non specificato localmente).
- **Issue/MR GitLab freedesktop**: libqmi #62, #113, #133, #122 e MR !382; libqrtr-glib #7; ModemManager MR !1443, !1452. Origine: <https://gitlab.freedesktop.org/mobile-broadband/ModemManager/-/merge_requests/1452> · API usata per ricontrollare gli issue: <https://gitlab.freedesktop.org/api/v4/projects/> · Licenza: (non specificato localmente).
- **Sorgenti upstream consultati per lo scaffolding della pagina Cellular**: `openwrt/luci` (commit `0834d099439e4ba788b4c33c54616d8ee9df5c6e`), `openwrt/rpcd` @ `4062f666ecd17a03a153a40e76af6904d3eb6a78`, `openwrt/packages` (`net/modemmanager`), `openwrt/ubox` (`log/logread.c`, `log/syslog.c` @ `6f78fa49`), pacchetto modello `luci-app-squid`; scheletro copiabile prodotto in `/home/user/rpcd-exec-minimal/`. Licenze: Apache-2.0 (LuCI) / GPL-2.0 (rpcd, ubox) `(non specificato localmente per i singoli file)`.
- **Thread kernel / patch status**: `patchew.org`, `patchwork.kernel.org` (API `/api/1.2/patches/`), `lore.kernel.org`, `lists.freedesktop.org/archives/libqmi-devel/`, `lists.infradead.org/pipermail/lede-commits/`, `elixir.bootlin.com` — Uso: verificare se un fix è **mergiato** o solo proposto (una serie su LWN non significa merged). Origine: <https://patchew.org/search?q=> · <https://patchwork.kernel.org/api/1.2/patches/?q=> · <https://lists.freedesktop.org/archives/libqmi-devel/2017-April/002285.html> · <https://elixir.bootlin.com> · Licenza: (non specificato localmente).
- **Forum XDA e blog/guide di settore** — Uso: categoria di ricerca "forum modder/hacker" nella metodologia di fan-out (XDA, whirlpool, ROOter, Reddit, blog) e riferimenti generici di boot-time su hardware embedded (Gateworks `trac.gateworks.com/wiki/boot_speed`, Toradex, `openwrt.org/docs/techref/preinit_mount`, `openwrt.org/docs/guide-user/network/wan/wwan/ltedongle`, QTI `docs.qualcomm.com/bundle/publicresource/topics/80-70022-10/46-performance-dashboard.html`). **Nessun thread XDA specifico è citato dai documenti locali.** Licenza: (non specificato localmente).
- **Riferimenti normativi e brevetti**: `patents.google.com/patent/US09544758B2` (Apple, "10-15 s time to cellular connectivity" — usato come *snippet*, non riletto); `linux-msm.github.io/mainline-status`; `android.googlesource.com/device/google/redbull` (analisi dell'init Android come oracolo del bring-up del modem). Licenza: (non specificato localmente).
- **Fonti Android/AOSP usate come oracolo**: `device/google/redbull/init.hardware.rc` (boot del modem come azione userspace, `start insmod_sh` in background, `write /dev/ipa 1` per il firmware IPA) — <https://android.googlesource.com/device/google/redbull/+/6d265bdf7f38a1383169013313...> · Licenza: Apache-2.0.
- **Ricerche in altre lingue** (cinese/russo) sul guasto clock DSI — metodologia della skill `chinese-kernel-issue-research` / `russian-kernel-issue-research`; nessun risultato attribuito. Licenza: (non specificato localmente).

---

### 10. Voci del capitolato **non** riscontrate nei documenti locali

Queste voci erano attese dall'elenco ma **non compaiono** in nessun documento, script o skill del progetto: vanno quindi o dichiarate non usate, o aggiunte a mano da chi conosce il contesto esterno.

- **`drmdb.emersion.fr`** — nessuna occorrenza di `drmdb` né di `emersion` in tutta la documentazione del progetto: **non citato localmente** (la mappa delle proprietà DRM è stata ricostruita con `nx679j-drmprops` e con il **brute-force degli ID** `1..400` + `GETPROPERTY`, perché il driver vendor non elenca le proprietà dei plane in `OBJ_GETPROPS`).
- **`msm-fb-refresher`** — nessuna occorrenza in tutta la documentazione del progetto: **non citato localmente**; il "keeper di commit" del pannello è stato scritto da noi nel client DRM (la proprietà CRTC `idle_pc_state=2` è la rete di sicurezza, il commit continuo < 58 ms è il meccanismo).
- **`goodix_ts_berlin`** (nome esatto) — **non citato localmente**; i sorgenti Goodix effettivamente consultati sono `goodix_berlin_{core,i2c,spi}.c` (mainline), `goodix_berlin_driver_v1.0.1` (vendor Nubia) e l'alias `gtx8_i2c` (vedi §6).
- **`xda`** — citato solo come *categoria* di ricerca nella metodologia di fan-out: **nessun thread XDA specifico** con URL.
- **`mkbootimg`** — **non usato come binario**: l'header `ANDROID!` è scritto/letto dal codice Python del progetto secondo l'header AOSP (`system/tools/mkbootimg/include/bootimg/bootimg.h`); `magiskboot` è invece usato per unpack/repack (§2).
