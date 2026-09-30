# NX679J OpenWrt — HANDOFF Wi‑Fi + LuCI

**Data:** 2026-09-20  
**Target:** Nubia RedMagic 7 NX679J, Qualcomm SM8450/Waipio, kernel vendor 5.10.66  
**Root del progetto:** `/home/user/nx679j-stock`  
**Obiettivo di questa fase:** rendere utilizzabili e persistenti il Wi‑Fi locale e l'interfaccia LuCI senza regredire il boot OpenWrt, il gadget USB o Internet cellulare.

## Prompt per la prossima sessione

```text
Stai lavorando sul Nubia RedMagic 7 NX679J (SM8450/Waipio) con OpenWrt nativo
su slot B e kernel vendor 5.10.66. Il repository di lavoro è:

  /home/user/nx679j-stock

Leggi prima questi documenti:

  experiments/WIFI-LUCI-HANDOFF-20260920.md
  experiments/MODEM-HANDOFF-20260919-2.md
  experiments/OPENWRT-PORT-STATUS.md
  experiments/NX679J-OPENWRT-REPORT.md

## Stato già verificato — non ripeterlo inutilmente

1. Il boot OpenWrt nativo, SSH via gadget USB NCM e rootfs OpenWrt sono
   funzionanti. La rete di manutenzione è:

     telefono usb0 = 10.0.0.1/24
     host           = 10.0.0.2/24

2. LuCI è presente nel rootfs e in una precedente verifica ha risposto con
   HTTP 200. Non assumere però che ubusd, rpcd, uhttpd e dropbear siano
   avviati automaticamente dopo ogni reboot: la persistenza dei servizi è
   parte del lavoro da verificare e correggere.

3. Il modem X65 con WindTre è RISOLTO nel boot di riferimento
   ca60b6b2-9f6d-4a73-bed2-fe3b1b15fe7f:

     rmnet_data0 con IPv4 assegnato
     default route via gateway cellulare
     ping -c 3 -W 2 8.8.8.8 -> 3/3, 0% packet loss

   La catena funzionante usa ingress AGG_DATA flags=0x2e, agg_size=8192,
   agg_count=0, egress 0xa, WDA raw-IP/QMAP5, mux1 e WDS APN internet.it.
   La procedura è riproducibile con gli script aggiornati e il resolver MSS
   dinamico per nome. Non usare gli script legacy che hardcodano remoteproc3:
   su ca60 MSS era remoteproc2 e ADSP doveva restare offline.

4. Il percorso USB di manutenzione è fragile: avviare alla cieca
   `/etc/init.d/network` o lasciare che netifd prenda possesso di usb0 può
   abbattere SSH. Prima di qualunque modifica di rete creare un rollback
   userspace e mantenere usb0 fuori dal controllo di netifd finché non è
   dimostrato il contrario.

## Regole operative obbligatorie

- Un esperimento per volta; separare FATTO, IPOTESI e RISULTATO.
- Prima di scrivere su boot/vendor_boot/rootfs o partizioni, fare backup,
  hash e readback. Non flashare per tentativi.
- Non modificare il modem, l'IPA, gli script AGG_DATA, il DMS/WDS o le
  configurazioni già verificate salvo una regressione riprodotta.
- Non fare `rmmod` a caldo di moduli vendor Wi-Fi/rmnet senza una prova
  specifica e una via di recupero.
- Non sostituire il kernel vendor né copiare alla cieca driver da un altro
  telefono/SoC. Prima identificare compatibili DT, moduli, firmware e
  versione API.
- Non usare Internet cellulare come unica rete di manutenzione durante il
  bring-up: preservare USB NCM e verificare sempre SSH prima di applicare
  una configurazione persistente.
- Non chiamare “Wi‑Fi funzionante” finché non sono verificati almeno:
  interfaccia radio presente, scan, associazione o AP, traffico bidirezionale
  e sopravvivenza a un riavvio.

## Fase 0 — baseline e inventario, senza modifiche distruttive

Sul boot OpenWrt attivo raccogliere e archiviare:

  cat /proc/cmdline
  uname -a
  cat /proc/version
  dmesg
  find /sys/class/net -maxdepth 1 -type l -printf '%f\n'
  find /sys/class/ieee80211 -maxdepth 2 -type f -print
  find /sys/bus/platform/devices -maxdepth 2 -type l -iname '*wlan*' -o -iname '*wifi*' -o -iname '*cnss*' -o -iname '*icnss*'
  ls -l /lib/modules/$(uname -r)
  grep -E 'wifi|wlan|ath|cnss|icnss|qmi|mhi|firmware' /proc/modules /proc/kallsyms 2>/dev/null
  dmesg | grep -Ei 'wifi|wlan|ath|cnss|icnss|qmi|mhi|firmware|calibration|board'
  ip -br link
  ubus list
  ps w
  ss -lntup

Correggere i comandi se il BusyBox disponibile non supporta un'opzione:
registrare comunque il comando effettivo e l'errore. Determinare in modo
misurato:

- quale dispositivo Wi‑Fi esiste nel device tree/sysfs;
- se il bus è PCIe, SDIO, platform, MHI o QRTR;
- quale driver vendor è già presente;
- quale firmware/calibration/NVM viene cercato e da quale path;
- se il problema è device assente, driver non caricato, firmware mancante,
  rfkill/regulatory domain, o solo userspace mancante.

Prima della fase successiva salvare un tar/hash di:

  /tmp
  /etc/config
  /lib/firmware
  /lib/modules
  /sys/kernel/debug  (solo se montato e utile)
  dmesg e stato dei remoteproc

## Fase 1 — ricerca locale e confronto stock

Cercare nel repository, senza modificare il target:

- moduli e sorgenti CNSS/ICNSS, ath11k/ath12k, WCN6855/WCN7850 o altro
  identificatore emerso dall'inventario;
- firmware Wi‑Fi, board files, BDF, calibration e regulatory database;
- nodi DT, reserved-memory, interconnect, regulators, clocks, PCIe/SDIO;
- script Android/vendor che inizializzano Wi‑Fi e i relativi servizi;
- pacchetti già inclusi nel rootfs: `iw`, `wpa-supplicant`, `hostapd`,
  `netifd`, `ubus`, `rpcd`, `uhttpd`, `luci`, `luci-mod-network`,
  `luci-mod-status`, `luci-theme-bootstrap`.

Confrontare il boot Android stock come controllo positivo, se necessario
passando da slot A, senza copiare file proprietari nel repository pubblico.
Registrare solo nomi, ABI, path e sequenze osservate. Se Android espone una
scheda Wi‑Fi funzionante, catturare:

  ip link
  iw dev
  logcat -b kernel -d
  getprop | grep -Ei 'wifi|wlan|cnss|icnss'
  ps -A | grep -Ei 'wifi|hostapd|wpa|cnss|vendor'

Non fermare servizi Android vendor durante la prima baseline.

## Fase 2 — bring-up Wi‑Fi minimo

Implementare prima il minimo cambiamento reversibile:

1. Se serve, aggiungere o caricare il solo modulo già compatibile con il
   kernel vendor. Verificare `insmod`/`finit_module`, simboli, taint e dmesg.
2. Rendere disponibili solo i firmware necessari nel path realmente cercato
   dal driver. Verificare dimensione, permessi e messaggi di firmware request.
3. Controllare rfkill, regulatory domain e presenza di `phy`/`wlanN` con `iw`.
4. Eseguire uno scan passivo o attivare un AP di test, ma non avviare
   contemporaneamente setup modem sperimentali.
5. Verificare traffico locale con un host separato prima di configurare NAT,
   firewall o routing cellulare.

Il criterio minimo di questa fase è:

  ip link mostra wlanN
  iw dev mostra phy e capabilities
  scan vede almeno un BSSID oppure hostapd crea un AP
  un client si associa
  ping bidirezionale tra client e telefono riesce

## Fase 3 — integrazione OpenWrt e LuCI

Stabilire prima quale ruolo serve al Wi‑Fi:

- AP per offrire rete ai client;
- client STA per collegarsi a una rete esistente;
- eventualmente AP+STA, solo dopo AP o STA singoli funzionanti.

Creare configurazioni UCI minime e versionate in `/etc/config/wireless`,
`/etc/config/network`, `firewall` e, se necessario, `system`. Non assegnare
usb0 alla LAN wireless finché il controllo manuale è stabile. Per la prima
integrazione preferire una rete Wi‑Fi separata con indirizzo statico,
senza DHCP relay/NAT complessi.

Verificare LuCI in quest'ordine:

1. `uhttpd` ascolta su usb0 e non espone WAN cellulare per errore;
2. `ubusd` e `rpcd` sono vivi;
3. il login LuCI funziona e la password root è impostata;
4. `luci-mod-network` vede la radio e salva/applica la configurazione;
5. `luci-mod-status` mostra interfaccia, associazioni e contatori;
6. un reboot ripristina i servizi e la configurazione senza perdere usb0;
7. il modem cellulare può essere avviato dopo il Wi‑Fi senza che netifd
   distrugga il gadget USB o la route `rmnet_data0`.

LuCI non deve diventare il punto di controllo esclusivo: ogni azione UI deve
essere riproducibile con UCI/ubus e verificabile da SSH.

## Fase 4 — persistenza nel boot reale

Solo dopo il bring-up manuale:

- aggiungere init script/procd o hook nel meccanismo di handoff già usato;
- avviare `ubusd`, `rpcd`, `uhttpd`, `dropbear`, `wpad/hostapd` e netifd
  nell'ordine corretto;
- mantenere il resolver MSS dinamico e il restart modem protettivo;
- introdurre dipendenze e timeout espliciti, con log persistente;
- aggiungere rollback automatico se il Wi‑Fi o LuCI non diventa pronto;
- ricostruire l'immagine solo dopo avere un archivio dei file modificati.

Il servizio di rete deve distinguere sempre:

  usb0          = management/SSH, non da riconfigurare alla cieca
  wlanN         = LAN/AP o STA, ruolo da definire
  rmnet_data0   = uplink cellulare, già verificato nel boot ca60

## Criteri finali di completamento

### Wi‑Fi

- radio e firmware identificati e documentati;
- `wlanN` presente dopo reboot;
- scan o AP funzionante;
- client associato;
- traffico bidirezionale verificato;
- configurazione sopravvive al reboot;
- nessuna regressione a usb0, SSH o modem cellulare.

### LuCI

- `uhttpd`, `ubusd` e `rpcd` partono automaticamente;
- login e accesso HTTP/HTTPS funzionano su USB;
- la pagina Network mostra la radio Wi‑Fi e le interfacce corrette;
- la configurazione Wi‑Fi può essere salvata/applicata da LuCI;
- dopo reboot LuCI e SSH restano raggiungibili;
- l'interfaccia non espone accidentalmente il pannello sulla WAN cellulare.

### Regressione modem

In un boot finale separato, non necessariamente durante ogni debug Wi‑Fi,
verificare che restino ottenibili:

  rmnet_data0 con IPv4
  default route via gateway cellulare
  ping -c 3 -W 2 8.8.8.8 -> 3 risposte, 0% packet loss

## Output obbligatorio della prossima sessione

Aggiornare questo file solo con fatti misurati, aggiungendo:

- boot ID, slot, kernel e immagine usata;
- inventario Wi‑Fi e path firmware;
- comandi/moduli/configurazioni modificati;
- log e archivi con dimensione e SHA-256;
- risultato scan/AP/STA e test client;
- risultato reboot/persistenza;
- risultato LuCI e verifica non regressiva del modem;
- una sezione esplicita `FATTO`, `IPOTESI`, `NON RISOLTO` o `RISOLTO`.

Non dichiarare “Wi‑Fi pronto” o “LuCI persistente” sulla sola presenza dei
binari: servono prova live, reboot e verifica dal percorso USB.
```

