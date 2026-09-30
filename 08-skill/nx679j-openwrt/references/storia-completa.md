# Storia completa del progetto NX679J — dal primo messaggio a oggi

> Fonti: `experiments/NX679J-OPENWRT-REPORT.md` (fasi 0–8, fino al 20/09) e `experiments/20260920-wifi-luci/STATO-ATTUALE.md` (fasi 9–12, 20–22/09). Questo file li condensa mantenendo i fatti verificati.

## 0. L'inizio (16/09/2026, 12:26)

Primo messaggio dell'utente:
> *"@file:nx679j-stock/NX679J_OPENWRT_KERNEL_RE_HANDOFF.md — abbiamo un nuovo progetto, dobbiamo installare nativamente openwrt sullo smartphone collegato USB a questo PC. Ti fo il file handoff per farti sapere cosa abbiamo provato e scoperto finora."*

Il device: Nubia RedMagic 7 **NX679J**, SM8450/Waipio, kernel vendor 5.10.66. Bootloader sbloccato, slot A/B, **EDL non funzionante** su questa unità (PBL → `END_OF_IMAGE`/INVALID_CMD) → la strategia di recovery è: **slot A con Android+Magisk intatto** e verifiche hash di ogni scrittura.

## 1. Fase boot-chain

- `abl_a` = ELF32 → volume UEFI → LZMA → PE32+ (LinuxLoader);
- DTB: `dtb_idx=5` + overlay `dtbo_idx=35`; board-id `0x10008` dall'overlay;
- stato slot nei bit GPT (non in misc);
- `vendor_boot_a == vendor_boot_b` nella baseline + copia d'oro sul PC.

## 2. Fase ramdisk/init

Lezioni pagate con cicli di boot reali:
1. **PID 1 non può morire** → il lavoro va in un figlio (`switch.sh`); PID 1 sorveglia.
2. **`libgcc_s.so.1` mancante** — busybox OpenWrt richiede sia libc sia libgcc_s (trovato con qemu-user in locale).
3. Permessi/symlink corretti (0755, symlink 120777).
4. `grep` su LZ4 non vede i nomi: leggere il cpio PRIMA della compressione.

Risultato: OpenWrt avviato, SSH su `10.0.0.1:22` via gadget USB NCM (`18d1:4ee7`), LuCI HTTP 200.

## 3. Fase userspace

- `/proc`/`/sys` montati DENTRO il chroot;
- `/var` symlink→`/tmp`: creare `lock/run/log/state` all'avvio;
- servizi avviati direttamente (`ubusd`, `rpcd`, `uhttpd`, `dropbear`) — procd non assume PID 1 in questa architettura;
- `reboot` busybox non attraversa il chroot → `echo b > /proc/sysrq-trigger`;
- **`usb0` mai a netifd** (faceva cadere il canale SSH).

## 4. Fase modem: da invisibile ad acceso

Evidenza Android che ha validato la direzione: `qcom_q6v5_pas 4080000.remoteproc-mss: Direct firmware load for modem.b11 failed` → kernel cerca in `/lib/firmware`, fallback userspace via `ueventd` (assente su OpenWrt).

Cause trovate una per una:
| sintomo | causa |
|---|---|
| firmware non caricato | non in `/lib/firmware` |
| mount firmware fallito | `vfat` è un modulo: serve DOPO la catena moduli |
| probe deferito per sempre | driver `smp2p` mancante → senza `qcom_smem_state` il modem deferisce |
| probe mai riprovato | i deferred si riattivano solo con una NUOVA registrazione di driver |
| moduli dati non caricati | `kmodloader` rifiuta in userspace (rc=255, zero messaggi) → **`finit_module`** |
| init bloccato | `ipam` blocca chi lo carica → caricare in un FIGLIO |
| remoteproc rilasciati | `start` durante il probe lo fa fallire (`releasing` a 19.251s) |
| sistema che si riavvia | modem acceso senza interlocutore QMI → reset piattaforma in 1-2 min |

**Restart protettivo** (dal comportamento Android): start → attendere ~86s → stop → 6s → start.

## 5. Fase QMI: dal falso QMUX al vero QRTR

Prima ipotesi (FALSIFICATA): QMUX sui nodi seriali `smdcntl8`/`smd11` — timeout/EBUSY, nessuna risposta. La causa: il RIL Android NON usa i canali seriali ma **socket `AF_QIPCRTR`** (QRTR), osservato con un tracer ptrace sul processo radio (`sendto(..., {sa_family=AF_QIPCRTR, sq_node=0, sq_port=0x4b})`).

Pezzi necessari (in ordine): `msm_sharedmem.ko` + `/dev/uio0`; `rmtfs` (EFS da `modemst1/2`+`fsg`+`fsc`); `tqftpserv` (root `/rfs` con `modem_pr`); `qrtr-smd.ko`; rproc start + restart; `pd-mapper` + IPA.

**Frame DMS ONLINE** (byte catturati Android e riprodotti):
```
00 16 00 2e 00 0c 00 01 01 00 00 10 05 00 00 00 00 00 00
risposta: 02 16 00 2e 00 07 00 02 04 00 00 00 00 00
```
Transizione DMS 5→0 verificata più volte.

## 6. Fase data path