## Stato di riferimento e artefatti

Il risultato modem da considerare autorevole è:

- boot: `ca60b6b2-9f6d-4a73-bed2-fe3b1b15fe7f`;
- archivio: `refs/session3-20260919/openwrt-ca60-ping-agg8192.tar`;
- SHA-256 archivio: `9f251031b900a8b2a8143b621981b0f26490616a396f78dcd93176cf4f7071ac`;
- rawdump finale: `refs/session3-20260919/rawdump-after-ca60-ping-agg8192.bin`;
- SHA-256 rawdump: `312325ede36635c2a7bec81a7231fdf3636984db784946247da6170ad0121de2`.

Riferimenti OpenWrt/LuCI già presenti nel progetto:

- `experiments/openwrt-rootfs/`;
- `experiments/20260917-init-v9/v10-work/newfiles/owrt/`;
- `native-openwrt-usb-build/rootfs/`;
- `port-work/nx679j-boot-assets/initramfs/openwrt_usb_build/rootfs/`;
- `experiments/OPENWRT-PORT-STATUS.md`;
- `experiments/NX679J-OPENWRT-REPORT.md`.

Questi alberi possono rappresentare build diverse: prima di scegliere una
baseline confrontare versioni, pacchetti, hash e contenuto effettivamente
incluso nell'immagine usata sul telefono.

## Nota di sicurezza operativa

Firmware, moduli vendor, EFS, rawdump completi e partizioni estratte restano
artefatti locali del dispositivo e non devono essere redistribuiti. Il lavoro
deve restare orientato all'interoperabilità; il codice nuovo deve dipendere
da ABI/UAPI documentate e da osservazioni riproducibili, non da copia di
implementazioni proprietarie.

---

# SESSIONE 2026-09-20 — ESITO (Wi-Fi + LuCI + regressione modem)

## RISOLTO

**Wi‑Fi e LuCI sono funzionanti e persistenti al reboot; il modem X65 è
ri-verificato sullo stesso boot finale.**

- **Immagine finale**: `experiments/20260920-wifi-luci/boot_b-v57-wifi.img`
  - dimensione 100663296 B (= boot_b della partizione sde41, major:minor
    259:25 in questo boot);
  - **sha256 `dd516b30e1dc1ce278c94d76149d34db489dec41102f36c45baf0a42f29f3d1a`**,
    md5 `3e7ee9d89491af8d5abc3c25efa2aea0`;
  - patch kernel/header come v54–v56 (kernel stock invariato, 49115136 B;
    ramdisk lz4 24775468 B = v9.cpio + new-uid0.cpio + overlay v57;
    overlay = albero rootfs live catturato con `tar -czf` + wrapper + switch.sh).
  - build: `experiments/20260920-wifi-luci/build-v57.py` (overlay di SOLO i file
    modificati; le basi v9/new-uid0 restano invariate).