`DMS online → IPA trigger (echo 1 > ipa3) → DPM Open → IPA ingress/egress → WDA Set/Get → rmnet_data0 mux → WDS Start (APN internet.it) → IPv4/route`.

IPv4 dinamico (10.101.x, 10.180.x, 10.140.x...) — mai codificarlo.

**Il problema residuo del report** (primo ping → reboot): nel boot `f206` il kprobe su `ipahal_pkt_status_parse` catturò il ramo WAN con status 32 byte a ZERO, ~75ms dopo il ping; nessuna risposta ICMP. Ipotesi mai dimostrata la causalità. **RISOLTO nelle sessioni successive**: la catena v76 + ModemManager standard hanno portato al ping 0% loss (v84, 22/09).

## 7. Fase v76→v84: gli standard OpenWrt

L'utente ha richiesto: **"usare i componenti di OpenWrt standard, non creare un protocollo nostro"** → proto custom rimosso dal ruolo principale; si usa `proto 'modemmanager'` + ModemManager patchato.

**I 4 fix che hanno risolto il connect standard (22/09):**
1. **libqmi patch 106 "probe mux"**: il vendor lascia mux fantasma EBUSY; la patch fa create+delete di prova per ogni mux. (musl `strerror(-16)`= "No error information" — ecco il mistero.)
2. **Kill della WDS della catena** prima del connect MM (una sola WDS per porta EMBEDDED).
3. **netlink-watch v3** + `S71mm-netlink-watch` nella lista servizi del wrapper (senza S71 MM va in "Timed out waiting for link port").
4. **Flash forte** (il primo v82 "sembrava ok" ma era cache del block device; al reboot c'era la v81!).

**v84 = boot 100% automatico**: chain → kill WDS → inject → MM restart forte (`kill -9` + 10s + start; il primo probe del boot scarta il modem perché `rmnet_ipa0` non esisteva) → ifup → UP in 3s → ping 0%.

Log del boot finale:
```
[269] chain: 1 done-marker(s)
[269] wds-session della catena fermato (pid 11759)
[272] evento rmnet_ipa0 iniettato
[306] OK: iface modem UP (inet 10.96.49.67/29 ... qmapmux2_1)
[307] OK: ping 1.1.1.1
```

**Riconnessione automatica WINDTRE (~4h)**: il dispatcher standard `/usr/lib/ModemManager/connection.d/10-report-down` cerca l'interfaccia UCI con `option device` == `modem.generic.device` (= `qcom-soc`); senza option → nessun match. Fix SOLO config: `uci set network.modem.device='qcom-soc'` + `force_connection='1'`. Testato: disconnect → `hotplug: Reconnecting 'modem' on 'disconnected' event` → nuovo IP.

**Fan-out 10 agenti** (documentato): confermò che è il meccanismo ufficiale (PR openwrt/packages #23590, #24370), che il "link port" esiste solo se l'evento netdev arriva entro 2.5s (`WAIT_LINK_PORT_TIMEOUT_MS=2500` — ecco perché netlink-watch), e che nessuna regola udev pubblica esiste per i mux.

## 8. Fase LuCI/WiFi

- Orologio device al 1970 → TLS rotto → `apk` non scarica. Fix: NTP (`ntpd -nq -p pool.ntp.org`) nel boot script + feed su **HTTP** (busybox wget non completa TLS). 11.202 pacchetti.
- `luci-proto-modemmanager` installato; allineati TUTTI i luci-* a 26.263 (altrimenti `ReferenceError: View is not defined`). Verificato con browser reale.
- LuCI mostra: Protocol ModemManager, Carrier, IPv4, traffico, Cellular Network.

## 9. Fase display (22/09)

Storia: setcrtc#1 OK (ROSSO pieno) ma setcrtc#2 non aggiorna il pannello (cmd-mode); atomic commits tutti EINVAL → trovato il bug: mancava **`SET_CLIENT_CAP(ATOMIC)`** (`drm_atomic_uapi.c:1306`); poi il commit atomic completo c-n-p FUNZIONA e AGGIORNA (ROSSO→VERDE, verificato webcam). Limite: ~3 commit poi crash (clock DSI EBUSY). Dettagli in `references/display-touch.md`.

Scoperta: **sorgenti kernel ufficiali Nubia** `github.com/ztemt/NX679S` (95K file) → display drivers in `re-nubia-disp/src/`.

## 10. Fase persistenza (22/09 notte)

Debug: `/«redacted»`, `/rfs`, rawdump — TUTTI volatili. La dir `/«redacted»` vive nel RAMDISK dell'immagine boot_b. → persistenza = mettere i file nel ramdisk via build. v86 verificata (LUCI+TOOL+UCI persistono), v87 con anche UCI fix → 100%.

## 11. Errori di metodo da tramandare

1. `rc=$?` dopo pipe → 0 anche se il comando è fallito.
2. Un `except` che inghiotte → cpio da 0 byte.
3. Buttare l'errno numerico → nascose `libgcc_s.so.1` per 3 iterazioni.
4. Path sbagliato → diagnosi sbagliata.
5. `kmodloader` rc=255 senza messaggi = segnale, non rumore.
6. Un rawdump con stderr di dd mischiato NON è valido.
7. Un ping dopo la route NON prova il traffico: servono risposte ICMP osservate.
8. Il journal ruota: verificare il risultato funzionale, non il log.