- **Boot di validazione finale**: boot_id `b127b878-631e-4b6a-8f38-7be3678e0ae8`,
  slot B, kernel vendor 5.10.66, ssid AP `NX679J-TEST`, rete `192.168.77.1/24`.

### Wi‑Fi: FATTI misurati (boot finale, tutto automatico)

Dal journal (`/nx679j-journal`, su boot v57):

```
switch.sh: v57 wifi: <8 moduli cnss*> OK
switch.sh: v57 wifi: fs_ready=1
switch.sh: v57 wifi: qca_cld3 OK
nx679j-wifi: driver dopo 8s (phy0=si wlan0=si)
nx679j-wifi: hostapd ubus dopo 0s
nx679j-wifi: network.wireless ubus dopo 2s
nx679j-wifi: uhttpd (usb0+ap)
nx679j-wifi: ap0 state=up ip=192.168.77.1 (tentativi=1)
nx679j-wifi: proc dnsmasq=ok hostapd=ok uhttpd=ok netifd=ok
```

- Radio: QCA6490 (WCN6855-family) su PCIe0; driver `cnss2` + `qca_cld3_qca6490`;
  firmware `qca6490/{amss20.bin,bdwlan.elf,bdwlang.elf,regdb.bin,m3.bin}` in
  `/lib/firmware` (= bind rw di sde6:/image → persistenti).
- AP: `phy0-ap0` type AP, canale 6 (2437 MHz), WPA2-PSK, client PC associato
  (`iw dev phy0-ap0 station dump` → station presente), DHCP (lease
  `192.168.77.168 user-Mini`), ping bidirezionale 3/3 0% loss.
- Persistenza: verificata su **quattro** boot consecutivi (v54 → v55 → v56 →
  v57); in v56/v57 la sequenza è interamente automatica al boot.

### LuCI: FATTI misurati (boot finale)

- `uhttpd` ascolta **solo** su `10.0.0.1:80/443` e `192.168.77.1:80/443`
  (netstat) → **il pannello non è in ascolto sulla WAN cellulare**.
- Login: POST `/cgi-bin/luci/` → 302 + cookie `sysauth`; pagina status → 200.
- Password root = [REDACTED] (impostata in sessione; variabile d'ambiente del
  tester, non nel repository).
- `luci-rpc getNetworkDevices` → `radio0` up; `network.interface dump` →
  interfaccia `modem` presente (`up:true`, `autostart:true`), device
  `rmnet_data0`/`rmnet_ipa0`/`usb0`/`phy0-ap0` visibili.
- ubusd, rpcd, uhttpd, dropbear (via switch.sh), dnsmasq, netifd, wpad tutti
  attivi dopo il reboot; usb0 (10.0.0.1) e SSH intatti.

### Regressione modem: RISOLTA sul boot finale v57

Catena riprodotta con gli strumenti di sessione3 (`refs/session3-20260919/`
x-tools) su boot `b127b878`:

```
bootstrap (rmtfs/tqftpserv/pd-mapper/crashlog/qrtr-smd + MSS start+restart
protettivo 86s) → READY_FOR_OBSERVED_DMS_TEST
DMS 5→0 (observed client, preflight u8=5 → fresh u8=0)
trigger IPA + endpoint + DPM open (pid stabile)
INGRESS selector=7 flags=0x2e → INGRESS accepted      ← agg8192: ORDINE CRITICO
EGRESS selector=6 flags=0xa → accepted
WDA raw-IP=2 UL/DL-QMAP=5; mux1 rmnet_data0; WDS APN internet.it
rmnet_data0 10.97.235.69/29, default via 10.97.235.70
ping -c 3 -W 2 8.8.8.8 → 3/3, 0% loss, RTT 39–70 ms → CELLULAR_CRITERIA_PASS
```

- **MSS risolto per NOME** (`4080000.remoteproc-mss`): in questi boot è
  `remoteproc3`; la versione staged nei tools ha un path hardcoded — è stata
  patchata a runtime (`sed` sul path dell'MSS) prima dell'esecuzione.
- **Lezione ordine IPA (nuova)**: `rmnet-config * pipes` (ingress 0xe)
  NON deve precedere l'agg8192 (0x2e): il driver risponde
  `EP 23 already allocated` → `handle3_ingress_format: failed to configure
  ingress` (EINVAL, kernel log). L'ingress agg si configura PRIMA di
  pipes/egress; i tools ufficiali del flow ingress non includono pipes.
- Artefatti: `refs/session4-20260920/session4-logs.tar.gz` (1633439 B,
  sha256 `f0cae140f8329c9cf9fd915aca596ae29c39a12d6ef6d6bc2128e8bfe963a6b2`),
  `rawdump-session4-window.bin.gz` (626035 B, sha256
  `435297c86f6f83b5bed48b989506e5622f5324904ee348b14df1b902afd7fe12`).

## FATTO — bug reali trovati e corretti nell'immagine

1. **PATH mancante negli init script**: da switch.sh i servizi partivano con
   `readlink/flock/ubus/uci: not found` (stderr in journal). Fix: wrapper
   `/etc/nx679j-wifi-services.sh` eseguito DENTRO il chroot con
   `PATH=/usr/sbin:/usr/bin:/sbin:/bin` e `cd /`.
2. **ujail/procd fallisce su questo kernel vendor**:
   `jail: failed to clone/fork: Invalid argument` (dnsmasq 100%, wpad a
   intermittenza; hostapd a mano senza jail: ok). Fix: `/sbin/ujail` rinominato
   in `/sbin/ujail.off` nell'immagine (procd spawna unjailed). Motivato e
   reversibile; da rivalutare se si vuole indagare il flag esatto.
3. **Pacchetti userspace assenti** dall'immagine v54: iw/wpad/wifi-scripts/
   iwinfo/wireless-regdb/ucode-mod-* installati via `apk` a runtime non erano
   nella ramdisk. Fix: albero live completo catturato dentro v55+ (pacchetti in
   `/usr` compresi).
4. **uhttpd esposto su 0.0.0.0** (pannello raggiungibile via IP cellulare).
   Fix: listen solo `10.0.0.1` + `192.168.77.1`; avvio condizionato alla
   presenza degli IP (fallback solo-usb0 se l'AP non sale).

## NON RISOLTO — anomalia flashing v55 (tracciata dopo)

Il flash di v55 (dd su `/proc/1/root/dev/sde41` = 259:25, readback md5
identico) **non è risultato persistente al reboot successivo**: boot_b
conteneva ancora v54 (verificato leggendo la partizione). La scrittura non è
stata trovata su nessun device dello scan completo (sda/sde/sdf + rawdump).
Causa non determinata (sospetti: nodo /dev non ricreato dal boot, cache).
**Mitigazione adottata (protocollo hardened, usato per v56/v57 e riuscito)**:
1. nodo creato da `/sys/class/block/sde41/dev` nel boot corrente;
2. pre-lettura della partizione con confronto md5 contro l'immagine attesa
   (prova che il nodo punta davvero a boot_b);
3. dd + `sync`×3 + `echo 3 > /proc/sys/vm/drop_caches` + readback md5
   (rilettura fisica, non cache);
4. validazione post-reboot del contenuto (journal `v5N wifi` + hash script).

## Tooling e riproducibilità (tutto nel repo)

- `experiments/20260920-wifi-luci/nxssh.sh` — SSH con chiave `nx679j_key`
  (la host key dropbear si rigenera a ogni boot; UserKnownHostsFile=/dev/null).
- `.../v57-wifi-services.sh` — wrapper servizi+wifi (dentro l'immagine come
  `/etc/nx679j-wifi-services.sh`).
- `.../v57-wifi-block.sh` + `.../build-v57.py` — blocco switch.sh e build
  overlay cpio (pattern riusabile per ogni futura immagine).
- `.../nx679j-modem-prepare.sh` — step pre-ingress (trigger+endpoint+DPM).
- Catena modem: `refs/session3-20260919/x-tools/` (bootstrap, dms-check,
  ingress-resume, data-staged, cellular-staged) + `/rfs` lot
  (`refs/modem_pr/`). `/tmp` e `/rfs` sono tmpfs: a ogni boot vanno
  ri-pushate (≈2–3 min) — NON incluse nell'immagine per scelta (risparmio
  ramdisk; valutare v58 se serve il modem auto-start).

## IPOTESI / prossimi passi

- `IPOTESI`: il "pilotaggio" pieno del modem da LuCI richiede un proto handler
  (`/lib/netifd/proto/nx679j_modem.sh`) che chiami gli script già esistenti e
  riporti stato/IP da `wds-session.log`; oggi l'interfaccia `modem` è
  visibile (proto none) con device e contatori, ma il bring-up resta
  script/SSH.
- `IPOTESI`: il fallimento ujail dipende da un flag namespaces specifico per
  servizio; indagabile con strace di ujail se si vuole riabilitare la jail.
- `IPOTESI`: dropbear ascolta su 0.0.0.0:22 (auth solo a chiave). Restringibile
  a 10.0.0.1/192.168.77.1 modificando lo start v22 di switch.sh (tocca il
  percorso di manutenzione: da fare solo con recovery pronta).
