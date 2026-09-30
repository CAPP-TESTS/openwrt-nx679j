# STATO ATTUALE — ModemManager standard su NX679J — LEGGERE PRIMA DI OGNI PASSO

## ⚡⚡ AGGIORNAMENTO FINALE (22/09 ~17:00 CEST) — PROBLEMA RISOLTO

> **§5–§7 qui sotto sono PRE-soluzione: superate.** Lo stato corrente è questo blocco.

**Il flusso standard funziona end-to-end, verificato sul device:**
`ifdown modem && ifup modem` → **UP in ~3s**, `ping 1.1.1.1` **0% loss**, DNS ok. Anche lo script di boot `/usr/lib/nx679j/modem/nx679j-mm-standard-boot.sh` → "OK: iface modem UP" + "OK: ping".

**I 4 fix che hanno risolto (in ordine di importanza):**
1. **libqmi patch 106 — "probe mux"**: il driver vendor lascia i mux FANTASMA occupati (EBUSY) dopo tentativi falliti; `get_first_free_mux_id` vedeva solo i nomi dei netdev. Ora fa un create+delete di prova per ogni mux candidato. Log finale: `Using dynamic mux ID 2` → `net link qmapmux12_1 created`. NB: **musl `strerror(-16)` = "No error information"** → EBUSY mascherato (era QUESTO il mistero!). Il messaggio netlink di libqmi era già perfetto (prova: rispedito byte-identico da `nl-send` → ACK -16 dal kernel).
2. **Kill della WDS della catena** prima del connect MM (una sola WDS per porta EMBEDDED): la catena la teneva → `call-already-present`. Lo script di boot lo fa da solo (poll chain → kill wds-session → inject → ifup).
3. **netlink-watch v3** (313608 B; annuncia i netdev runtime a MM+netifd, niente marker) **+ `S71mm-netlink-watch` nella lista esplicita di servizi del wrapper `nx679j-boot-services.sh`** — senza S71 il watcher non parte al boot e MM va in `Timed out waiting for link port`.
4. **Flash forte**: il primo flash del v82 "sembrava ok" (readback c51278ea) ma era **cache del block device**: al reboot c'era ancora il v81! Protocollo obbligatorio:
```
N=/dev/sde41; [ -b $N ] || mknod $N b 259 25
dd if=<img> of=$N bs=4M oflag=direct
sync -f $N; sync
dd if=$N of=/dev/null bs=4M          # svuota cache
md5sum $N; sleep 3; md5sum $N       # DUE letture fredde = attese
# SOLO POI: echo b > /proc/sysrq-trigger
```

**Stato immagini: v84 flashata e VERIFICATA — BOOT 100% AUTOMATICO FUNZIONANTE!** (`boot_b-v84-mm-std.img`, md5 `5dc9fc4d2c171e43`, sha256 `7c486622…`). La v84 aggiunge allo script di boot il **"MM restart forte"**: dopo l'inject, MM va riavviato con `kill -9` + 10s + `start` (al primissimo probe del boot il modem viene scartato perché `rmnet_ipa0` non esisteva ancora; un semplice `restart` è troppo rapido e il port QRTR non si libera). Log del boot finale (tutto automatico):
```
[269] chain: 1 done-marker(s)
[269] wds-session della catena fermato (pid 11759)
[272] evento rmnet_ipa0 iniettato
[303] ifup modem...
[306] OK: iface modem UP (inet 10.96.49.67/29 ... qmapmux2_1)
[307] OK: ping 1.1.1.1
```
`ubus ... status` → `"up": true`; ping 0% loss. **OBIETTIVO CENTRATO.**
Nota cosmetica: nel blocco aggiunto avevo scritto `log` mentre lo script definisce `say` → le 2 righe di log di quel blocco non appaiono (il flusso funziona lo stesso); fix cosmetico già applicato al sorgente PC (per un'eventuale v85, non urgente).

**Fix LuCI + sistema (22/09):** (1) **L'orologio del device era fermo al 1970** → rompeva TLS (SSL error) e di conseguenza `apk` non scaricava nulla; ora sincronizzato via NTP (`ntpd -nq -p pool.ntp.org`) e la sincronizzazione è stata **aggiunta allo script di boot** `nx679j-mm-standard-boot.sh` (dopo l'ifup). (2) **Feed apk su HTTP**: il wget busybox di questa build non completa il TLS verso downloads.openwrt.org, quindi `distfeeds.list` è stato cambiato da `https://` a `http://` (i pacchetti restano firmati/verificati) → **11.202 pacchetti disponibili**. (3) **Installato `luci-proto-modemmanager`** → LuCI ora mostra i dettagli dello stato dell'interfaccia `modem` (non più "Unsupported protocol type") e aggiunge la pagina **Status → Cellular Network**. File installati: `/www/luci-static/resources/protocol/modemmanager.js`, `view/modemmanager/status.js`, `modemmanager_helper.js` + menu `admin/status/modemmanager`. (4) **Allineamento versioni LuCI (22/09)**: il pacchetto nuovo (26.263.44884) su base vecchia (26.180.75667~128a781) produceva il banner `ReferenceError: View is not defined`; tutti i 15 pacchetti luci-* sono stati aggiornati alla 26.263.44884~0834d09 (upgrade mirato, backup in /tmp/bk-luci sul device). Verificato con browser reale (opencli profile pdc8925e): Interfaces mostra `Protocol: ModemManager` + Carrier + IPv4 + traffico, Cellular Network mostra modem/registrazione/SIM, **0 errori**. Per reinstallare dopo un re-flash: `apk` richiede NTP attivo (il device parte al 1970!) e i feed su HTTP (TLS busybox rotto).

**Riconnessione automatica (22/09 sera) — la "particolarità WINDTRE" delle ~4h è RISOLTA:** WINDTRE (SIM consumer) deattiva periodicamente il bearer e cambia IP (`regular-deactivation`); prima la connessione cadeva e NON si ripristinava (netifd con stato stantio `up`; in LuCI `Carrier: Absent`). **Causa precisa**: il dispatcher STANDARD OpenWrt `/usr/lib/ModemManager/connection.d/10-report-down` sul 'disconnected' cerca l'interfaccia UCI la cui `option device` è ESATTAMENTE `modem.generic.device` (= `qcom-soc` per il nostro modem QRTR); la nostra config era SENZA option device → nessun match → uscita senza riconnettere (mai visto "Reconnecting" nei log). **Fix (solo config, zero codice custom — il meccanismo standard è bastato)**: `uci set network.modem.device='qcom-soc'` + `uci set network.modem.force_connection='1'` (retry netifd sui fallimenti di connect; default del proto sarebbe `proto_block_restart`). **Test superato dal vivo**: `mmcli -m any --simple-disconnect` → log `hotplug: Reconnecting 'modem' on 'disconnected' event` → nuovo IP in pochi secondi → ping OK. Ricerca: 2 agenti hanno confermato che è il meccanismo ufficiale (PR openwrt/packages #23590 + #24370; in 24.10/25.12) e che i casi identici (Vodafone UK 48h, WINDTRE 4h) usano proprio questo. Nota: `disable_modem=0` (nostra config) rende il teardown più rapido del default.

**Conferme indipendenti (fan-out 10 agenti, 22/09 — la ricerca fatta PRIMA del debug finale):** la ricerca estesa (ModemManager upstream, OpenWrt feed, postmarketOS, meizu-m2172 fork, code-search) ha confermato: (a) un "link port" in MM esiste SOLO se l'evento del netdev arriva a MM entro 2.5s (`WAIT_LINK_PORT_TIMEOUT_MS=2500`) — il nostro `netlink-watch` è esattamente l'iniezione di evento mancante su questo kernel vendor; (b) nessuna regola udev/ID_MM_PHYSDEV_UID pubblica esiste per i mux (nessun precedente da copiare — il nostro approccio è nuovo); (c) il fix OpenWrt #23551 (whitelist qmapmux* in modemmanager.common) è GIÀ presente nella nostra 25.12, quindi lo strato shell era a posto; (d) MM 1.24 non ha fix merged per l'aggancio del port post-creazione (solo MR aperte) → le nostre patch restano la strada. Summary completi dei 10 agenti: `~/.hermes/profiles/kernel-re/cache/delegation/subagent-summary-*-20260922_130525_*.txt`.

**Prossimi passi (opzionali):** 1) 2-3 reboot di conferma stabilità; 2) v85 solo cosmetica (log→say); 3) cleanup esperimenti in /tmp del device.

---

> **Questo file è la fonte unica di verità** del lavoro "connect dati tramite il componente
> standard OpenWrt `proto modemmanager` su Nubia RedMagic 7 NX679J".
>
> Regole:
> 1. Prima di qualsiasi nuovo tentativo, rileggere le sezioni 5, 6 e 7.
> 2. **Vietato ripetere un tentativo della sezione 6 senza un dato nuovo** (nuova misura,
>    nuovo log, nuova ipotesi falsificabile). Se lo si fa, va scritto qui perché.
> 3. Ogni voce va accompagnata da **comando + output reale** (o file+riga). Non si scrivono
>    conclusioni senza evidenza: se un'inferenza è incerta si scrive "DA CONFERMARE".
> 4. Ogni tentativo nuovo va **aggiunto** alla sezione 6 con esito ed evidenza.
> 5. Sul device si lavora **solo read-only** finché non si decide un test (niente scritture
>    non pianificate, niente ifup/ifdown a raffica: vedi §6 punto 7 e 9).
>
> Ultima verifica completa sul device: **2026-09-22, host 15:47 → 16:22 CEST** (sessione
> read-only via SSH). Stato del device riletto riga per riga, non copiato da sessioni vecchie.

---

## 1. Obiettivo

Far funzionare il **connect dati** su NX679J (SoC SM8450, kernel vendor 5.10.66,
OpenWrt 25.12.5 armsr/armv8 che gira in chroot/overlay sul vendor) usando **solo componenti
standard OpenWrt**:

* interfaccia UCI `network.modem` con `proto 'modemmanager'` (proto stock, patchato solo dove
  necessario e documentato in §3);
* `ifup modem` → ModemManager (ricompilato dal feed SDK) → bearer QMI → netdev RMNET creato da
  MM/libqmi → netifd assegna IP/rotta.

**Criterio di successo**: `ifup modem` porta l'interfaccia `modem` in stato `up` con IP del
bearer, `ping 8.8.8.8` senza perdita, **senza** usare il proto custom della catena v76
(`S95nx679j-modem`/`chain.sh`), che resta la rete di sicurezza e **non va toccata**.

---

## 2. Stato attuale verificato

### 2.1 Contesto di verifica

| Voce | Valore | Comando / fonte |
|---|---|---|
| Data/ora host (verifica) | 2026-09-22 15:47:42 → 16:22 CEST | `date` |
| Data/ora device | `Tue Jan  6 16:12:35 UTC 1970` → `Tue Jan  6 16:20:46 UTC 1970` | `date` sul device |
| RTC device | assente/1970 → **i timestamp dei file sul device non sono confrontabili con l'host**, l'ora del device è avanti di ~24m53s rispetto all'host | `date` |
| Uptime kernel (device) | 981.61 s → 1401.10 s durante la finestra di verifica | `cat /proc/uptime` |
| Kernel | `Linux (none) 5.10.66-android12-9-00005-gf6e6376090be-ab8060604 #1 SMP PREEMPT Fri Jan 7 14:51:36 UTC 2022 aarch64` | `uname -a` |
| OpenWrt | `OpenWrt 25.12.5 r33051-f5dae5ece4`, `DISTRIB_TARGET='armsr/armv8'`, `DISTRIB_ARCH='aarch64_generic'` | `cat /etc/openwrt_release` |
| Accesso | SSH diretto **dentro** OpenWrt: **non esiste `/owrt`**, i path del rootfs OpenWrt sono alla radice (`/usr/sbin/ModemManager`, `/lib/netifd/proto/...`) | `ls /owrt` → *No such file or directory* |
| Indirizzi del device | `usb0 10.0.0.1/24` (SSH), `phy0-ap0 192.168.77.1/24`, `rmnet_data0 10.181.52.50/30` | `ip -4 addr show` |

> ⚠️ **ATTIVITÀ CONCORRENTE OSSERVATA DURANTE LA VERIFICA** (importante, non è un errore di
> lettura): fra la prima e la seconda lettura i log del device sono cambiati — `/var/log/mm.log`
> è passato da **58.529 a 60.441 righe**, sono comparse le connessioni **attempt #2 e #3**, sono
> comparsi i **marker link** `qmapmux0_0`/`qmapmux1_0`, è scomparso `qmapmux9_9` e MM è stato
> **riavviato** (wrapper PID 21091 → 27020). Cioè: mentre questo file veniva scritto qualcun
> altro stava testando sul device. **Rileggere §2.4 prima di agire**: lo stato può essere
> cambiato di nuovo.

### 2.2 Componenti sul device — md5 verificati (2026-09-22)

Comando usato: `md5sum <file>` via SSH.

| File sul device | md5 | Atteso | Esito |
|---|---|---|---|
| `/usr/sbin/ModemManager` | `23121a96aad86bb73ee61e098cc08b38` | `23121a96aad86bb7…` | ✅ coincide (MM 1.24.0 con le 8 patch) |
| `/usr/lib/libqmi-glib.so.5.11.0` | `d7488a944be2ede0b5667296e1a3858e` | `d7488a944be2ede0…` (patch 100–105) | ✅ coincide |
| `/lib/netifd/proto/modemmanager.sh` | `b19c91fa27471908bd971ddb3a0f8879` | — | proto stock + 2 modifiche locali (§3.1) |
| `/usr/lib/nx679j/modem/netlink-watch` | `d469dd47c8eb750a8c23f0a78ad5fbc4` (325.208 B) | — | = build di `/tmp/netlink-watch2.c` (v2 con marker) |
| `/etc/init.d/mm-netlink-watch` | `198bafe58cc78879b0ed0452eb92f187` | — | init procd, `START=71`, enabled |
| `/usr/lib/nx679j/modem/rmnet-link` | `9b769ebca1308c793ed80bc95d6a9f6a` | — | tool che crea i link rmnet **funzionante** |
| `/usr/lib/nx679j/modem/chain.sh` | `f4bd2cbac3c14c2957e9847737b160cb` | — | catena v76 (NON toccare) |
| `/etc/init.d/nx679j-modem` | `735d02dcbbd443208083e96e86457296` | — | avvio catena a `START=95`, enabled |
| `/lib/udev/rules.d/80-mm-nx679j.rules` | `fbfdcf61f9e463919295e040c4b46ce2` | — | 3 righe, §3.1 |
| `/etc/hotplug.d/net/10-nx679j-modem` | `94aabdf7a5f4c8b28f3b0f3ffcc5a556` (521 B) | — | §3.1 |
| `/etc/init.d/modemmanager` | `1408 B` (md5 non calcolato) | — | stock OpenWrt, `START=70`, `LOG_LEVEL=DEBUG`, `--log-file /var/log/mm.log` |
| `/usr/bin/mmcli` | presente (`/usr/bin/mmcli`, `mmcli 1.24.0`) | — | ⚠️ `find / -name "mmcli*"` può non trovarlo (busybox find); verificare con `which mmcli` |

Versioni: `ModemManager 1.24.0`, `mmcli 1.24.0`.

> Nota per chi cerca i file a mano: sul device `ip` **è busybox** (`/sbin/ip -> ../bin/busybox`,
> `ip -V` = `BusyBox v1.37.0`), **iproute2 non è installato** (`apk info | grep iproute` → vuoto).
> Questo spiega perché `ip link add … type rmnet …` non può funzionare (busybox non sa parlare
> col kind `rmnet`) e perché serve il tool `rmnet-link`.

### 2.3 Configurazione UCI verificata

```
$ uci show network.modem
network.modem=interface
network.modem.proto='modemmanager'
network.modem.auto='0'
network.modem.apn='internet.it'
network.modem.iptype='ipv4'
network.modem.disable_modem='0'
```

* **nessuna** `option device` (voluto: §6 punto 6);
* `auto='0'` → l'interfaccia non parte da sola al boot; il retrigger è affidato al hotplug
  `10-nx679j-modem` (§3.1);
* `disable_modem='0'` → a `ifdown` MM **non** viene disabilitato: nel teardown si legge
  `Skipping modem disable` (proto righe ~840-900).

### 2.4 Stato runtime del device (ultima lettura: device 16:20:46, uptime 1401 s)

**Processi rilevanti** (`ps w`):

```
10629 /usr/bin/dbus-daemon --system --nofork
13496 /usr/lib/nx679j/modem/netlink-watch          <-- vivo
12830 /tmp/qmi-qrtr-observed dpm-session 4 1 2 23 3600   <-- catena v76
12965 /tmp/qmi-qrtr-observed wds-session internet.it 4 1 1 3600
27020 {ModemManager-wr} /bin/sh /usr/sbin/ModemManager-wrapper --log-level=DEBUG --debug --log-file /var/log/mm
27032 /usr/sbin/ModemManager --log-level=DEBUG --debug --log-file /var/log/mm.log
27021 /usr/sbin/ModemManager-monitor        (x2: 27021, 27083)
+ ~14 processi ZOMBIE [ModemManager]/[ModemManager-mo]/[qmi-proxy] (10683, 10921, 11059,
  19083, 19124, 19306…21248) = residui dei cicli di restart di MM in questo boot
```

**Interfacce** (`ip link show` → `ip -4 addr show`):

```
19: rmnet_ipa0: <> mtu 1500 qdisc pfifo_fast state DOWN qlen 1000      link/[519]
20: rmnet_data0@rmnet_ipa0: <UP,LOWER_UP,M-DOWN> … state UNKNOWN        inet 10.181.52.50/30
22: qmaptest2@rmnet_ipa0: <,M-DOWN> mtu 1500 qdisc noop state DOWN     link/[519]
23: qmapmux0_0@rmnet_ipa0: <,M-DOWN> … state DOWN
24: qmapmux1_0@rmnet_ipa0: <,M-DOWN> … state DOWN
```

* alle 16:12:35 esistevano `qmapmux9_9` (idx 21) e `qmaptest2` (idx 22); alle 16:19 **`qmapmux9_9`
  non c'è più** e ci sono i marker `qmapmux0_0`, `qmapmux1_0` (nomi compatibili con la lista
  marker di `/tmp/netlink-watch2.c`: `qmapmux%d_0`, i=0..5);
* `rmnet_data0` conserva l'IP della catena v76 → **la catena custom è ancora attiva**;
  `cat /sys/class/net/rmnet_data0/operstate` = `unknown` (normale per rmnet);
* **non si è eseguito `ping`** in questa sessione (fuori dal perimetro read-only autorizzato):
  l'ultima prova di ping è quella del boot, in `/tmp/cell-ping.log` (§4.1).

**Modem visti da ModemManager**:

```
$ mmcli -L
    /org/freedesktop/ModemManager1/Modem/1 [QUALCOMM INCORPORATED] 0      <-- alle 16:12
$ mmcli -L
No modems were found                                                     <-- alle 16:19/16:20
```

`mmcli -m any --output-keyvalue` (16:12) riportava: `plugin qcom-soc`, `device qcom-soc`,
`physdev --` (**vuoto!**), `ports: qrtr0 (qmi), rmnet_ipa0 (net)`, `state registered`,
`access-technologies lte, 5gnr`, `unlock-required sim-pin2`, `revision NX679J_Z69_UN_ZML1S_V311`.

**Remoteproc / MSS**: `remoteproc3` = `running` (gli altri offline) → la catena ha il modem su.

**Log MM** (`/var/log/mm.log`, 60.441 righe, 3 avvii MM nel file: righe 11058, 45823, 60002):

| Evento | Timestamp device | Riga |
|---|---|---|
| avvio MM (run "buona", modem creato) | 000490251.899 (≈16:04:11) | 45823 |
| `modem for device 'qcom-soc' successfully created` | 000490253.904 | 46259 |
| tentativo #1 fallito (mux id 1) | 000490381.286 (≈16:13:01) | 58525 |
| tentativo #2 fallito (mux id 2) | 000490396.244 (≈16:13:16) | 58831 |
| tentativo #3 fallito (mux id 2) | 000490451.367 (≈16:14:11) | 59133 |
| ultimo avvio MM | 000490735.553 (≈16:18:55) | 60002 |
| `couldn't check support for device 'qcom-soc': not supported by any plugin` | 000490747.558 (≈16:19:07) | 60440 |

> I timestamp di mm.log sono **secondi epoch dell'orologio del device** (base 1970), non uptime:
> `000490381` ⇔ `Jan 6 1970 16:13:01`, coerente con la mtime del file `Jan 6 15:58`.

---

## 3. Inventario completo delle modifiche

### 3.1 Sul device (dentro OpenWrt)

**(a) `lib/netifd/proto/modemmanager.sh`** — proto stock, md5 `b19c91fa27471908bd971ddb3a0f8879`.
Verifiche eseguite:

```
$ grep -n 'add_device' /lib/netifd/proto/modemmanager.sh     -> nessun match (rc=1)   OK: rimosso
$ grep -n 'device="any"'  /lib/netifd/proto/modemmanager.sh
597:	[ -n "${device}" ] || device="any"      <-- setup
849:	[ -n "${device}" ] || device="any"      <-- teardown
```

Modifiche locali presenti nel file:
1. in `proto_modemmanager_setup()` e in `proto_modemmanager_teardown()`: default interno
   `device="any"` quando l'option UCI `device` non c'è (commento nel file: *"the netifd 'device'
   option cannot be used as it would make netifd try to claim a device named 'any'"*);
2. **rimozione** della chiamata `add_device` (causava teardown immediato, §6 punto 5).

Flusso effettivo del proto (righe 590-780):
`mmcli --modem=any --output-keyvalue` → `modemmanager_check_pin_state` →
`modemmanager_cleanup_connection` (simple-disconnect + delete-bearer) →
`mmcli --modem=any --timeout 120 --enable` → set EPS bearer / allowed-modes / plmn →
`mmcli --modem=any --timeout 120 --simple-connect="apn=internet.it,ip-type=ipv4"` (riga 734) →
lettura bearer → `proto_notify` con IP/netdev.

**(b) `lib/udev/rules.d/80-mm-nx679j.rules`** (md5 `fbfdcf61f9e463919295e040c4b46ce2`):

```
SUBSYSTEM=="net", KERNEL=="rmnet_ipa0",  ENV{ID_MM_PHYSDEV_UID}="qcom-soc"
SUBSYSTEM=="net", KERNEL=="rmnet_data0", ENV{ID_MM_PORT_IGNORE}="1"
SUBSYSTEM=="net", KERNEL=="qmapmux*",    ENV{ID_MM_PHYSDEV_UID}="qcom-soc"
```

MM le applica da sé (nel log si vede `[rmnet_ipa0] property added: ID_MM_PHYSDEV_UID=qcom-soc`,
`pattern '^qmapmux.*$' matched`): servono a **agganciare i netdev al device `qcom-soc`** senza udevd.

**(c) `etc/hotplug.d/net/10-nx679j-modem`** (md5 `94aabdf7a5f4c8b28f3b0f3ffcc5a556`, 521 B):

```sh
[ "$ACTION" = "add" ] || exit 0
[ "$DEVICENAME" = "rmnet_data0" ] || exit 0
{ sleep 2; ubus call network.interface.modem up >/dev/null 2>&1; … } &
```

→ retrigger: quando la catena crea `rmnet_data0`, fa `ifup modem` da solo. **È la probabile
origine dei tentativi MM automatici** (vedi §5.3 per la sequenza osservata).

**(d) `usr/lib/nx679j/modem/netlink-watch` + `etc/init.d/mm-netlink-watch`**
(md5 `d469dd47c8eb750a8c23f0a78ad5fbc4` / `198bafe58cc78879b0ed0452eb92f187`).
Motivo d'esistenza (dal sorgente `/tmp/netlink-watch2.c`): *"on this image the kernel does not
deliver kobject uevents for runtime-created netdevs (procd hotplug-call is never invoked; netifd
never creates its device object; MM never learns about the new port)"*. Fa due cose:
1. ascolta **rtnetlink** (`RTMGRP_LINK`, `RTM_NEWLINK/RTM_DELLINK`) e per ogni ifname
   `rmnet*|qmapmux*|qmimux*|mbimmux*` esegue
   `mmcli --report-kernel-event="action=add,name=<ifn>,subsystem=net"` e inietta un **uevent
   kobject sintetico** sul gruppo netlink `NETLINK_KOBJECT_UEVENT` (per procd/netifd);
2. **"marker link"** (v2): crea `qmapmux0_0 … qmapmux5_0` con
   `ip link add link rmnet_ipa0 name qmapmux<i>_0 type rmnet mux_id <4+i>` allo startup e appena
   compare `rmnet_ipa0`, per far **scegliere a libqmi un mux ≠ 1**
   (`get_first_free_mux_id()` guarda solo se il *nome* del netdev esiste: §9).
   **Questo passo FALLISCE** su questa immagine (busybox non supporta `type rmnet`) → quindi i
   marker vanno creati col tool `rmnet-link` (§6 punto 8, §7 passo 3).

**(e) Catena custom v76 (NON toccare)**: `etc/init.d/nx679j-modem` (`START=95`) →
`usr/lib/nx679j/modem/chain.sh` (`START` steps, log in `/tmp/chain.log`) → `/tmp/qmi-qrtr-observed`
(bootstrap/MSS, DMS, DPM holder, ingress, data egress/wda/mux/wds) → crea `rmnet_data0`
(mux_id 1) e assegna IP. Toolkit in `usr/lib/nx679j/modem/` con symlink in `/tmp` creati
dall'init (`for f in /usr/lib/nx679j/modem/*; do ln -s …`).

**(f) `etc/init.d/modemmanager`** stock: `USE_PROCD=1`, `START=70`, `LOG_LEVEL=DEBUG`,
comando `/usr/sbin/ModemManager-wrapper --log-level=DEBUG --debug --log-file /var/log/mm.log`
+ istanza `ModemManager-monitor`. `rc.d`: `S70modemmanager`, `S71mm-netlink-watch`,
`S95nx679j-modem`.

### 3.2 Host — SDK (patch e build)

SDK: `/tmp/sdk/openwrt-sdk-25.12.5-armsr-armv8_gcc-14.3.0_musl.Linux-x86_64`

**ModemManager 1.24.0** — oltre alle 4 patch upstream del feed (`0001…0004`), 8 patch locali in
`feeds/packages/net/modemmanager/patches/`:

| Patch | md5 | Cosa fa |
|---|---|---|
| `0100-nx679j-filter-allow-virtual-net.patch` | `10ab5398a1ffa9259d972a10da9e48b4` | consente i netdev virtuali nel filter (rmnet/qmapmux) |
| `0101-nx679j-qrtr-skip-dataformat.patch` | `27fe21df8bc18a2c409436dd94e9dbd1` | niente WDA set-data-format sul porto QMI qrtr quando il data port è gestito a mano |
| `0102-nx679j-rmnet-data-port.patch` | `78f5504849043f383a321040aaca7b00` | `peek_port_qmi_for_data`: riconosce come data port i netdev con prefisso `rmnet`/`qmapmux` |
| `0200-nx679j-null-net-driver-ipa.patch` | `5e80cf16fc90248b2089c5bdd02d4cfc` | `net_driver` = `ipa` quando sysfs non ha driver (vendor) |
| `0201-nx679j-bearer-null-driver-ipa.patch` | `1d599f24e9eb11690ac52cc5a2246198` | idem nel bearer |
| `0202-nx679j-wait-link-port-timeout.patch` | `1e27109a5534f15acbd87861b26e287c` | timeout attesa "link port" a 10 s |
| `0203-nx679j-vendor-mux-link-port.patch` | `26f41ef5515860a7133ae86ea48ac369` | "grab link port" del netdev del bearer via `iflink` (il vendor non espone il link port) |
| `0204-nx679j-qmapmux-name-no-dot.patch` | `939a4b3834c4057c2ee1f705865a8d50` | `link_prefix_hint = "qmapmux<dbus-id>_"` (nome senza punto) |

**libqmi 1.36.0** — 6 patch locali in `feeds/packages/libs/libqmi/patches/`:

| Patch | md5 | Cosa fa |
|---|---|---|
| `100-fix-nlmsg-data-hdr.patch` | `7dd97aceb87d2910331e0772e8f1c0c6` | legge l'errore netlink da `NLMSG_DATA(hdr)` (prima leggeva dal buffer sbagliato) |
| `101-nx679j-netlink-zero-ifi.patch` | `346574404a16f3fc8f670e49b1e41676` | azzera `struct ifinfomsg` (family/type/index/flags/change = 0) |
| `102-nx679j-netlink-sendto-kernel.patch` | `fbbdde01c709f48d6b176817228fa7f1` | invia con `sendto()` al kernel invece di `g_socket_send` senza destinazione |
| `103-nx679j-netlink-sock-raw.patch` | `d572674c9e3420f2828fa7419a3e1e74` | socket `SOCK_RAW|SOCK_CLOEXEC` |
| `104-debug-nlqmi-hexdump.patch` | `2da4407de4dba088d155ef7eb6479071` | hexdump del messaggio netlink in `/tmp/nlqmi.txt` (debug) |
| `105-fix-rmnet-mask.patch` | `5cdeb27a159c38fdc80eaeeca823e5f3` | `rmnet_mask = rmnet_flags` (mask = 0x1 quando si chiede solo DEAGGREGATION) |
| **`106-probe-mux-ebusy.patch`** (aggiunta 2026-09-22 15:53, **non ancora sul device**) | `ca65e60171d788a9d6bfd4a0d6e9e813` | sonda ogni mux candidato con create+delete reale, perché *"a mux ID can be EBUSY with no netdev present (a previous failed attempt leaves the mux allocated in the driver)"* (il commento è nel patch stesso) |

> Mappatura nome↔md5 verificata con `md5sum $SDK/feeds/packages/libs/libqmi/patches/*` il
> 2026-09-22. Attenzione a un dettaglio: la copia nel SDK di `106` **non** è `/tmp/106-probe.patch`
> (md5 `4e6405686bd363149018d28c82c14ccc`) ma `/tmp/106-probe-fmt.patch` (md5
> `ca65e60171d788a9d6bfd4a0d6e9e813`, 6458 B) → nel SDK c'è la variante "fmt".

**Build corrente del SDK (dopo la patch 106)**: `staging_dir/target-aarch64_generic_musl/usr/lib/libqmi-glib.so.5.11.0`
= `5cedd832d9f344c1…` (3.848.920 B) → **NON è quello che gira sul device** (che è `d7488a944…`,
build 100–105). Conseguenza pratica: **non è più possibile ricostruire byte-identico il libqmi
del device dal tree attuale**; per ripetere il test va ricostruito con le patch correnti o
rientrodotte.

Comandi di build usati (dai log host):
`cd $SDK && make package/feeds/packages/libqmi/compile V=s` (log `/tmp/libqmi-build3.log`,
`time: package/feeds/packages/libqmi/compile#94.90#…`) e analogamente per
`package/feeds/packages/modemmanager/compile`; il risultato va preso da
`staging_dir/target-aarch64_generic_musl/usr/lib/` (o `.pkgdir/libqmi/usr/lib/`).

### 3.3 Immagini di boot (host) e cosa contengono **davvero**

| File | md5 | Note |
|---|---|---|
| `boot_b-v81-mm-std.img` | `8c13eb0f8554980a296b140e834ddcd0` (100.663.296 B) | ultima immagine scritta; script `build-v81.py` (2026-09-22 15:08) |
| `build-v80.py` | — | immagine precedente (`boot_b-v80-wifi.img` md5 `448b46ba3e2032cb735c8d837613b431`) |
| `owrt-live16.tar.gz` | `e9a2626de8438c2ac5b39c53c263aac6` | snapshot del rootfs live usato come base da `build-v81.py` |
| `build-v81.py` | 11.831 B | estrae `owrt-live16.tar.gz` in `build-v64/`, applica l'overlay, produce `merged.cpio`/`new-ramdisk.lz4` |

**Contenuto dell'immagine v81** (letto da `build-v64/overlay/owrt/…`, la dir di lavoro di
`build-v81.py`) confrontato col device:

| Payload | Nell'immagine v81 | Sul device ora | Coincide? |
|---|---|---|---|
| `usr/sbin/ModemManager` | `23121a96aad86bb73ee61e098cc08b38` | `23121a96aad86bb73ee61e098cc08b38` | ✅ |
| `usr/lib/libqmi-glib.so.5.11.0` | `9fb7c21042d9cc5920c47af243f6f693` (= patch 100–104) | `d7488a944be2ede0b5667296e1a3858e` (= 100–105) | ❌ **diverso** |
| `lib/netifd/proto/modemmanager.sh` | `b19c91fa27471908bd971ddb3a0f8879` | `b19c91fa27471908bd971ddb3a0f8879` | ✅ |
| `usr/lib/nx679j/modem/netlink-watch` | `f8c9805414e530d1c28535c4639d394a` (313.584 B) | `d469dd47c8eb750a8c23f0a78ad5fbc4` (325.208 B) | ❌ **diverso** |
| `etc/init.d/mm-netlink-watch` | `198bafe58cc78879b0ed0452eb92f187` | `198bafe58cc78879b0ed0452eb92f187` | ✅ |
| `lib/udev/rules.d/80-mm-nx679j.rules` | `fbfdcf61f9e463919295e040c4b46ce2` | `fbfdcf61f9e463919295e040c4b46ce2` | ✅ |
| `etc/hotplug.d/net/10-nx679j-modem` | `94aabdf7a5f4c8b28f3b0f3ffcc5a556` | `94aabdf7a5f4c8b28f3b0f3ffcc5a556` | ✅ |

> ⚠️ **Un riavvio del device riporta libqmi a 100–104 e netlink-watch alla versione senza marker**
> (sarebbe un "rollback silenzioso" e farebbe perdere tempo in diagnosi sbagliate).
> Prima di qualunque reboot: o si accetta il rollback consapevolmente, o si prepara una v82 che
> masterizzi lo stato voluto.

Verifica del flash su slot B: **non ri-eseguibile da questo host adesso** —
`ls -l /dev/sde41` → *No such file or directory*; `/proc/partitions` elenca solo `nvme0n1`,
`nvme0n1p1/2`, `zram0` (il telefono non espone la UFS a questo host in questo momento). Resta
valido quanto dichiarato nella sessione di flashing (dd su `/dev/sde41` 259:25 + readback);
lato file l'immagine è quella con md5 `8c13eb0f…`.

### 3.4 Artefatti host "work in progress" (NON ancora sul device)

| File | Data | Cosa è |
|---|---|---|
| `/tmp/netlink-watch2.c` | 2026-09-22 15:31 | sorgente v2 con `make_marker_links()` |
| `/tmp/netlink-watch2` | 15:31, 325.208 B | **md5 `d469dd47c8eb750a8c23f0a78ad5fbc4` = identico al `netlink-watch` che gira ORA sul device** (verificato con `md5sum` su entrambi) |
| `/tmp/netlink-watch-v2`, `/tmp/netlink-watch-v3` | 13:36 / 13:37, 313.584 B ciascuno | md5 `f8c9805414e530d1c28535c4639d394a` = versione **senza marker** = quella masterizzata in v81 e conservata in `mm-final/netlink-watch` |
| `/tmp/netlink-watch` | 13:00, 311.136 B | build intermedia (md5 non registrato in questa verifica) |
| `/tmp/netlink-watch.c` | 13:37, 3827 B (md5 `b4f0cff84af029790f4dcbe5625c5c54`) | sorgente della versione senza marker |
| `/tmp/106-probe.patch`, `/tmp/106-probe-fmt.patch` | 15:53 | patch "probe mux EBUSY"; **nel SDK è finita la variante `-fmt`** (`ca65e601…`) |
| `/tmp/mm-dry3/` | — | albero sorgente MM per prove a secco (contiene `mm-bearer-qmi.c`, `mm-bearer-qmi.c.orig`, plugin `qcom-soc`) |
| `/tmp/lq136/libqmi-1.36.0/` | — | albero sorgente libqmi con le patch applicate (usato per leggere il codice, §9) |
| `/tmp/l16clean/` | — | copia "pulita" del rootfs live (contiene anche `lib/udev/rules.d/80-mm-nx679j.rules`) |
| `/tmp/usr/lib/libqmi-glib.so.5.11.0` | 15:24 | vecchia libreria buildata (md5 `9fb7c210…`, 100–104) |
| `mm-final/` (dir del progetto) | 22 set 15:03 | payload "finali" di riferimento: `ModemManager`(`23121a96…`), `modemmanager.sh`(`1539128357ddbde3d7557cc5c4b6fff9`), `netlink-watch`(`f8c98054…`), `25-modemmanager-net`, `80-mm-nx679j.rules` |

> **Regola pratica per non confondersi coi nomi**: i nomi dei file in `/tmp` (v2/v3/watch2) sono
> fuorvianti. L'unica mappa affidabile è per **md5**: `d469dd47…` = versione con marker (quella in
> uso sul device), `f8c98054…` = versione senza marker (quella dentro l'immagine v81).
>
> Nota: **`mm-final/` è più vecchio del device** (il suo `netlink-watch` e il suo `modemmanager.sh`
> non coincidono con quelli in funzione: `f8c98054` vs `d469dd47`, `15391283` vs `b19c91fa`).
> Non usarlo come "verità" senza confrontare gli md5.

---

## 4. Cosa funziona già (con evidenza)

### 4.1 Catena dati custom v76 → traffico reale
`/tmp/chain.log` (boot di oggi, uptime 143→266 s):

```
[262.65] data wda rc=0
[262.76] data mux rc=0            <-- crea rmnet_data0
[263.85] data wds rc=0
[263.85] bearer_ready=1
[263.93] cell address rc=0
[266.07] cell ping rc=0
[266.07] criteria=1
[266.07] DONE
```

`/tmp/cell-ping.log` (evidenza di traffico dati end-to-end):

```
PING 8.8.8.8 (8.8.8.8): 56 data bytes
64 bytes from 8.8.8.8: seq=0 ttl=111 time=40.268 ms
64 bytes from 8.8.8.8: seq=1 ttl=111 time=25.083 ms
64 bytes from 8.8.8.8: seq=2 ttl=111 time=24.000 ms
--- 8.8.8.8 ping statistics ---
3 packets transmitted, 3 packets received, 0% packet loss
```

Stato attuale: `rmnet_data0` ha `inet 10.181.52.50/30` e i due holder QMI della catena sono vivi.
`/tmp/mux-create.log`: `CREATED name=rmnet_data0 parent=rmnet_ipa0(19) mux=1 flags=0x1`.
`/tmp/data-links-created.log` mostra il link e gli attributi rmnet:
`ifindex=20 name=rmnet_data0 parent=19 … kind=rmnet mux_id=1`, `rmnet_attr=2 bytes=01000000ffffffff`
(⇒ **flags=1, mask=0xFFFFFFFF**: questa è la combinazione che il vendor accetta, §9).

### 4.2 ModemManager "vede" il modem e prova a connettersi
Nel run MM delle 16:04/16:11: `mmcli -L` → `Modem/1`, `mmcli -m any` con `ports: qrtr0 (qmi),
rmnet_ipa0 (net)`, `state registered`, `access-technologies: lte, 5gnr`, plugin `qcom-soc`.
E il tentativo di connect arriva fino alla **creazione del link**, cioè tutta la catena
MM→QMI→WDS funziona e si ferma solo sul netlink (§5.1).

### 4.3 Il tool `rmnet-link` crea i link rmnet
`/usr/lib/nx679j/modem/rmnet-link <parent> <name> <mux>` (uso: `usage: … <parent> <name> <mux-id 0..255>`,
sorgente `/home/user/nx679j-stock/experiments/qrtr/rmnet-link.c`). Esito osservato: **riesce**
per mux 2..9, `EINVAL` per mux 100, `BUSY` per mux 1 (occupato da `rmnet_data0`).
Prova indiretta sul device adesso: i link `qmapmux0_0` e `qmapmux1_0` esistono (§2.4) e su questa
immagine **solo** questo tool (o la catena) può crearli: `ip` è busybox e non supporta `type rmnet`.

### 4.4 netlink-watch girerà e il suo canale rtnetlink è affidabile
PID 13496 vivo dalle 16:00; il log MM mostra le tracce del suo lavoro:
`hotplug: add network interface rmnet_ipa0: event processed` (16:12:48 e 16:19:03) e
`[rmnet_ipa0] property added: ID_MM_PHYSDEV_UID=qcom-soc` (16:20:22).

---

## 5. Cosa NON funziona (messaggi esatti)

### 5.1 ⛔ BLOCCO PRINCIPALE — MM non riesce a creare il netdev del bearer

Messaggio esatto (identico in 3 tentativi, mux id diversi):

```
<dbg> [000490451.367664] [modem1/bearer1] launching connection with QMI port (qrtr0) and data port (rmnet_ipa0) (multiplex required)
<dbg> [000490451.367668] [qrtr0/qmi] multiplex support already available when setting up data format
<dbg> [000490451.367739] [qrtr0/qmi] Creating RMNET link with flags: none
<dbg> [000490451.367763] Using dynamic mux ID 2
<wrn> [000490451.367835] [modem1/bearer1] connection attempt #3 failed: failed to create net link for device: failed to add link for device: Could not allocate link: Failed to add link with mux id 2: Netlink message with transaction 3 failed: No error information
<msg> [000490451.367877] [modem1] state changed (connecting -> registered)
```

Tentativi (tutti in `/var/log/mm.log`):

| # | Riga | Timestamp | mux scelto | esito |
|---|---|---|---|---|
| #1 (run modem9) | 44871 | 000490157 (≈16:09:17) | 1 | `… Failed to add link with mux id 1: Netlink message with transaction 1 failed: No error information` |
| #1 (run modem1) | 58525 | 000490381 (≈16:13:01) | 1 | idem, mux id 1, transaction 1 |
| #2 | 58831 | 000490396 (≈16:13:16) | 2 | idem, mux id 2, transaction 2 |
| #3 | 59133 | 000490451 (≈16:14:11) | 2 | idem, mux id 2, transaction 3 |

`No error information` = `strerror(0)`: l'errore netlink viene letto come 0 (vedi §9: la patch 100
corregge il puntatore da cui si legge `nlmsgerr`, ma il valore arriva comunque 0).

### 5.2 ⛔ SECONDO BLOCCO (ora attivo) — MM non crea più nessun modem

Dall'ultimo riavvio di MM (16:18:55) **il modem non esiste più**:

```
<msg> [000490735.553059] ModemManager (version 1.24.0) starting in system bus...
<dbg> [000490735.556156] [base-manager] adding port qrtr0 at sysfs path: (null)
<dbg> [000490736.573996] [rmnet_ipa0] property added: ID_MM_PHYSDEV_UID=qcom-soc
<dbg> [000490743.557471] [rmnet_ipa0] property added: ID_MM_CANDIDATE=1
<dbg> [000490745.558474] [plugin/qcom-soc] port rmnet_ipa0 filtered by udev tags
<dbg> [000490745.558537] [plugin-manager] task 1,rmnet_ipa0: will try with plugin 'generic'
<dbg> [000490747.558452] [plugin-manager] task 1,rmnet_ipa0: not supported by any plugin
<dbg> [000490747.558456] [device qcom-soc] fully ignoring port rmnet_ipa0 from now on
<msg> [000490747.558484] [base-manager] couldn't check support for device 'qcom-soc': not supported by any plugin
<dbg> [000490822.281220] [rmnet_ipa0] property added: ID_MM_PHYSDEV_UID=qcom-soc      (nuovo evento hotplug, 16:20:22)
<dbg> [000490826.282300] [base-manager] couldn't check support for device 'qcom-soc': not supported by any plugin
```

`mmcli -L` → **`No modems were found`**.

Nelle run precedenti (che *creavano* il modem) comparivano invece queste righe, che ora
**mancano del tutto**:

```
<dbg> [000490121.879335] [qrtr0/qmi] net driver set to 'ipa' (raw: '(null)')
<msg> [000490121.879343] [base-manager] modem for device 'qcom-soc' successfully created
```

**DA CONFERMARE** (ipotesi da testare, non un fatto): il support-check del plugin `qcom-soc`
richiede, nel proprio rules file `77-mm-qcom-soc.rules`, `SUBSYSTEM=="net", DRIVERS=="ipa"` — e
`rmnet_ipa0` sul vendor **non ha driver sysfs** (per questo esistono le patch 0200/0201). Nelle
run buone il modem veniva creato dal porto **qrtr0** e `rmnet_ipa0` si agganciava dopo come
*additional port* via `ID_MM_PHYSDEV_UID`; adesso la creazione da qrtr0 non avviene più.
Candidati da verificare: (a) probe QMI su qrtr0 fallita/andata in timeout, (b) QMI client non
ottenibile perché gli holder della catena occupano gli endpoint, (c) semplice problema di
sequenza/timing fra `netlink-watch`, hotplug e avvio di MM.

### 5.3 Meccanismi rotti / rumori che confondono (già verificati)

| Sintomo | Evidenza |
|---|---|
| `add_device` nel proto → teardown immediato | rimosso; oggi `grep -n add_device …` → nessun match |
| `option device 'any'` nell'UCI → `DEVICE_CLAIM_FAILED` (netifd prova a reclamare il device "any") | UCI attuale non ha `device`; default interno nel proto (righe 597/849) |
| `mm_monitor_cache_add`: `No 'sysfspath' for object '/org/freedesktop/ModemManager1/Modem/1' not found…` | logread 16:11:24; causa: `modem.generic.physdev` **vuoto** (`mmcli -m any` → `physdev : --`) → `proto_set_available` non viene mai chiamato dal monitor |
| `hotplug-call` non invocato dal kernel per i netdev creati a runtime | motivo per cui esiste netlink-watch |
| uevent sintetici funzionanti solo come iniezione manuale | netlink-watch inietta su `NETLINK_KOBJECT_UEVENT` gruppo 1 |
| `ip link add … type rmnet mux_id N` → `RTNETLINK answers: Invalid argument` | `ip` = **busybox 1.37.0** (iproute2 assente) ⇒ busybox non implementa il kind `rmnet`; i marker creati via `ip link add` **non nascono** |
| `[modem4] running QMI port 'qrtr0' reset with data interface 'qmapmux9_9': Couldn't query file info: Error when getting information for file "/dev/qrtr0": No such file or directory` | mm.log 5789-5790 (⚠️ `/dev/qrtr0` non esiste: normale, MM usa AF_QIPCRTR; non è la causa) |
| `ipa-wan __ipa_wwan_close:1365 [rmnet_ipa0]: ipa3_deregister_intf failed -22` | `dmesg` `[783.249141][T19083]` — T19083 era un processo **ModemManager**; traccia di chiusura della data path da parte di MM (stato "sporco", §6 p.9) |

---

## 6. Tentativi già fatti e risultati — LISTA ANTI-LOOP

> **Non ripetere nessuno di questi punti senza un dato nuovo.** Per ciascuno è indicato
> l'esito registrato e perché non ha risolto.

1. **Regola udev `KERNEL=="qmapmux*"` → `ID_MM_PHYSDEV_UID`** (per far assorbire a MM il link
   creato esternamente). **FALSIFICATA**: MM crea un *device separato* per quel porto; nel log
   `[base-manager] additional port qmapmux9_9 in device 'qcom-soc' added after device probing has
   already finished` + `wrn` → non è la strada per agganciare il bearer.
2. **hotplug manuale di `rmnet_ipa0`** (`mmcli --report-kernel-event` / uevent iniettato):
   funziona solo come **iniezione puntuale**, non in automatico al momento giusto.
3. **`/sbin/hotplug-call` non invocato dal kernel** per i netdev runtime → costruito
   `netlink-watch` (che resta la soluzione in uso; non è più un'ipotesi aperta).
4. **Patch MM 0202 (timeout link port 10 s) + 0203 (grab link port via iflink) + 0204 (nome
   `qmapmux…` senza punto)** applicate e presenti sul device: **non hanno sbloccato** la
   creazione del link (l'errore avviene *prima*, in libqmi/netlink).
5. **`add_device` nel proto** → causava **teardown immediato**: RIMOSSO (verificato: nessun
   match di `add_device` nel file sul device).
6. **`option device` in UCI**: con `'any'` → `DEVICE_CLAIM_FAILED` (netifd reclama il device
   fittizio "any"); senza opzione → il proto diceva *No device specified*. **Fix consegnato**:
   default interno `device="any"` nel proto (righe 597 e 849). ⚠️ Il blocco "No device specified"
   è chiuso, ma **non è stato ancora dimostrato** un connect riuscito end-to-end con questo fix.
7. **ifdown/ifup ripetuti**: **NON risolvono** da soli. Osservato in questa sessione: i tentativi
   #1–#3 si sono susseguiti a ~15 s e ~55 s senza intervento umano (probabile retrigger
   `10-nx679j-modem` + retry di netifd) e l'esito è identico. **Evitare loop di ifdown/ifup**:
   consuma tempo e sporca lo stato del driver.
8. **Marker link con `ip link add`** (netlink-watch v2): **falliscono** — `ip` è busybox e non
   supporta il kind `rmnet`, quindi `make_marker_links()` non crea nulla
   (marker "v2" = `qmapmux0_0…qmapmux5_0`, mux 4..9). **Fix indicato**: creare i marker con
   `rmnet-link`. *(Stato: i marker `qmapmux0_0` e `qmapmux1_0` esistono oggi sul device, quindi
   qualcuno li ha già creati col tool; il codice di netlink-watch però non è ancora stato
   aggiornato — /tmp/netlink-watch2.c continua a usare `ip link add`.)*
9. **Stato "sporco" del driver**: dopo una serie di errori, `rmnet_ipa3`/IPA possono entrare in
   uno stato in cui **tutte** le creazioni falliscono finché non si riavvia (catena o device).
   Traccia correlata: `dmesg [783.249141] ipa-wan __ipa_wwan_close:1365 [rmnet_ipa0]:
   ipa3_deregister_intf failed -22`. **Prima di dichiarare un test "fallito", verificare che il
   tool `rmnet-link` riesca ancora a creare un link**: se fallisce anche lui, il test non è
   valido (stato sporco, non ipotesi falsificata).
10. **Ipotesi "mux 1 è occupato ⇒ MM deve usare mux ≥ 2"**: **TESTATA e NON SUFFICIENTE**
    (novità di questa sessione). Con i marker presenti, libqmi ha scelto **mux 2** (log: `Using
    dynamic mux ID 2`, e il messaggio netlink contiene `IFLA_RMNET_MUX_ID=2`) e **la creazione è
    fallita comunque** (attempt #2 e #3). ⇒ la collisione di mux **non è la causa radice**.
11. **Patch libqmi 100–105** (netlink header, ifinfomsg azzerato, sendto al kernel, socket raw,
    hexdump, mask=flags): applicate; il fallimento persiste **con mask 0x1**.
    *(La mask precedente, quella mainline con 5 bit, è anch'essa fallita: il libqmi 100–104
    `9fb7c210…` girava prima.)*
12. **`bootstrap`/catena**: la catena v76 funziona e non va toccata; i suoi fallimenti
    (`STOP bootstrap`, `STOP dms`) sono gestiti da gate `*/tmp/*.once` e non fanno parte di
    questo problema.

---

## 7. Prossimi passi candidati (in ordine di priorità)

### Passo 0 — *(bloccante, prima di tutto)* rimettere MM in grado di vedere il modem
Oggi `mmcli -L` = `No modems were found` (§5.2). Senza il modem nessun test di connect è
possibile. Diagnosi read-only da fare subito:

```sh
grep -n "adding port qrtr0" /var/log/mm.log                 # confronto run buona (≈46259) vs run corrente (≈60002+)
grep -n "modem for device 'qcom-soc' successfully created" /var/log/mm.log
awk 'NR>=60002' /var/log/mm.log | grep -nE "qrtr|probe|support" | head -40
```
Poi confrontare la sequenza della run buona (righe ~45823-46290) con l'attuale (60002-60520) e
individuare la prima riga che diverge. **Criterio di successo del passo**: ricompare
`modem for device 'qcom-soc' successfully created` e `mmcli -L` mostrare un `Modem/N`.

### Passo 1 — far coincidere il messaggio netlink di libqmi con quello del tool che FUNZIONA
Questa è l'ipotesi più forte oggi, perché nasce da un confronto byte-per-byte (§9) e non da
supposizioni. Due differenze strutturali fra il messaggio che fallisce e quello che riesce:

| Campo | MM/libqmi (fallisce) | `rmnet-link` (riesce) |
|---|---|---|
| `IFLA_LINKINFO` | type `0x0012` (**senza** `NLA_F_NESTED`) | type `0x8012` (**con** `NLA_F_NESTED`) |
| `IFLA_INFO_DATA` | type `0x0002` (**senza** `NLA_F_NESTED`) | type `0x8002` (**con** `NLA_F_NESTED`) |
| `IFLA_RMNET_FLAGS` | `{flags=0x1, mask=0x1}` (patch 105) | `{flags=0x1, mask=0xFFFFFFFF}` |
| mux id | `u16` = 2 ✔ (coerente con la policy del kernel) | `u16` ✔ |
| resto (ifinfomsg azzerato, `IFLA_LINK=19`, flags `NLM_F_REQUEST|ACK|CREATE|EXCL`) | identico | identico |

Azione: **patch 106** su libqmi che (a) mette `NLA_F_NESTED` su `IFLA_LINKINFO` e `IFLA_INFO_DATA`
e (b) usa `mask = UINT32_MAX` come il tool (annullando l'effetto di 105) — mantenendo mux id
`u16` — poi rebuild, copia sul device, `ifup modem`, lettura esito.
**Criterio di successo**: compare un netdev `qmapmux1_1` (o simile) e nel log
`connected`/bearer con IP; in `ip link` il mux risulta attivo.
**Se fallisce**: falsifica l'ipotesi "formato del messaggio" e resta in piedi l'ipotesi
"stato/sequenza" (§6 punto 9) → verificare prima che `rmnet-link` riesca ancora (test di
controllo), poi riprovare il connect **subito dopo un boot fresco della catena**, senza
tentativi MM precedenti.

### Passo 2 — usare lo strumento che funziona come "oracolo di mux"
`rmnet-link` crea link su mux 2..9 e restituisce `BUSY`/`EINVAL` in modo pulito; usarlo per:
1. marcare i mux già occupati (i "marker" di netlink-watch: `qmapmux0_0`, `qmapmux1_0`, …) →
   funzione già dimostrata: libqmi sceglie il mux successivo libero (§9);
2. **test di controllo prima di ogni prova MM**: se `rmnet-link rmnet_ipa0 <nome-test> <mux>` non
   riesce, il driver è in stato sporco → il test MM non è valido (riavviare la catena/device).
   *(Attenzione: creare un link di test è una modifica dello stato del device: da fare solo
   quando si è deciso di testare, non durante verifiche read-only.)*

### Passo 3 — aggirare del tutto il ramo "libqmi crea il link"
Solo se i passi 1–2 non bastano. Serve a **isolare** il problema senza perdere il traguardo
"componente standard": creare il link con un helper (netlink-watch o una versione v3 che chiami
`rmnet-link` quando MM segnala che il bearer sta partendo) e far sì che MM **lo riconosca come
proprio link port** (patch 0203 già presente, che aggancia il link via `iflink`).
Vantaggio: dimostra che il resto della catena standard (ifup → proto → MM → netifd) funziona.
Rischio: non è più "nessun componente custom" → va scritto come **piano B** ed etichettato.

### Passo 4 — consolidare in un'immagine
Quando un test riesce: masterizzare lo stato buono (libqmi con la patch vincente +
netlink-watch aggiornato + eventuale v3) in una **v82** (`build-v82.py` sul modello di
`build-v81.py`, base `owrt-live16.tar.gz`), altrimenti ogni reboot fa rollback (§3.3).
Verifica del flash: paragone md5 dei file di `build-v8x/overlay/owrt/...` con quelli letti dal
device (questa tabella in §3.3 è il modello da riempire).

### Passo 5 — pulizia
* aggiornare `netlink-watch*.c` per usare `rmnet-link` al posto di `ip link add` (§6 p.8);
* ripulire i ~14 zombie e decidere se il monitor MM (`ModemManager-monitor`) è utile qui
  (fallisce sempre su `physdev` vuoto);
* valutare se `10-nx679j-modem` (retrigger `ifup modem` a ogni `add` di `rmnet_data0`) va
  reso meno aggressivo: oggi è la probabile causa dei tentativi automatici #1–#3.

---

## 8. Comandi utili

### 8.1 Accesso (read-only)

```sh
# helper del progetto
/home/user/nx679j-stock/experiments/20260920-wifi-luci/nxssh.sh 'COMANDO'

# equivalente esplicito
ssh -i /home/user/.ssh/nx679j_key -o IdentitiesOnly=yes -o StrictHostKeyChecking=no \
    -o UserKnownHostsFile=/dev/null root@10.0.0.1 'COMANDO'
```
Nota: la host key dropbear cambia ad ogni boot → `UserKnownHostsFile=/dev/null` è obbligatorio.

### 8.2 Fotografia dello stato (tutto read-only)

```sh
nxssh.sh 'date; cat /proc/uptime; uname -a'
nxssh.sh 'md5sum /usr/sbin/ModemManager /usr/lib/libqmi-glib.so.5.11.0 \
          /lib/netifd/proto/modemmanager.sh /usr/lib/nx679j/modem/netlink-watch \
          /lib/udev/rules.d/80-mm-nx679j.rules /etc/hotplug.d/net/10-nx679j-modem'
nxssh.sh 'uci show network.modem; ip link show; ip -4 addr show'
nxssh.sh 'mmcli -L; mmcli -m any --output-keyvalue | head -40'
nxssh.sh 'ps w | grep -E "[M]odemmanager|ModemManager|netlink-watch|qmi-qrtr"'
nxssh.sh 'cat /sys/class/remoteproc/remoteproc3/state; cat /tmp/chain.log | tail -5'
```

### 8.3 Log

| Cosa | Comando |
|---|---|
| Log MM completo (DEBUG) | `nxssh.sh 'tail -100 /var/log/mm.log'` · `wc -l /var/log/mm.log` |
| Solo gli errori di connect | `nxssh.sh 'grep -nE "connection attempt|couldn.t connect bearer" /var/log/mm.log'` |
| Link / mux scelti da libqmi | `nxssh.sh 'grep -nE "Creating RMNET link|dynamic mux|static mux" /var/log/mm.log'` |
| Messaggio netlink inviato (patch 104) | `nxssh.sh 'cat /tmp/nlqmi.txt'` (una riga = un messaggio; hex) |
| Log catena v76 | `nxssh.sh 'cat /tmp/chain.log'`, `/tmp/data-*.log`, `/tmp/cell-*.log` |
| Esito ping di boot (evidenza dati) | `nxssh.sh 'cat /tmp/cell-ping.log'` |
| Syslog/hotplug/MM | `nxssh.sh 'logread | grep -iE "ModemManager|hotplug|netifd|netlink" | tail -30'` |
| Kernel (rmnet/ipa) | `nxssh.sh 'dmesg | grep -iE "rmnet|ipa|qmap" | tail -20'` |

### 8.4 Azioni che **modificano** lo stato (usare solo quando deciso)

```sh
nxssh.sh 'ifup modem'                      # avvia il connect standard (proto modemmanager)
nxssh.sh 'ifdown modem'                    # teardown (con disable_modem=0 NON disabilita il modem)
nxssh.sh 'ubus call network.interface.modem status'
# ricreare i marker (se mancano): <parent> <name> <mux>
nxssh.sh '/usr/lib/nx679j/modem/rmnet-link rmnet_ipa0 qmapmux1_0 5'
# test di controllo dello stato del driver (deve riuscire; poi eventualmente ip link del <nome>)
nxssh.sh '/usr/lib/nx679j/modem/rmnet-link rmnet_ipa0 zztest 8'
# copia di un binario nuovo sul device (MODIFICA: solo quando si testa)
scp -i /home/user/.ssh/nx679j_key -O -o IdentitiesOnly=yes -o StrictHostKeyChecking=no \
    -o UserKnownHostsFile=/dev/null <file> root@10.0.0.1:/tmp/
```

### 8.5 Build lato host

```sh
SDK=/tmp/sdk/openwrt-sdk-25.12.5-armsr-armv8_gcc-14.3.0_musl.Linux-x86_64
cd $SDK
make package/feeds/packages/libqmi/compile V=s          # log: /tmp/libqmi-build3.log
make package/feeds/packages/modemmanager/compile V=s
# artefatti:
#   $SDK/staging_dir/target-aarch64_generic_musl/usr/lib/libqmi-glib.so.5.11.0
#   $SDK/build_dir/target-aarch64_generic_musl/libqmi-1.36.0/.pkgdir/libqmi/usr/lib/libqmi-glib.so.5.11.0
md5sum $SDK/feeds/packages/libs/libqmi/patches/*
md5sum $SDK/feeds/packages/net/modemmanager/patches/*
```

---

## 9. Appendice A — decodifica del messaggio netlink (evidenza chiave)

`/tmp/nlqmi.txt` sul device (193 byte, scritto dalla patch 104 il 22-09 alle ~16:14, quindi è il
messaggio del **tentativo #3** — `nlmsg_seq=3`, coerente con `transaction 3`):

```
60******************************************************************************…
```

Decodifica (byte-per-byte):

```
nlmsg_len=96  nlmsg_type=0x10 (RTM_NEWLINK)  nlmsg_flags=0x605 (REQUEST|ACK|CREATE|EXCL)
nlmsg_seq=3   nlmsg_pid=0
ifinfomsg: family=0 type=0 index=0 flags=0x0 change=0x0          <-- patch 101 (azzerato)
IFLA_LINK (type 5)      = 19          (= ifindex di rmnet_ipa0)
IFLA_IFNAME (type 3)    = "qmapmux1_1"
IFLA_LINKINFO (type 18, len 40)          <-- SENZA NLA_F_NESTED
   IFLA_INFO_KIND (type 1) = "rmnet"
   IFLA_INFO_DATA (type 2, len 24)       <-- SENZA NLA_F_NESTED
      IFLA_RMNET_MUX_ID (type 1, u16) = 2
      IFLA_RMNET_FLAGS  (type 2, 8 B) = { flags=0x00000001, mask=0x00000001 }
```

Confronto col tool che funziona (`experiments/qrtr/rmnet-link.c`, che **crea davvero** i link):

```c
struct ifla_rmnet_flags flags = { .flags = RMNET_FLAGS_INGRESS_DEAGGREGATION,
                                  .mask  = UINT32_MAX };            /* <-- mask piena */
r->h.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL;
r->ifi.ifi_family = AF_UNSPEC;                                     /* resto 0 */
put_attr(r, IFLA_LINK, &parent, sizeof(parent));
put_attr(r, IFLA_IFNAME, name, strlen(name) + 1);
put_attr(r, IFLA_LINKINFO | NLA_F_NESTED, NULL, 0);                /* <-- NESTED */
put_attr(r, IFLA_INFO_KIND, "rmnet", sizeof("rmnet"));
put_attr(r, IFLA_INFO_DATA | NLA_F_NESTED, NULL, 0);               /* <-- NESTED */
put_attr(r, IFLA_RMNET_MUX_ID, &mux /* uint16_t */, sizeof(mux));
put_attr(r, IFLA_RMNET_FLAGS, &flags, sizeof(flags));
```

Policy lato kernel vendor (riferimento in repo:
`experiments/refs/ipa-lineage20/rmnet_config.c`, righe 68-84 e 189-235):

```c
static const struct nla_policy rmnet_policy[__IFLA_RMNET_EXT_MAX] = {
    [IFLA_RMNET_MUX_ID] = { .type = NLA_U16 },                    /* ⇒ il mux va come u16 (OK) */
    [IFLA_RMNET_FLAGS]  = { .len = sizeof(struct ifla_rmnet_flags) },   /* 8 byte (OK) */
};
static int rmnet_newlink(...) {
    real_dev = __dev_get_by_index(src_net, nla_get_u32(tb[IFLA_LINK]));
    if (!real_dev || !dev) return -ENODEV;
    if (!data[IFLA_RMNET_MUX_ID]) return -EINVAL;                 /* unico EINVAL "statico" */
    …
    if (data[IFLA_RMNET_FLAGS]) { flags = nla_data(…);
        data_format = flags->flags & flags->mask; port->data_format = data_format; }
```
⇒ Dal riferimento il `mask` è un semplice AND con `flags` (mask 0x1 e mask ~0 danno lo stesso
`data_format`), ma **il kernel effettivamente in uso è un vendor 5.10.66** (il riferimento è
`lineage20`): resta quindi **da verificare empiricamente** quale delle due differenze
(`NLA_F_NESTED`, `mask`) fa fallire il messaggio. **Questo è il test del Passo 1 (§7).**

### Come nasce il nome del link (spiega i marker e `qmapmux9_9`)

Da `/tmp/lq136/libqmi-1.36.0/src/libqmi-glib/qmi-net-port-manager-rmnet.c`:

```c
/* By convention, ifname_prefix0 corresponds to mux ID 1, and so on. */
return g_strdup_printf ("%s%u", ifname_prefix, mux_id - 1);        /* riga 79 */
…
if (ctx->mux_id == QMI_DEVICE_MUX_ID_AUTOMATIC) {
    ctx->mux_id = get_first_free_mux_id (self, ifname_prefix);     /* riga 515 */
    g_debug ("Using dynamic mux ID %u", ctx->mux_id);              /* riga 517 */
}
…
g_debug ("Creating RMNET link with flags: none");                  /* log visto in mm.log */
```
e `get_first_free_mux_id()` (righe 420-442) sceglie il primo mux il cui **nome** non esiste:
`mux_id_is_free = !if_nametoindex (ifname)`.

Conseguenze verificate:
* il prefisso è quello di MM (patch 0204): `qmapmux<dbus-id>_` → con `Modem/1` e mux 2 il nome è
  **`qmapmux1_1`** ✔ (coincide col dump) e il nome per mux 1 è **`qmapmux1_0`**;
* quindi per "deviare" MM su mux ≥ 2 basta **possedere il nome** `qmapmux1_0` (non serve che il
  mux sia spawnato dallo stesso driver): la logica guarda solo il nome. È esattamente ciò che fa
  netlink-watch v2 — e spiega perché con i marker presenti è stato scelto mux 2;
* `qmapmux9_9` osservato nella sessione precedente = prefisso `qmapmux9_` + (mux−1=9) ⇒ mux 10 di
  un `Modem/9` (`/tmp/netlink-watch2.c` commenta: *"vendor rmnet rejects >= 10"*), oppure un nome
  scelto a mano col tool. **DA CONFERMARE** con una nuova lettura prima di usarlo come prova.

---

## 10. Changelog di questo file

* **2026-09-22 16:22 (host 15:47→16:22)** — creazione. Stato riletto integralmente sul device
  (read-only): md5 di tutti i componenti, UCI, `ip link`, `ps w`, `mmcli -L`/`-m any`,
  `/var/log/mm.log` (60.441 righe), `/tmp/*.log`, `dmesg`, `logread`; inventario host (patch SDK
  con md5, artefatti `/tmp`, `mm-final/`, immagini v80/v81 + confronto col device); decodifica
  del messaggio netlink (`/tmp/nlqmi.txt`) e confronto col tool `rmnet-link`.
  **Novità principali introdotte qui**: (1) il mux 2 è stato provato da MM e fallisce ⇒ l'ipotesi
  "collisione mux 1" è falsificata; (2) due differenze strutturali trovate fra messaggio MM e
  messaggio del tool funzionante (`NLA_F_NESTED`, `mask`) ⇒ nuovo test al Passo 1;
  (3) avviso di rollback: la v81 **non** contiene il libqmi attualmente in esecuzione né
  netlink-watch v2; (4) dopo il riavvio delle 16:18:55 MM non crea più il modem (`No modems were
  found`) ⇒ Passo 0 bloccante.

---

## DISPLAY — mappa completa dei limiti (22/09 sera, test ripetuti con webcam OBS)

**Percorso che FUNZIONA** (verificato 3 volte stasera, pattern R/V/B/N visibile in webcam):
1. ESD kill OBBLIGATORIO: `mount -t debugfs none /sys/kernel/debug` + `echo esd_sw_sim_success > /sys/kernel/debug/qcom,mdss_dsi_r6130_1080_2400_amoled_cmd/esd_check_mode` — **si perde ad ogni reboot** (debugfs in RAM). Senza: ogni setcrtc/flip → mismatch 0x0A → ESD recovery → **RESET HW → reboot**.
2. `nx679j-drmtest` da **crtc VERGINE** (mai configurato in questo boot): setcrtc legacy con variante `1080x2400x90cmd` → OK, il contenuto si vede.
3. Il tool deve restare VIVO (hold 86400s). Il boot lo fa da solo (`display-late.sh`: attende uptime>900 → moduli touch → gap 250s → ESD kill → drmtest).

**Limiti scoperti (test 22/09 sera)**:
- **aggiornamento contenuto: il pannello cmd-mode NON cambia con setcrtc successivi.** `paneltest` (2 setcrtc in-process, timing VERBATIM dal connettore — `best='1080x2400x90cmd' clk=248410 ht=1122 vt=2460`): SETCRTC#1 OK → R/V/B/N visibile; SETCRTC#2 OK nel kernel → **il pannello mostra ANCORA il fb1**. L'aggiornamento dovrà passare dal PAGE_FLIP.
- **chiusura del processo master (anche pulita, a fine hold) → reset HW → reboot.** Il processo display non deve MAI uscire (o uscire solo con display già spento).
- **setcrtc da processo nuovo dopo chiusura del precedente (crtc "sporco") → ioctl bloccato → reset** (recolor, paneltest da sporco; 2 reboot).
- **PAGE_FLIP: mai testato pulito.** Nel kiosktest (condizioni sporche: setmaster multipli, setcrtc su crtc di altro processo) → crash+reboot. Da testare: flip puro in-process su crtc attivo configurato dal proprio processo.
- **i tool nuovi NON sopravvivono al reboot** (la dir `/usr/lib/nx679j/modem` viene ripristinata dal boot script coi soli tool storici: drmtest, touchpaint, ecc.) → `scp -O` ad ogni sessione di test.
- **nodi /dev/dri** creati da display-late.sh: se si interviene prima, `mknod /dev/dri/card0 c 226 0` (+ renderD128 c 226 128).
- SSH: usare **USB 10.0.0.1** (rtt 3ms); WiFi 192.168.77.1 instabile sotto carico; `scp` senza `-O` fallisce (no sftp-server); busybox: niente pkill/fuser/top/modetest.

**Prossimo esperimento (prima cosa, prossima sessione)**: `panelflip` = processo unico: ESD kill → setcrtc#1 (verbatim, da vergine) → **flip loop tra 2 fb dumb** (colori alternati) → se il pannello cambia colore ⇒ flip FUNZIONA in-process ⇒ kiosk = processo unico con flip (WPE/cog headless→fb→flip, touch via event0). Se il flip fallisce anche pulito: valutare commit vendor SDE o UI a pagina singola.

## SORGENTI KERNEL UFFICIALI NUBIA (trovati 22/09 sera — RISORSA CHIAVE)
- **`github.com/ztemt/NX679S`** (e `NX709S`) — sorgenti kernel UFFICIALI ZTE/Nubia (95K file)! Clone shallow in `/home/user/re-nubia/NX679S` (`git show HEAD:<path>` per leggere i file; il repo è --no-checkout).
- `display-drivers.zip` (dal repo) estratto e copiato **stabilmente** in `re-nubia-disp/src/` (`sde/`, `dsi/`, `msm_atomic.c`, `msm_drv.c/h`).
- **Fatti dal codice**: il driver usa il percorso **ATOMIC completo** (`.atomic_commit = msm_atomic_commit`, `.atomic_commit_tail`, `.page_flip = drm_atomic_helper_page_flip`); NESSUN ioctl MSM custom. Il commit → `handle_frame_done` (ping-pong done IRQ, `sde_encoder_phys_cmd.c`) → sblocca i page-flip events / release fence. Patch Nubia: `msm_atomic.c` (hbm in commit), `dsi_pwr.c` (regolatori), `dsi_panel.c` (aod/hbm/lhbm/demura/esd extra check).
- **IPOTESI da testare**: il setcrtc legacy successivo NON setta il PLANE di scanout → da provare `drmModeSetPlane` (o atomic commit completo col plane primario: FB_ID+CRTC_ID) — è il percorso che usa Android via SDM.
- Moduli custom nel kernel: `panel_event_notifier.ko` (attiva il display a eventi), `msm_ext_display.ko`, `nubia_goodix_ts` (touch). Il firmware DT (`running_fdt.dts` nello stock_dump) ha il nodo pannello completo: TE-pin abilitato (`qcom,mdss-dsi-te-using-te-pin`, `te-dcs-command=0x01`), `wr-mem-continue=0x3c`, 4 timing set (90/120/60/165fps) con `timing-switch-command`, `esd-check-enabled`, comandi Nubia (demura/change-page).

---

## DISPLAY — SVOLTA 22/09 sera: IL COMMIT ATOMIC FUNZIONA!

### La formula VERIFICATA (ROSSO→VERDE visibile su webcam)
1. ESD kill: `echo esd_sw_sim_success > /sys/kernel/debug/qcom,mdss_dsi_r6130_1080_2400_amoled_cmd/esd_check_mode`
2. Guard v2 CON DROP_MASTER (NON deve tenere il master! check `/sys/kernel/debug/dri/0/clients`)
3. **`SET_CLIENT_CAP(ATOMIC=3)` OBBLIGATORIA** (ioctl 0x4010640d) — senza → EINVAL su tutti i commit
4. **Commit atomic COMPLETO, ordine c-n-p:**
   - crtc 152: MODE_ID (blob del mode 90cmd) + ACTIVE=1
   - connector 56: CRTC_ID=152 + autorefresh=0 (+ frame_trigger_mode=2 opz.)
   - plane 103 (**NON 78!**): CRTC_ID=152 + FB_ID + SRC_W/H/X/Y + CRTC_W/H/X/Y (SRC in 16.16)
   - flags: ALLOW_MODESET (0x400) | TEST_ONLY (0x100) per validare; poi 0x400
5. Tool: `nx679j-atom11` (in experiments/20260920-wifi-luci/). Ioctl corretti v5.10: GETPROPERTY=0xAA, OBJ_GETPROPS=0xB9, GETPLANERES=0xB5, PAGE_FLIP=0xB0, DIRTYFB=0xB1, SETPLANE=0xB7, ATOMIC=0xBC (56B struct), CREATE_BLOB=0xBD. Le prop del plane NON escono da OBJ_GETPROPS → ID globali via brute-force 1..400 (FB_ID=17 CRTC_ID=20 SRC_W=11 SRC_H=12 SRC_X=9 SRC_Y=10 CRTC_W=15 CRTC_H=16 CRTC_X=13 CRTC_Y=14 MODE_ID=23 ACTIVE=22 autorefresh=49 frame_trigger_mode=66).

### I limiti (perché oltre il 3° commit crasha)
- **4° commit full**: call trace `dsi_clk_update_parent` → "failed to set byte clk parent" EBUSY(-16) → clk_set_rate(-22) → "enc55 resource kickoff failed" → RESET HW → reboot. Causa: MODE_ID/ACTIVE ripetuti → percorso modeset → clock DSI re-init → EBUSY.
- **Commit successivi senza crtc** ("np" conn+plane): EINVAL "active without enabled" (drm_atomic.c:337 — crtc state enable=0).
- **Commit con MODE_ID senza ACTIVE**: crash (log 433, run m2).
- **Chiusura fd del processo** → cleanup driver → crash ANCHE col guard: IL PROCESSO DISPLAY DEVE RESTARE VIVO PER SEMPRE.

### Prossimi passi (in ordine)
1. Leggere log 433 del run m2 (cosa ha fatto prima del crash).
2. Replicare ESATTAMENTE il percorso SDM: primo commit con ANCHE le prop power/LP del connector; commit successivi solo plane FB_ID — e capire perché enable=0 (drm_atomic.c:337 / drm_atomic_helper check_modeset).
3. Log test: rawdump 433 (atom11), 434 (dmesg live). Comandi: `dd if=/proc/1/root/dev/rd bs=32768 skip=$((32+433)) count=1`.
4. Journal mirror dmesg: slot 8+ (rolling, boot UUID nell'header).

---

## DISPLAY — FLIP LOOP RISOLTO DEFINITIVAMENTE! 🎉 (22/09 notte)

### La scoperta finale: IDLE POWER COLLAPSE
Il "limite ~3 commit" NON era un limite del driver! È il **power collapse**:
- `IDLE_POWERCOLLAPSE_DURATION` = ~58ms (`sde_encoder.h:52`) — [se [passano [>58ms [senza [commit, [il [driver [SPEGNE [i [clock [DSI [([idle [power [collapse, [`delayed_off_work`).
- Il commit successivo li RIACCENDE (`_sde_encoder_rc_kickoff` → `rc_state IDLE→ON` → `dsi_clk_req_state`) — [e [dopo [2 [riaccensioni: [`dsi_clk_update_parent` EBUSY(-16) → `clk_set_rate` -22 → RESET HW.
- **Con gap < 58ms tra commit** (come un compositor a 60fps = 16ms): il timer viene RI-SCHEDULATO ad ogni kickoff (`_sde_encoder_rc_kickoff_delayed`) → il collapse NON scatta MAI → **flip INFINITO** ✓.

### La formula completa (VERIFICATA: 60/60 commit OK)
1. ESD kill + guard v2 VERIFICATO attivo (`ps | grep guard` = 1) + `/dev/dri/card0` (mknod c 226 0 — [si [perde [ad [OGNI [reboot!).
2. Commit #1: cnp completo (crtc MODE_ID+ACTIVE + conn CRTC_ID/autorefresh + plane FB_ID+rect) → [accende.
3. Commit #2..N: **PLANE-ONLY** (solo FB_ID + CRTC_ID del plane + rect invariati) — [ricetta [SDM [`SetupAtomic` (`hw_device_drm.cpp`: `PLANE_SET_FB_ID`+`PLANE_SET_CRTC` ogni frame; crtc/conn SOLO al primo).
4. **usleep(gap) con gap ≪ 58ms** (testato 30ms) — [e [fbs [in [ROTAZIONE [([4 [fb, [come [un [compositor; [NON [allocare [50 [fb [([600MB [GEM [→ [fallimenti).
5. Tool: `nx679j-atom17` (N commit, gap_us, log rawdump 442). Dmcap: `nx679j-dmcap.sh` → dmesg in rawdump 440 (per trace post-crash).

### Evidenze
- Log 442: `TUTTI 59 FLIP OK` + `TUTTI I 60 COMMIT OK` (hold, processo vivo).
- Webcam: colore pieno sul pannello ✓ (prova visiva).
- Trace del vecchio crash (con sleep(2)): rawdump 440 → `dsi_recheck_clk_state` → [etc.

---



### La scoperta (dopo debug di /«redacted», /rfs, rawdump)
- Rootfs OpenWrt, `/rfs` e rawdump (>15MB) sono **TUTTI volatili** (rawdump pulito al boot).
- La dir `/«redacted»` (switch.sh, relay, worker, journal) **vive NEL RAMDISK dell'immagine boot_b**: viene ri-estratta dall'immagine ad ogni boot.
- **Conseguenza: la persistenza vera = il ramdisk stesso.**

### Il meccanismo v87 (FUNZIONANTE, verificato al boot)
- Sul PC: `persist-tars/{etc,luci,tools}.tar` (creati dal device con `tar cf`)
- `build-v87.py`: estrae owrt-live19 → **PERSIST-INSIDE**: estrae i 3 tar sopra `OVL/owrt/` (sovrascrive i file base) → cpio+lz4 → `boot_b-v87-final.img` (assert su modemmanager.js/atom11/network)
- Flash: `scp` → `rm -f /dev/sde41; mknod /dev/sde41 b 259 25` → `dd oflag=direct conv=fsync` → **2 letture fredde** → reboot
- **Verifica post-boot v87**: `LUCI=1 TOOL=2 UCI=2 MODEM=1` → TUTTO al 100% (LuCI, tool, UCI device='qcom-soc'+force_connection, modem up da solo a ~360s, ping 0%)

### Aggiornare la persistenza in futuro (3 passi, ~1 min, tutto da SSH)
1. Sul device: modifica i file → `cd /; tar cf /tmp/pw/<nome>.tar <percorsi>` → `scp` sul PC in `persist-tars/`
2. `python3 build-v87.py` (o vNN successiva, ~10s)
3. `scp` img → `dd` sde41 → reboot

### Contenuti attuali
- **etc.tar**: /etc completo (config UCI con device='qcom-soc' + force_connection='1')
- **luci.tar**: /www/luci-static + /usr/share/luci + rpcd acl.d (LuCI 26.263 + luci-proto-modemmanager)
- **tools.tar**: /usr/lib/«redacted» completo (42 file: atxxxx 8-11, guard v2, drmtest, paneltest, touchsim, ecc.)

### Check di verifica post-boot (un comando)
```
ssh -i ~/.ssh/«redacted»_key root@10.0.0.1 'L=$(ls /www/luci-static/resources/protocol/modemmanager.js >/dev/null 2>&1 && echo 1 || echo 0); T=$(ls /usr/lib/*/modem/ | grep -cE "atom11|guard"); U=$(uci show network.modem | grep -cE "qcom-soc|force"); M=$(ubus call network.interface.modem status | grep -c "\"up\": true"); echo "LUCI=$L TOOL=$T UCI=$U MODEM=$M"'
```

### Nota rawdump (NON usarlo per persistenza)
Scrivibile e leggibile (64MB, dd a offset alti OK), MA **viene pulito al boot** (zona >15MB azzerata). Utile solo per log test letti nello stesso boot o poco dopo.

---

## DISPLAY — DIAGNOSI STRIDE + TEST PITCH-FIX (stato corrente; reboot da indagare)

### Fatti osservati
- L'utente conferma che il touch risponde; sul pannello vede linee diagonali arancioni/verdi e sfondo glitchato. Screenshot webcam via V4L2/OBSBOT: linee e artefatti, non una schermata LuCI.
- Sul device, `/sys/kernel/debug/dri/0/framebuffer` mostra i framebuffer kiosk `XR24`, `1080x2400`, `pitch[0]=4352`, `size=10444800`; framebuffer attivo sul plane 103. ESD mode letto come `esd_sw_sim_success`; una sola guardia e un solo kiosk erano attivi.
- Nel `nx679j-kiosk2.c` precedente, `fill_rect()` e il clear iniziale indicizzavano le righe con `W`/`W*H`, ignorando il pitch restituito da `CREATE_DUMB`. Per XR24: `1080*4=4320` byte visibili contro pitch 4352, cioè 32 byte/8 pixel di padding per riga; il disallineamento cresce lungo l'altezza. Fonte di formato: DRM UAPI `drm_mode_create_dumb.pitch` e framebuffer `pitches[]` (`https://docs.kernel.org/gpu/drm-uapi.html`, `https://docs.kernel.org/gpu/drm-kms.html`).
- Test host `test-kiosk-stride.c`: RED prima della correzione (scriveva nel padding della riga 0 invece che nella riga 1); GREEN dopo la correzione.
- Modificato il solo percorso di rendering: salvato `pitches[0..1]`, clear per-riga usando il pitch, `fill_rect()`/`draw_inc()` ricevono il pitch esplicito. Touch e sequenza DRM non modificati. Build statica AArch64/musl GCC 14.3.0; hash locale/device `1c840ddc8212cf4b9d91ff2a592edf743d2e6c4544342ed3086aaca2b5f0a7a6`.

### Esito hardware — NON dichiarare risolto
- Prima del test: uptime 5134s, guard PID 24297, kiosk vecchio PID 25922, ESD mode attivo, pitch 4352.
- Il vecchio kiosk è terminato ma il PID è rimasto zombie (cmdline vuota); DRM clients mostrava solo la guardia. Il comando SSH di lancio del nuovo binario è scaduto a 45s; poi SSH ha rifiutato connessioni. Webcam diretta ha mostrato logo REDMAGIC/Powered by Android; controllo successivo: uptime 77s e nessun kiosk/guard nel primo tratto di boot. Il rawdump/dmesg è volatile ed è stato azzerato: manca il trace causale.
- **La correzione stride NON è ancora verificata visivamente nel kiosk.** Il comando SSH di lancio è scaduto e il telefono è ripartito; non è provato se il nuovo processo abbia eseguito il commit. Ipotesi aperta: chiusura del vecchio master/processo vs. nuovo modeset/clock SDE vs. altra causa. Non attribuire il reboot al pitch-fix senza log.
- Fan-out mirato reboot/lastclose/ESD concluso (8 assi). Sorgenti verificati: [SDM SM8450 `hw_device_drm.cpp` @ `0de890d`](https://github.com/TheGammaSqueeze/GammaOSNextDistribution/blob/0de890dede9a550da87920d61b4b08da7c80fb02/hardware/qcom-caf/sm8450/display/sdm/libs/core/drm/hw_device_drm.cpp) usa `GetBufferLayout()` per stride/offset e applica MODE_ID/ACTIVE nel first-cycle; la [DRM UAPI v5.10](https://www.kernel.org/doc/html/v5.10/gpu/drm-uapi.html) fa restituire al chiamante il pitch del dumb buffer. La [tree ZTE/NX679S](https://github.com/ztemt/NX679S/tree/0280bdce975602ff17161f5de1b8fc63dc96bd47) è **5.10.101**, non prova la build live 5.10.66; il checkout locale `kernel-patch-test/stock-kernel-source/Makefile` è addirittura 5.4.242 e non è evidenza esatta per il device. La [DRM core v5.10.66](https://github.com/gregkh/linux/blob/v5.10.66/drivers/gpu/drm/drm_file.c#L450-L453) distingue lastclose globale (ultimo fd) dal rilascio di un singolo master; la sorgente Qualcomm di famiglia mostra un possibile `preclose`/cleanup commit, ma non è verificata byte-per-byte sul 5.10.66 del telefono. Il full modeset dopo idle può riattivare/reparentare i clock DSI; `esd_sw_sim_success` evita il controllo ESD di stato, non ogni errore DSI. Nessun report pubblico trovato che associ direttamente NX679J+questo glitch/reboot. **Conclusione: il reboot precedente resta senza causa provata.**

### Baseline display dopo il reboot — processo DRM lasciato vivo
- `display-late.sh` ha avviato `nx679j-drmtest`: ESD `esd_sw_sim_success`; `SETCRTC OK` 1080x2400@90cmd su CRTC 152, poi hold. Verifica live più recente: boot ID `746a3fd8-47e2-40cd-979a-6af24b49cb41`, uptime 3858s, PID 24653 unico DRM master. Il framebuffer attivo è XR24 1080x2400 con `pitch[0]=4352`, offset 0.
- Webcam/V4L2, acquisizione `/home/user/.hermes/profiles/kernel-re/cache/scratch/nx679j-postfanout-current.png`: mostra bande orizzontali di test, non splash e non le linee diagonali. Il percorso statico `drmtest` è pulito; **questo non prova ancora il kiosk**.
- Il binario pitch-fix statico AArch64 è sul host in `~/.hermes/profiles/kernel-re/cache/scratch/nx679j-kiosk2-pitchfix` e nel rootfs volatile del device come `/tmp/nx679j-kiosk2-pitchfix-staged`. SHA-256 letto indipendentemente su host e device: `1c840ddc8212cf4b9d91ff2a592edf743d2e6c4544342ed3086aaca2b5f0a7a6`. **Non eseguito**. Il test stride host ha dato RED sul codice precedente e PASS dopo la correzione.
- Non chiudere il DRM master corrente e non avviare un secondo client. Il reboot non è necessario per la prossima prova: si sta verificando un passaggio *in-place* sullo stesso DRM file mediante `pidfd_getfd`; nessun commit scanout ancora. L'eventuale commit live va tenuto a una sola proprietà `FB_ID` del plane primario attivo e senza flag `ALLOW_MODESET`; prima eseguire atomic TEST_ONLY; per il commit hardware chiedere conferma all'utente perché può glitchare il pannello o resettare il device.
- Nota anti-loop: `display-late.sh` scrive il marker rawdump a `seek=410`; la lettura live di quell'offset ha restituito `slot=378 uptime=1337.60` con il boot ID corrente (il logger `slot=378` usa l'offset `32+378=410`). Possibile collisione del marker, **da investigare prima di rendere persistente un lancio kiosk**. Per il test singolo la copia eseguibile è volatile e non può auto-ripetersi dopo un reboot.
- Logging verificato: BusyBox 1.37 `dmesg` non supporta `-w`; `/dev/kmsg` è leggibile e la sua ABI consente più reader indipendenti. Catturare stream sul host prima del commit, non affidarsi al rawdump volatile.

### Follow-up: test in-place senza chiudere il master (23/09)
- **Fatto live aggiornato (23/09, dopo i test):** boot ID `746a3fd8-47e2-40cd-979a-6af24b49cb41`, uptime 8636s; PID 24653 `nx679j-drmtest` resta master, fd3=`/dev/dri/card0`; CRTC152 attivo. Framebuffer 64 XR24 1080x2400 pitch4352 sul plane78 (plane-0); plane103 (plane-1) è inattivo.
- Webcam prima e dopo i test: `/home/user/.hermes/profiles/kernel-re/cache/scratch/nx679j-pidfd-pre-test.png` e `nx679j-atomic-testonly-after.png`; entrambe mostrano bande orizzontali rosse/verdi/blu/bianche pulite, non kiosk né LuCI.
- **Sonda read-only pidfd PASS:** helper AArch64 `pidfd-readonly-probe`; duplica fd3 di PID24653 via `pidfd_getfd`, risolve `/dev/dri/card0`, legge `msm_drm 1.4.0` e GETCRTC152→FB64/1080x2400x90cmd. Nessun SET_MASTER, cap o commit.
- **Dry-run atomico PASS, ma non è una prova visiva del pitch-fix:** helper `nx679j-atomic-fbid-testonly` SHA-256 `a5c7c13d9716a29eb1b1158383f7dd6c8f63a7d6737cbc1af7e35f7c747bdbd0`, uguale host/device. `CREATE_DUMB`+`ADDFB` XR24 restituiscono pitch 4352; `DRM_MODE_ATOMIC_TEST_ONLY` ha accettato il solo `FB_ID` (property 17) del plane78, senza `ALLOW_MODESET`. Il test non ha inviato commit hardware. La webcam resta uguale, FB64 resta attivo, FB temporaneo 57 non compare più nel debugfs: cleanup verificato.
- **Stato condiviso da ricordare:** per la prova il helper ha impostato `DRM_CLIENT_CAP_ATOMIC=1` sullo stesso `drm_file` di PID24653 (che implica anche `universal_planes`); il processo originale è vivo e in hold, non ha fatto altre ioctl. Nessun `DROP_MASTER`, `RMFB` o destroy di buffer attivi.
- **Ricerca completata:** 10 agenti + verifica diretta di Linux stable v5.10.66. `pidfd_getfd` dà un alias dello stesso `struct file`, quindi chiudere il duplicato non esegue il release finché PID24653 conserva fd3. L'alias ha comunque tutti i privilegi del master: usarlo solo col helper fidato e senza toccare mode/connector/ACTIVE.
- **Autorizzazione utente ricevuta (23/09):** un solo commit reale plane-only, già consumato: cambiare solo FB_ID sul plane78 attivo; niente MODE_ID, ACTIVE, connector, ALLOW_MODESET, flash o modem.
- **Commit eseguito:** helper `/tmp/nx679j-plane78-scanout-once 24653 3 152 78 64`; `ATOMIC COMMIT RETURNED 0`. UAPI ha cambiato FB_ID plane78 da 64 a 57, property 17, nuovo FB XR24 1080×2400, pitch=4352, dumb handle=2, size=10444800; request flags=0. Output salvato in `/home/user/.hermes/profiles/kernel-re/cache/scratch/nx679j-plane78-scanout-result-20260923.txt`.
- **Postcondizioni verificate** (stesso boot ID `746a3fd8-47e2-40cd-979a-6af24b49cb41`, uptime 11826s): PID24653 fd3 resta `/dev/dri/card0` e unico master; CRTC152 enable=1 active=1, modo `1080x2400x90cmd`; plane78 FB57 pitch[0]=4352; plane103 inattivo. SSH sopravvive.
- **Webcam dopo commit:** `/home/user/.hermes/profiles/kernel-re/cache/scratch/nx679j-plane78-after-commit.png`, vision: bande orizzontali rosso/verde/blu/bianco uniformi, senza glitch/diagonali/splash. Però è lo stesso pattern del vecchio FB64: non discrimina visivamente il nuovo scanout; il `FB_ID` kernel è cambiato, il contenuto effettivo latched dal pannello resta non provato.
- **Catena verificata nei log del commit (monotonic 11757.225s):** `disp_cc_mdss_byte0_clk_src`, `CMD_RCGR=0x1`, `CFG_RCGR=0x201`, sorgente richiesta `dsi0_phy_pll_out_byteclk`. In Linux stable v5.10.66 `clk-rcg2.c:update_config()` imposta `CMD_UPDATE`, polla 500 volte a 1µs e avvisa/ritorna `-EBUSY` se il bit non si cancella: questa è la prima failure osservata, ma la causa fisica del mancato ack è ignota ([fonte](https://github.com/gregkh/linux/blob/v5.10.66/drivers/clk/qcom/clk-rcg2.c#L100-L147)). Seguono byte-parent `-16`, pixel/link clock `-22` e `sde_encoder_prepare_for_kickoff` `resource kickoff failed`; il `-22` può essere cascata del reparent fallito oppure rate non valido, non è provato. Il log registra `sw_event=1`, `rc_state=4`; nel peer Waipio pubblico sono `KICKOFF` e `IDLE`, perciò il percorso è compatibile con l’uscita da IDLE, non prova l’effettivo power-collapse hardware. La sorgente peer `0280bdce...` (target NX679J incluso, kernel 5.10.101) ha byte0/pclk0 RCG con solo `CLK_SET_RATE_PARENT`; lo stack DSI ignora il ritorno del PLL-parent callback e continua al set-rate. Il commit vendor può tornare 0 pur con kickoff failure: `msm_atomic_commit_dispatch()` mette il worker in coda, fa flush nel caso blocking, ma non riceve l’esito interno; `sde_crtc_commit_kickoff()` è `void` e marca `needs_hw_reset`. Le sorgenti pubbliche sono comparatori, non il kernel live 5.10.66 esatto; la causa hardware resta aperta. Nessun fix SM8750 viene applicato per analogia.
- `dmesg` pre/post acquisiti sull'host (`before`: 255478 B, `after`: 255514 B). `/dev/kmsg` via `dd` ha dato un record e terminato; `logread -f` OpenWrt è stato verificato attivo e poi fermato, acquisendo 75683 B in `/home/user/.hermes/profiles/kernel-re/cache/scratch/nx679j-plane78-scanout-logread-20260923.log`. **Aggiornamento fase 2 (stesso boot):** il readback NON è vuoto — leggendo il nodo `esd_check_mode` del pannello in `/sys/kernel/debug/qcom,mdss_dsi_r6130_1080_2400_amoled_cmd/` il valore restituito è `esd_sw_sim_success`: il bypass ESD è **ATTIVO**. Il readback vuoto precedente era un artefatto del percorso di lettura (read corta/offset), come ipotizzato dai comparator; non era "stato ignoto". `nx679j-display-late.sh:90` usa `echo esd_sw_sim_success` (quindi newline) e il log dello stesso boot dice `ESD KILL ok`; ciò prova che il write è tornato positivo, non necessariamente che il parser target abbia cambiato stato. Comparatore pubblico Qualcomm/Nubia: `esd_check_mode` ha sia `.read` sia `.write`; il writer riconosce esattamente `esd_sw_sim_success\n`, ma in quel peer può restituire `len` anche per token non riconosciuti; il reader fa EOF con `*ppos != 0`, altrimenti `snprintf(buf, len, ...)` poi copia `len` byte (NUL-padding) e ritorna `len`. Una read molto corta può quindi apparire vuota: ipotesi di spiegazione, non prova sul BusyBox né sul live 5.10.66. Il pubblico NX679S pinned `0280bdce...` configura NX679J ma dichiara kernel 5.10.101; sorgente esatto NX679J 5.10.66 non trovato. Non riscrivere il controllo solo per il readback vuoto. Fonti: [`NX679S display-drivers.zip` (5.10.101)](https://github.com/ztemt/NX679S/blob/0280bdce975602ff17161f5de1b8fc63dc96bd47/display-drivers.zip), membro `dsi_display.c:1758–1907`; [Qualcomm 5.10 peer source](https://git.codelinaro.org/clo/la/platform/vendor/opensource/display-drivers/-/blob/ac17a22157f56a438e24009e4fa91a69100612ef/msm/dsi/dsi_display.c#L1706-1907). Questo ESD dubbio resta separato dal nuovo errore RCG→DSI kickoff.
- **Contenimento e snapshot live read-only (boot invariato, uptime 14964.50s):** PID24653 è ancora master su fd3 `/dev/dri/card0`; CRTC152 attivo `1080x2400x90cmd`; plane78 FB57 pitch4352; plane103 inattivo; SSH raggiungibile. `clk_summary` adesso riporta `dsi0_phy_pll_out_byteclk 0 0 0 59784484`, `disp_cc_mdss_byte0_clk_src 3 3 0 19200000`, `disp_cc_mdss_byte0_clk 1 1 0 19200000`, `disp_cc_mdss_pclk0_clk_src 2 2 0 19200000`, `disp_cc_mdss_pclk0_clk 1 1 0 19200000`. È una fotografia successiva, non lo stato del kickoff fallito e non spiega il timeout. Nessun rollback, secondo commit, reboot, scrittura partizioni o modifica modem; non inviare altre ioctl display senza nuova autorizzazione esplicita.
- **Fan-out di ricerca completata** (`deleg_a39317a6`, 10 assi): upstream v5.10.66 conferma l’handshake `CMD_UPDATE`/`-EBUSY`; il peer Nubia Waipio 5.10.101 conferma la catena DSI e rende compatibile il log con `KICKOFF` da stato software `IDLE`, ma non identifica la causa fisica né prova che il binario NX679J 5.10.66 abbia lo stesso codice. L’analogia SM8750 `CLK_OPS_PARENT_ENABLE` non è una fix applicabile senza verifica. Fan-out cinese/russa e ricerca esatta non hanno trovato una fix/report affidabile; non scrivere patch.
- **Consultazione GPT-6-Astra via `oneprovider` completata:** conferma indipendente che il primo guasto osservato è l’RCG update timeout, mentre la causa fisica resta non determinata; `ATOMIC flags=0` con ritorno 0 non prova il latch del nuovo FB; IDLE-exit è plausibile, non provato. Raccomandazione: solo se il guasto ricompare spontaneamente, catturare nello stesso istante log kernel timestampati + `clk_summary` e registri CMD/CFG RCG con un’interfaccia read-only sicura. Non riprodurre il guasto con un altro commit; non ho letto registri raw. Il singolo commit autorizzato è consumato.
- Helper scanout-once e rollback sono staged e non sono stati riutilizzati; il primo è AArch64 e SHA verificati. Il singolo commit autorizzato è consumato: niente ripetizioni, rollback o altre ioctl display senza nuova autorizzazione esplicita. La `clk_summary` odierna è solo lettura e non ricostruisce lo stato al kickoff; eventuale lettura raw di registri/regmap è un accesso hardware distinto, da valutare e autorizzare prima.

## 2026-09-23 FASE 2 — il link DSI è morto: prove read-only e nuova leva `idle_pc_state`

- **FATTO — il pannello mostra un frame stale, il link non gira.** Letture read-only, boot `746a3fd8-47e2-40cd-979a-6af24b49cb41`, uptime ~15650s:
  - `clk_summary`: `dsi0_phy_pll_out_byteclk` e `dsi0_phy_pll_out_dsiclk` con `enable_cnt=0 prepare_cnt=0`; `disp_cc_mdss_byte0_clk_src` e `disp_cc_mdss_pclk0_clk_src` a **19200000 Hz (XO/TCXO)**; i branch `disp_cc_mdss_byte0_clk`/`pclk0_clk` restano `enable_cnt=1`. `disp_cc_mdss_mdp_clk_src` 500 MHz con `enable_cnt=4`.
  - `/proc/interrupts`: riga `sde 4 Edge dsi_ctrl` = **3** totali dopo 4,3 h e **non avanza** in 3 s; `sde 12 dp_display_isr` = 0; `disp_rsc` = 2.
  - `/sys/kernel/debug/dri/0/crtc152/fps` = **0.0**; `fence_status` release fence **`commit_count=2 done_count=1`** (un commit non è mai completato); `connector56/release_fence` `commit_count=2 done_count=1`.
  - `/sys/kernel/debug/dri/0/debug/rm_status`: `blk:intf allocated:1`, `blk:ctl allocated:1`, `blk:pingpong allocated:2` → le risorse display sono ancora allocate.
  - `qcom,mdss_dsi_r6130_1080_2400_amoled_cmd/dsi-ctrl-0/state_info`: `CTRL_ENGINE = ON`, `COMMAND_ENGINE = ON`, `BYTE_CLK = 82049180`, `PIXEL_CLK = 109398906`, `ESC_CLK = 19200000` → il software crede che il link sia configurato mentre il clock framework dice il contrario.
  - Il pannello continua a mostrare le bande RGBW: è command-mode, tiene il frame nella sua GRAM.
- **Conseguenza:** il commit FB_ID 64→57 non è mai arrivato al pannello; le due webcam identiche non erano un limite della webcam, era il pannello congelato. Per verificare un nuovo scanout serve un **pattern visivamente diverso** e/o **movimento**, non un pattern uguale.
- **FATTO — mappa proprietà DRM verificata sul device** (sonda read-only `/tmp/nx679j-drmprops`, fd duplicato con `pidfd_getfd`, zero commit):
  - CRTC152: `19 OUT_FENCE_PTR`, `22 ACTIVE` range[0,1], `23 MODE_ID`, `153 input_fence_timeout`, `154 output_fence`, `165 vm_request_state` range[0,1,2], **`166 idle_pc_state` range[0,1,2] valore corrente 0**.
  - connector56: `20 CRTC_ID`, `2 DPMS` range[0,3], `47 RETIRE_FENCE`, `49 autorefresh` range[0,6], `50 bl_scale`, `51 sv_bl_scale`, `65 dsc_mode`, `66 frame_trigger_mode` range[0,1,2].
  - plane78 (**primario**, `type`=1): `17 FB_ID`, `18 IN_FENCE_FD`, `20 CRTC_ID`, `9-12 SRC_X/Y/W/H`, `13-16 CRTC_X/Y/W/H`, `79 zpos`, `80 alpha`; stato attuale SRC=1080×2400 dst=1080×2400+0+0, zpos=0, alpha=255.
  - Il modo corrente è riottenibile da `GETCONNECTOR(56)` (5 modi: 90/165/144/120/60cmd; quello attivo `1080x2400x90cmd`, clock 248410, type 0x8) e dal `GETCRTC(152)` (`mode_valid=1`, fb_id=57).
- **LEVA (da sorgente peer 5.10.101 `sde_crtc.c:4053`):** `if (idle_pc_state != IDLE_PC_NONE) sde_encoder_control_idle_pc(encoder, (idle_pc_state == IDLE_PC_ENABLE) ? true : false);` → impostare **`idle_pc_state = 2`** sul CRTC disabilita l'idle power collapse, cioè il percorso XO↔PLL che è fallito. Non ancora applicata; non provata sul binario live.
- **Toolchain (fatto nuovo e riutilizzabile):** su questo host **non** c'è più l'SDK OpenWrt in `/tmp`, ma `aarch64-linux-gnu-gcc -static` (glibc statico) produce binari che girano sul rootfs musl del device (verificato con hello-world e con le due sonde). Compilare sempre `-static` e verificare con `file`.
- **Prossimo passo (NON eseguito, serve autorizzazione esplicita):** un solo test di recupero sul master condiviso (pidfd_getfd, nessun kill/takeover), con due varianti già discusse: (A) commit singolo con `idle_pc_state=2` + nuovo FB distintivo; (B) ciclo modeset off→on (commit1 CRTC ACTIVE=0 + conn CRTC_ID=0 + plane CRTC_ID=0; commit2 cnp completo con MODE_ID reale + ACTIVE=1 + `idle_pc_state=2` + nuovo FB). Verifica prevista: fence `done_count` che avanza, PLL byte/dsi abilitati, contatore IRQ `dsi_ctrl` che sale, `fps` > 0, webcam con pattern diverso o in movimento (diff pixel fra due catture).
- **AUTORIZZAZIONE UTENTE (23/09, fase 2):** concessa per UN test di recupero sul master condiviso (fd duplicato con `pidfd_getfd`, nessun kill/takeover). Limiti dichiarati: max 2 commit atomici + eventuale flip loop breve (≤30 flip plane-only a 150 ms), nessun cambio di modo, nessun ALLOW_MODESET non necessario, nessun flash, nessuna modifica al modem. La variante (A `resume` o B `off`+`on`) la scelgo in base al parere GPT-6-Astra richiesto via `oneprovider`.
- **Armamentario pronto (host + device), NON eseguito:**
  - `/tmp/nx679j-display-resume` sul device, SHA `3a856d3c0c772ad3d4c1c0ce624faf1bf72c63e90913cffab066cd37aa4d5624` (identico all'host): varianti `resume` / `off` / `on` / `flip`, pattern **barre verticali R|G|B|W con fasce nere sopra e sotto** (inconfondibile rispetto alle bande RGBW orizzontali congelate).
  - `nx679j-p2-snapshot.sh` — snapshot read-only dei 5 segnali (clk/IRQ/fps/fence/state/esd) da prendere PRIMA e DOPO.
  - `nx679j-p2-motion.py` — prova oggettiva: N catture OBS + diff pixel (`mean_abs`, `% pixel>16`), verdetto MOVIMENTO/statico.
  - Stream log kernel già attivo su host: `nx679j-p2-logread-20260923b.log` (`logread -f`).
  - **Baseline pre-test (pannello congelato):** 3 frame a 1,5 s di distanza → `mean_abs=1.392/1.455`, `pixel>16=0.125/0.175%` ⇒ static (solo rumore). Dir: `nx679j-p2-webcam-pre/`.
- **Criterio di successo dichiarato:** dopo il commit, `fence_status` con `done_count` che avanza, `dsi0_phy_pll_out_byteclk/dsiclk` con `enable_cnt>0`, RCG byte/pixel a ~59,78/101 MHz, contatore IRQ `dsi_ctrl` che sale, `fps>0`, e `nx679j-p2-motion.py` che dichiara MOVIMENTO.

## FASE 3 — il commit di recupero ha bloccato il kickoff e il device si è RIAVVIATO da solo

- **FATTO, boot vecchio:** prima del commit il clock tree era **tornato sano** (PLL `dsi0_phy_pll_out_byteclk` `enable_cnt=1` a 82049180, `_dsiclk` 109398906, RCG alle stesse rate) mentre 20 min prima era a XO con PLL spento ⇒ lo stato "XO" era probabilmente l'**idle-collapse normale**, non un link morto. IRQ `dsi_ctrl` però fermo a 3 e `fps 0.0`.
- **FATTO, TEST_ONLY (con flag corretti):** passano (a) crtc+`idle_pc_state=0`+plane, (b) crtc+`idle_pc_state=2`+plane, (c) plane-only. Il driver **accetta** sia il cambio `idle_pc_state=2` sia il nuovo FB, **anche senza ALLOW_MODESET**.
- **BUG MIO, corretto:** `DRM_MODE_ATOMIC_TEST_ONLY` è **0x100**, non 0x02 — 0x02 è `DRM_MODE_PAGE_FLIP_ASYNC`, che il core atomico respinge con EINVAL. I primi tre TEST_ONLY erano invalidi (non dicevano nulla sul driver).
- **FATTO, commit reale (variante A autorizzata: `idle_pc_state=2` + plane78 → nuovo FB barre verticali, flags=0):** l'ioctl **non è mai tornato** (>120 s) e nel log dello stream host (`nx679j-p2-logread-20260923b.log`, monotonic 16208):
  - `[drm:_sde_encoder_phys_cmd_handle_wr_ptr_timeout:1550] [sde error]enc55 intf1 wr_ptr_irq wait failed, switch_te:0`
  - `[drm:_sde_encoder_phys_cmd_handle_ppdone_timeout:500] [sde error]enc55 intf1 pp:0 kickoff timed out ctl 0 koff_cnt 2`
  - `msm_drm … ctx:0, reg-dump logging …` + dump, ultime righe `xin halt:0x3fff0000, pnd err:0x0, src err:0x0`
  - e pochi secondi dopo il **device si è riavviato da solo**: nuovo boot ID `552db251-3148-436a-b8f2-4e91cc17d0db`, uptime ripartito da 0. SSH tornato subito disponibile; **`/sys/fs/pstore` VUOTO** (nessuna prova pstore).
- **Questo spiega il reboot "di causa ignota" della sessione precedente** (uptime 5134→77): stessa firma — commit display su pannello command-mode, kickoff che non completa, reset. Correlazione forte, meccanismo non provato (in ricerca).
- **LEZIONE OPERATIVA:** un commit atomico **bloccante** (flags=0) su questo pannello può restare appeso in kernel e portarsi dietro il device. D'ora in poi: `DRM_MODE_ATOMIC_NONBLOCK` (0x200) così l'ioctl ritorna subito, verifica via fence/log/telemetria, mai un commit bloccante "al buio".
- **Stato del boot nuovo (read-only, uptime 240 s):** display NON ancora inizializzato — è **normale**: `/tmp/display-late.log` dice `15:52:53 armato: attendo uptime >= 900`, quindi il launcher parte a uptime **~900 s** (il commento "~3000" in `/etc/nx679j-boot-services.sh` è superato dal gate reale a 900). `/etc/nx679j-boot-services.sh` documenta già che "l'rcS durante il bring-up wifi fa crashare il SoC (reset hardware)".
- **Volatilità confermata:** al reboot `/tmp` si svuota (`/tmp/nx679j-display-resume` non c'è più: va ri-scp). Il binario resta sull'host in `cache/scratch/` (SHA `52bc1395…` post-patch flag).
- **Ricerca obbligatoria avviata** (`deleg_fe1b27d5`, 9 assi): semantica `wr_ptr_irq`/`ppdone`, meccanismo del reboot, TE e `frame_trigger_mode`, DDIC vdtr6130, NONBLOCK+fence, debugfs/panic/pstore, fonti CN/RU, sequencing corretto per un pannello command-mode da userspace. Più un consulto Astra via `oneprovider`.
- **Nessun'altra azione display** finché non rientrano ricerca e parere Astra.

## FASE 4 — CAUSA RADICE IDENTIFICATA: il kernel NX679J è pre-fix "PLL prima del parent RCG"

- **PROVA DIRETTA sul kernel live:** `grep -c dsi_display_phy_pll_enable /proc/kallsyms` = **0** → il simbolo **non esiste**; esistono invece `dsi_display_phy_pll_toggle` e `dsi_clk_update_parent [msm_drm]`. Nel peer NX679S (5.10.101, post-fix) è proprio `dsi_display_phy_pll_enable` (`dsi_display.c:2893-2919`) la funzione che accende il PLL **prima** del reparent ⇒ il binario live è **pre-fix**.
- **Il commit che lo risolve (CLO):** `13d0d423af78` (2022-04-21) *"turn on the PLL before switching RCG parent during clk on"* — messaggio verbatim: *"we'll be setting PLL which is off as a parent to RCG that is on"*. Pre-fix il codice fa `set_clk_src(!prepare)` **prima** di `pll_toggle` e **sovrascrive `rc`**, scartando l'errore.
- **Meccanismo = esattamente il nostro errore:** il parent dell'RCG byte/pixel viene puntato a un PLL **spento** → `CMD_UPDATE` non si cancella → `WARN_CLK "rcg didn't update its configuration."` + `-EBUSY` (`clk-rcg2.c:111-134`) → `dsi_ctrl_set_clock_source` con rollback scartato → pixel/link clock `-22` → `sde_encoder_prepare_for_kickoff: resource kickoff failed`. `dispcc-waipio.c` ha **0 occorrenze** di `CLK_OPS_PARENT_ENABLE`: nessuna rete di sicurezza dal CCF.
- **Perché a volte funziona:** il sito del reparent è **unico**, ma viene **saltato** quando i clock sono già accesi (continuous splash, `dsi_clk_manager.c:639-642`) o in seamless DMS. Spiega tutto il quadro osservato: l'init al boot avviene con lo splash attivo → nessun reparent → funziona; poi bastano **58 ms** di idle (`IDLE_POWERCOLLAPSE_DURATION`, `sde_encoder.h:52`) per far collassare il link; da quel momento ogni commit deve fare il reparent rotto → o RCG `-EBUSY` (ioctl 0 ma frame non latched) o, con commit **bloccante**, kickoff appeso (`wr_ptr_irq`/`ppdone`) e **reboot**.
- **FIX senza toccare il kernel:** il client display deve **committare di continuo** senza mai lasciar passare >58 ms, e la **prima** commit deve avvenire mentre lo splash è attivo (come fa `nx679j-drmtest` al boot). Nessuna patch necessaria.
- **Lever userspace NON disponibile:** `clk_prepare_enable`/`clk_rate` scrivibili in debugfs esistono solo con `CONFIG_CLOCK_ALLOW_WRITE_DEBUGFS`, **assente** in questa build (verificato in `/proc/config.gz`: c'è `CONFIG_DEBUG_FS_ALLOW_ALL=y`, non il clock). Non possiamo accendere il PLL dal userland: la strada è comportamentale.
- **Altre ricadute utili:** `idle_pc_state` (CRTC, range 0/1/2, default 0) è l'unico modo userspace di disabilitare l'idle-PC ma va scritto **mentre il display è acceso**; `pidfd_getfd` sul master è solido (stesso `struct file`/`drm_file`) però `GETCRTC` non ha il flag master → un GETCRTC riuscito **non prova nulla**; un **DRM lease** creato via fd master è il modo legittimo di passare il display a un altro processo; per la UI, `cog -P drm` (WPE WebKit) è l'unico kiosk-browser pacchettizzato e richiede EGL/GBM **software** (`libmesa-llvmpipe`), alternativa senza GL `weston --backend=drm --renderer=pixman` + client wl_shm (nessun motore HTML).
- **Prossimo passo:** verificare che lo stack EGL/GBM software funzioni su SDE (`kmscube` senza master), poi costruire il client di boot che committa di continuo.

## FASE 5 — perché non si vede una console sul display + INCIDENTE (colpa mia)

- **Risposta alla domanda "non dovremmo vedere la shell OpenWrt sullo schermo?"** → **No, e non per una nostra dimenticanza.** Questo kernel vendor **non espone fbdev**: il display è solo DRM/KMS (techpack Qualcomm), non esiste `/dev/fb*`, quindi né il log kernel né un prompt hanno un percorso di rendering verso il pannello. Non abbiamo mai visto la shell perché **non esiste un driver che disegni testo** lì sopra. Per avere testo a schermo serve un client userspace che disegni in un framebuffer DRM **e continui a committare**. Buona notizia: il testo **non richiede GL/GPU** ⇒ è il traguardo intermedio più economico e robusto (`weston --backend=drm --renderer=pixman` + `weston-terminal`, oppure un renderer minimo con font bitmap + PTY). LuCI invece richiede un motore HTML: `cog -P drm` (WPE WebKit) con EGL/GBM **software** (`libmesa-llvmpipe`, installato e presente sul rootfs volatile).
- **INCIDENTE (errore mio, da non ripetere):** ho lanciato `kmscube` dando per scontato che `drmtest` tenesse il master. Ma il launcher display parte a **uptime 900** e la prova era a uptime ~600: **non esisteva nessun master**, quindi kmscube è diventato master all'open e ha fatto un **modeset reale** su crtc152. Sequenza nel log (monotonic 574):
  - `sde_rm_topology_get_topology_def:367 invalid arguments: rm:0 topology:0` + `_sde_encoder_phys_is_dual_ctl:760 invalid topology`
  - `sde_crtc_frame_event_work:2787 [sde error]crtc152 … invalid frame_pending:0` + `sde_fence_signal:434 extra signal attempt! done count:1 commit:1`
  - **154 fault SMMU** su `smmu_sde_unsec_cb`, iova `0xb8000000…0xb8003b00`, flags `0x24`
  - dopo 574.44 s i log periodici si fermano ⇒ device **bloccato**: SSH sia su USB `10.0.0.1` sia su WiFi `192.168.77.1` non esegue più comandi. Serve un **reboot/power-cycle dell'utente**.
- **REGOLA NUOVA (vincolante):** **mai** lanciare un client DRM (`kmscube`, `weston`, `cog`, `drmtest`, kiosk) in una finestra in cui **nessun master è attivo**: l'`open()` diventa implicitamente master e può fare un modeset reale (SMMU fault → device bloccato). Prima di ogni prova DRM: leggere `/sys/kernel/debug/dri/0/clients` e verificare il PID master; se il display non è inizializzato (uptime < 900) **non provare nulla**.
- **Nota di merito:** il root cause della FASE 4 (kernel pre-fix "PLL prima del parent RCG") resta valido e indipendente da questo incidente.

## FASE 6 — DESIGN approvato da Astra (gpt-6-astra via `oneprovider`) per il client persistente

- **Raccomandazione n.1:** il kiosk deve essere un **client PERSISTENTE che SOSTITUISCE il client statico di boot**, non un processo esterno che prende in mano un display già collassato. Così diventa **primo e continuo proprietario** dello scanout, avviato subito dopo l'init noto-buono mentre il display è sveglio. Non chiudere/abbandonare/killare il master, non fare takeover.
- **Regole tecniche accettate:** (1) ogni commit reale con **`NONBLOCK=0x200`**, un solo commit in volo, throttling sul completamento — NONBLOCK **non** è un watchdog: il lavoro in kernel può comunque bloccarsi; (2) cadenza ben sotto i 58 ms con immagine **distinta e in movimento** (un frame stale non deve poter sembrare successo); (3) **non toccare `frame_trigger_mode`** al primo test (default 0; il tentativo `posted_start` del passato non aveva migliorato nulla); (4) **non** includere `idle_pc_state=2` nel primo test: variabile separata, solo dopo una baseline nota-buona e con autorizzazione esplicita; (5) `TEST_ONLY=0x100` prima di ogni commit reale (validazione, non test di sicurezza).
- **Telemetria di successo (tutte, non basta l'ioctl 0):** immagine diversa/in movimento verificata via diff pixel; contatore IRQ `dsi_ctrl` che avanza; `fps>0`; release fence `done_count == commit_count`; PLL byte abilitato e RCG byte/pixel alle rate operative (non XO); zero nuovi errori RCG/DSI/`wr_ptr`/`ppdone`.
- **Contenimento:** se un commit NONBLOCK si blocca comunque → **smettere di inviare commit**, non killare il processo né chiudere l'fd master (il kill non annulla il commit in kernel e aggiunge rischio), raccogliere solo log/telemetria; nessuna recovery improvvisata. Un altro hang **non è garantito sicuro**.
- **Evidenza del reboot:** dopo un riavvio controllare `/sys/fs/pstore`, `/proc/last_kmsg`, `/proc/cmdline`, `/proc/bootconfig` (pstore vuoto = nessun record, non "nessun crash").
- **Piano operativo conseguente:** (1) client di boot = kiosk con pattern in movimento e flip continuo NONBLOCK → nuova immagine via `persist-tars` → build → flash; (2) stessa base con **testo da PTY** = shell OpenWrt a schermo (nessun GL richiesto); (3) poi LuCI con `cog -P drm` (WPE, EGL/GBM software).
- **Artefatto pronto (host): `nx679j-kiosk3.c`** → binario statico in `cache/scratch/nx679j-kiosk3`, SHA `11259b6b5921c2074d3cb72c13d3431d0d8429ce6d84ccdea13236bff8303cab`, compila pulito con `aarch64-linux-gnu-gcc -static -Wall -Wextra`. Deriva da `nx679j-kiosk2.c` con: **flip NONBLOCK (0x200)** + **`OUT_FENCE_PTR`** sul CRTC e attesa **`poll(fence, 100 ms)`** (un commit in volo, nessun blocco illimitato), **TEST_ONLY (0x500)** del commit completo prima di quello reale, telemetria ogni 300 frame (`fps`, `gap_max_ms`, `fence_timeout_tot`), **stop dopo 5 fence timeout consecutivi** (contenimento Astra: non insiste, resta vivo senza uscire). Pattern in movimento invariato (barra che scorre + mirino touch). NON tocca `frame_trigger_mode` né `idle_pc_state`.
- **Da fare appena il device torna su:** (1) power-cycle utente; (2) attendere uptime ≥ 900 e verificare `/sys/kernel/debug/dri/0/clients` + PID master (mai lanciare client DRM senza master!); (3) autorizzazione per l'immagine con il kiosk3 come client di boot (`persist-tars` → build → flash) e poi verifica con diff pixel + telemetria di Astra.
- **FATTO — recupero remoto impossibile dopo l'hang (verificato, non supposto):** `ssh root@10.0.0.1 'reboot -f'` e `ssh root@192.168.77.1 'reboot -f'` → connessione autenticata ma **nessun comando eseguito**; `ping` su entrambi gli IP: **100% packet loss** su 6 tentativi/30 s; `adb devices` vuoto e i descrittori USB del gadget `18d1:4ee7` mostrano **solo** funzioni di rete (CDC Comm 2/13 + CDC Data 10/0) → **nessuna interfaccia adb, nessuna CDC-ACM**; nessun `/dev/ttyACM*`/`ttyUSB*` sull'host. Lo stream `logread` lato host è morto da solo. ⇒ **unico recupero: tasto power fisico** (~15-20 s). Regola conseguente: dopo un hang display non riprovare le vie remote, chiedere subito il power-cycle e non contare sul watchdog PMIC.
- **FASE 7 — v88 FLASHATA e verificata:** `boot_b-v88-kiosk3.img` (md5 `742d7247…`) scritta su `/dev/sde41` con `dd oflag=direct` + `sync -f` + **2 letture fredde entrambe `742d7247…`**; riavvio eseguito. Immagine = v87 + `kiosk3` + launcher kiosk3-first con fallback `drmtest` (disattivabile con `/tmp/no-kiosk3`). Attesa dell'init display a uptime ≥900 in corso.
- **FASE 8 — CAUSA DEL REBOOT DIMOSTRATA (ricerca, 9 assi) e protezione applicata:** il riavvio spontaneo **non è un watchdog che scopre un hang**: è il driver display che chiama `panic()` di proposito. Catena: `_sde_encoder_phys_cmd_handle_ppdone_timeout` → `SDE_DBG_DUMP(0x0,"panic")` → `if (do_panic && panic_on_err) panic(__func__)` con **`DEFAULT_PANIC 1`**; e siccome il kernel ha `CONFIG_QCOM_FORCE_WDOG_BITE_ON_PANIC=y` + **`PANIC_TIMEOUT=-1`**, il panic fa scattare subito il **bite del watchdog APSS** → reset hardware. Ecco perché "reboot senza banner di panic": il banner non arriva sul canale catturato e il DT stock non ha `console-size` in ramoops ⇒ `/sys/fs/pstore` vuoto. **Mitigazioni trovate e disponibili:** (1) debugfs `dri/0/debug/panic` = 0 disarma il panic (i dump SDE diventano solo log) — **APPLICATO su questo boot (`prima: 1 → dopo: 0`)**; (2) registrare da userspace `DRM_EVENT_SDE_HW_RECOVERY` (è ciò che fa l'HWC AOSP) ⇒ il driver **notifica** invece di panificare → da mettere in v89. Altri fatti utili: `autorefresh` sul connector fa **uscire del tutto** il resource-control SDE dal percorso idle (secondo modo di evitare il collasso); l'API di test Qualcomm e l'HWC AOSP usano sempre **cnp completo + ALLOW_MODESET**, mai plane-only; `switch_te:0` significa "nessun campione TE fresco" (coerente con TE assente); la sonda di kiosk3 (NONBLOCK + OUT_FENCE_PTR + poll) coincide con quella raccomandata.
- **Stato al momento:** boot `412a759e-0935-48ce-b92b-64e49d2cf3c4`, **v88 confermata in esecuzione** (`/usr/lib/nx679j/modem/nx679j-kiosk3` presente, 882096 B), `panic knob = 0`, nessun client DRM e nessun `/tmp/display-late.log` (normale: il launcher parte a uptime ≥900). Nota: fra il riavvio post-flash e questo controllo il device si è riavviato **una volta in più** (uptime ripartito), causa non determinata — coerente con l'instabilità documentata del bring-up wifi nei primi ~90 s.
- **FASE 9 — DISPLAY VIVO ✅ (v88):** il blocco era il marker rawdump slot 410 = `DL-FIRED 552db251` (boot dopo il mio incidente kmscube) ⇒ dal mio hang il display non veniva inizializzato affatto e io lo leggevo come "normale". Azzerato → kiosk3: `TEST_ONLY rc=0`, `commit 1 OK`, `fps 45.4`, `gap_max 23 ms`, `fence_timeout 0`, **`fence done=993 commit=993`**, IRQ `msm_drm` 4425→4835 in 3 s, PLL byte **59.78 MHz** + dsiclk **79.71 MHz**, webcam **MOVIMENTO** (`mean_abs 10.1` vs `1.39` congelato). Touch confermato dall'utente.
- **FASE 10 — v89 flashata:** `boot_b-v89-hardened.img` md5 `2f9e5b74…` = v88 + **disarmo del panic SDE nel launcher dopo il mount di debugfs**; 2 letture fredde ok; boot `e2ec9d2a…`, marker azzerato, launcher v89 presente. Modem: il reboot ha ripristinato la **catena automatica** (1 modem, 2 iface, `cell ping rc=0`, chain `DONE`); serve solo il reboot quando `mmcli -L` è 0.

## FASE 11 — VELOCITÀ DI BOOT: da ~315 s a ~135 s (misurato)

- **Timings finali:** catena modem `DONE` **65 s** (da 265), modem+dati (`ping OK`) **~135 s** (da ~315), kiosk3 **~150 s** (da ≥900). Ultima immagine: md5 `37d450c8e90f034955e64745327fcecc` (`boot_b-v90-mmwd.img`), verificata con doppia lettura fredda.
- **Cosa ha reso:** (a) bootstrap dinamico su **risposta QMI reale** (86+15 s ciechi → 10+1 s misurati); (b) margine pre-rcS 60→10 s; (c) waiter che aspetta il rproc MSS + **staging symlink `/tmp` anticipato** + catena a 37 s; (d) **`DISPLAY_LATE_GAP` 250→10 s** (era l'attesa vera del display, non la gate); (e) gate display 900→120 s; (f) `mmcli --scan-modems` periodico.
- **Cosa resta:** ~40-70 s dentro il **probe di ModemManager**, con **varianza ~90 s tra boot** → serve una campagna di 4-5 boot strumentati prima di toccare altro (skill §9). Candidato: portare il bearer fuori da MM (la catena lo crea già a livello QMI).
- **Correzioni di metodo (mie):** v101 e v102 annullate perché giudicate da un singolo boot (erano rumore); il `log` non definito in mm-standard-boot è cosmetico; il waiter sembrava "morto" solo per il troncamento a 15 caratteri di `ps` busybox (`{nx679j-mm-watch}`); la catena a 37 s senza staging **fallisce silenziosamente** (helper mancanti) e ritarda anche il tentativo buono (modem a 231 s).

## ►► PUNTO DI INGRESSO UNICO PER RIPRENDERE: `RIPRESA.md` (stesso dir)
Contiene: cosa dove sta, i numeri misurati, il primo passo (campagna 4-5 boot sulla fase MM), la procedura di flash verificata, la recovery (fastboot/GOOD) e la lista delle trappole pagate. **Leggerlo prima di toccare il device.**


## FASE 12 — 25/09/2026: display, cella+CA, TASTO LATERALE, metodo

- **Sovrapposizioni display CHIUSE (v158).** Causa reale: ogni funzione di pagina scriveva il proprio titolo in blu a y=200 scala 4 (200..264) DENTRO la barra dei tab (170..258): 7 titoli rimossi. Prima sbagliai tre bersagli (contatore, riga topbar, operatore pagina modem): la lezione e' che cercare ogni uso dell'attributo distintivo (grep per colore) batte il correggere il primo sospetto. Altre: barra tab su UNA riga, contenuto sotto la barra, ridisegno a ogni cambio dati (prima si ridisegnava solo al tocco: schermo fermo), anti-residuo sul vetro (sfondo alternato di 1 LSB).
- **Cella + carrier aggregation (v166, verificati).** `nx679j-cell.sh` produce `cell.*` (banda, earfcn, bw, pci, rsrp, rsrq, rssi, snr, plmn, tac, gid, ta, vicine) e `ca.*` (totali, attive, dl, pcc.*, scc1.band/earfcn/pci/bw/state/rsrp/stima). **RSRP per portante = incrocio del PCI** con la cell-location. Il contributo per portante NON e' misurabile (in UI etichettato "stima"). Il fetcher lo chiama ogni 10 cicli con cache in /tmp/ui-cell.txt.
- **qmicli COMPLETO (v165).** Quello di OpenWrt e' compilato con la collection basic: 11 comandi NAS, senza `--nas-get-lte-cphy-ca-info`. Ricostruito dall'SDK senza toccare .config (il default di libqmi e' full; il defconfig sceglie basic), installato nell'immagine (stessa 1.36, stesso SONAME: qmi-network non se ne accorge). Prima: disattivare sysprof in glib2 (`-Dsysprof=disabled`), altrimenti glib non compila (libunwind-generic assente nel feed).
- **MODULI VENDOR: la scoperta che spiega tre problemi in uno.** Nella chroot nessuno fa autoload (manca l'helper uevent): i driver non caricati dal boot vendor non arrivano mai e tutto SEMBRA non supportato. `nx679j-modules.sh` (7 .ko in /usr/lib/nx679j/modem/kmod/, ~250 KB, chiamato dal launcher) carica la catena PON (qcom-pon, NON "qpnp-power-on": il sorgente si chiama cosi', il modulo no) + pm8941-pwrkey + pmic-pon-log, e la catena glink (qti_battery_charger, charger-ulog-glink, ucsi_glink). I nodi /dev/input/event* si creano leggendo il numero di device dal kernel: l'ordine CAMBIA a ogni avvio (pwrkey puo' essere event0 o event2); la UI scansiona e sceglie per capacita', ed e' questo che l'ha salvata.
- **TASTO LATERALE FUNZIONANTE (verificato).** pmic_pwrkey su /dev/input/eventN con **KEY_POWER (bit 116)**; pmic_resin = VOLUME UP (114). Il kernel conta le pressioni (/proc/interrupts: pmic_pwrkey 6, pmic_resin 2) e la UI le riceve (log: "tasto: /dev/input/event0 aperto (fd=5)").
- **STANDBY DISPLAY: IN CORSO, non finito.** Ricerca (8 assi): si spegne con CRTC/DPMS off (DCS 0x28+0x10, rail off): frame nero e luminosita' 0 NON spengono nulla e non risparmiano energia. Il collasso a 58 ms NON si applica a pannello spento. **Al risveglio serve un modeset completo con idle_pc_state=disable nello STESSO commit** (il driver lo forza a enable durante il disable: senza ripristino il pannello collassa dopo 58 ms). Pannello = Raydium R6130 (off-command e reset-sequence nel DTS vendor). Implementato in nx679j-ui (v168..v170): `commit_off()` = piano staccato + crtc non attivo + connettore staccato; `standby_exit()` = `commit_cnp(fb,0,2)`; guardia `dirty && !standby` nel loop (nessun commit a pannello spento: sarebbe un no-op silenzioso che finisce in kickoff timeout/watchdog); comando di servizio /tmp/ui-standby per provare da remoto. **BLOCCATO su EINVAL (-22) che arriva PRIMA di ogni log**: soddisfatta la regola "o entrambi CRTC e FB, o nessuno" (p_fbid=0 insieme a p_crtcid=0), connettore staccato, flag e struttura verificati sul sorgente. Prossimo: far parlare il driver SDE vendor (non logga con drm.debug=0x1f) oppure passare alla via DPMS via DRM_IOCTL_MODE_OBJ_SETPROPERTY (il kernel costruisce lui il commit).
- **ORACOLO ANDROID pilotabile da qui.** `reboot-bootloader` (binario nostro, syscall RESTART2) => fastboot su USB (unlocked: yes, slot-count: 2, product: taro); cambio slot e ritorno verificati. In Android: **13 dispositivi input** (noi 1 prima dei moduli), **stesso identico kernel** (5.10.66-android12-9-00005-gf6e6376090be-ab8060604), gadget USB **ncm,adb** (= rete + adb sullo stesso cavo: il modello che usiamo anche noi).
- **Alimentazione: carica ma NON e' controllabile.** qti_battery_charger si carica ma non espone /sys/class/power_supply su questo setup. La carica e' autonoma nel PMIC (funziona da giorni); non possiamo leggere i mA ne' alzare il limite. "Non carica" sarebbe falso: "non e' regolabile".
- **REGRESSIONE MIA, corretta:** lo script dei moduli ricaricava pmic_glink quando mancava power_supply — condizione permanentemente vera => ricarica a OGNI avvio => sessione dati abbattuta (rmnet_data0 senza indirizzo, ping KO). Rimosso. **Regola: un rimedio automatico va eseguito una volta sola, o protetto da una condizione che puo' essere vera SOLO quando il guasto c'e'.**
- **METODO aggiornato** (skill kernel-debug-validate, ref false-absence-triage): "assente" non e' un risultato finche' non ho verificato percorso, nome, strumento e prerequisiti. Cinque falsi negativi nello stesso giorno (device tree, nome del modulo PON, collection di qmicli, stderr soppresso, opkg vs apk). Corollari: mai sopprimere stderr in diagnosi; chiedere a dmesg/log/sorgente; il verdetto dell'oracolo.
- **Protezioni di flash (utili due volte oggi):** niente scrittura se la compilazione non e' pulita, se l'immagine e' identica alla precedente, o se il backup non combacia col valore atteso (da aggiornare a ogni flash col contenuto reale del device).
- **STATO:** v170 in flash (md5 af0dad4523d1ec0738dcea3bf8ff5859, WRITE_GATE_PASS, doppia lettura). Sani: rete, UI (45.4 fps, fence_timeout 0), tasto agganciato, moduli automatici, 217 chiavi. Aperto: standby display (EINVAL) e controllo alimentazione.
- **PROSSIMO PASSO:** far parlare l'SDE (debug del driver vendor) oppure passare alla via DPMS; poi il collaudo del tasto da parte dell'utente.

### Aggiornamento standby (25/09 sera, v171)

- Provata anche la via **DPMS** (`DRM_IOCTL_MODE_OBJ_SETPROPERTY` 0xBA, valore 3, connettore 0xc0c0c0c0;
  `find_prop_global("DPMS")` la trova: cerca per nome su tutti gli id). **Stesso EINVAL (-22).**
- **Conclusione**: la via DPMS e il commit atomico scritto a mano **convergono sullo stesso commit
  interno** (`drm_atomic_connector_commit_dpms` costruisce un commit atomico) => **non e' un problema
  di composizione del commit: e' il driver SDE vendor che rifiuta la disattivazione del CRTC.**
  Prossimo passo obbligato: far parlare l'SDE (il suo debug non e' `drm.debug`) oppure provare
  una forma di disattivazione diversa.
- **Comportamento osservato durante il tentativo**: i frame si FERMANO (4200 -> 4200) e la rete resta su
  (ping OK), poi i frame ripartono (4800). Cioe' il CRTC va giu' MA la UI resta in stallo sui commit
  no-op: **non e' uno standby pulito**, e non va considerato tale.
- **Protezione documenti attiva**: `doc-backup.sh` agganciato a `build-v90.py` => snapshot datato dei
  documenti del progetto a OGNI build (in `docs-snapshots/`). Verificato nella build v171.
- **STATO: v171 in flash**, md5 `58135186bb376ea23d437d29863237f7` (WRITE_GATE_PASS, doppia lettura).

### INCIDENTE 25/09 (sera) — lo standby lasciava il CRTC a meta' e bloccava la UI

- **Sintomo riferito**: touch e tasto morti entrambi; poi precisato dall'utente: *"all'inizio funziona e poi
  smette"* — il dato che ha permesso la diagnosi (grazie a quello, non alle mie misure).
- **Causa**: il tentativo di spegnimento (DPMS e commit atomico: ENTRAMBI falliscono con EINVAL) **lascia il
  CRTC a meta'**: il display si spegne (frame fermi) ma l'ioctl esce in errore, quindi il codice crede di
  aver fallito mentre lo stato e' compromesso. Da li' **ogni commit successivo si incastra** (no-op
  silenzioso del vendor) e il ciclo della UI si blocca: touch e tasto sono letti nello STESSO loop, quindi
  muoiono insieme. Nota: `commit_cnp` NON usa NONBLOCK, percio' il blocco e' definitivo.
- **Secondo fatto**: `kill -9` sulla UI bloccata **non la uccide** (ioctl non interrompibile, stato D):
  l'unica uscita e' il riavvio del sistema. Il launcher nel frattempo ha correttamente rifiutato di avviare
  una seconda istanza (`altra istanza gia' attiva: esco`) — il controllo anti-doppione ha evitato il peggio.
- **RIMEDIO APPLICATO (v172)**: **innesco dello standby DISATTIVATO** — il tasto registra la pressione nel
  log e non tenta piu' lo spegnimento. Il codice dello standby resta (ricerca, DPMS, commit atomico) ma
  non e' armato: una funzione che uccide l'interfaccia e' peggio di una funzione che manca.
- **REGOLA NUOVA**: una funzione che parla col kernel e puo' bloccarsi **non va armata finche' il suo
  percorso di errore non e' provato**; e un commit DRM su stato non noto va fatto NONBLOCK con timeout.
- **STATO: v172 in flash**, md5 `c503bbef81de11751b2bdf4bf9fd0b0c`. Device verificato sano: UI 45.4 fps,
  touch+event0/event2 aperti, rete OK, 217 chiavi.

### TOUCH MORTO 25/09 sera — serve un ciclo di alimentazione VERO

- **Sintomo**: il touchscreen non genera piu' interrupt (`/proc/interrupts` IRQ 395 fermo a 3, solo init).
  La UI e' sana (frame che avanzano, processo in stato S, input aperti): **il guasto e' a monte della UI**.
- **Cosa NON basta**: riavvio software (warm) — il chip resta bloccato; `kill -9` della UI non c'entra.
- **Cosa ho provato (e l'esito)**: `echo <dev> > /sys/bus/platform/drivers/nubia_goodix_ts/unbind` =>
  **il kernel e' CRASHATO e il device si e' riavviato da solo** (connessione SSH reset, uptime azzerato).
  Il driver del touch e' fragile sulla remove: NON ripetere l'unbind.
- **Cosa serve**: un ciclo di alimentazione VERO = **pressione lunga del tasto laterale (~15 s)**,
  che fa scattare il reset hardware del PMIC (taglia e ripristina i rail). E' l'unico modo di
  riavviare il controller del touch dopo questo tipo di blocco.
- **Origine probabile**: i miei esperimenti sui moduli PMIC (`pmic_glink` ricaricato) hanno potuto
  glitchare i rail del touch. Non ne ho la prova: lo dico come sospetto, non come fatto.
- **Da fare al risveglio**: verificare il delta di `/proc/interrupts` (IRQ nubia_goodix_ts) con un dito
  sullo schermo: se sale, il touch e' tornato.

### ESITO TOUCH 25/09 (notte) — GUASTO HARDWARE, non software

Catena di prove, tutte misurate sull'IRQ `nubia_goodix_ts` di `/proc/interrupts` (valore di init ~3):

| prova | esito |
|---|---|
| riavvio software (piu' volte) | touch morto |
| ciclo con reset da tasto | touch morto |
| driver ricaricato (unbind => **kernel CRASH**, device riavviato da solo) | touch morto |
| **Android stock (slot A)** | touch morto — stesso IRQ fermo a 3 |
| **spegnimento VERO** (`reboot -p` da Android, rail tagliati, ping 100% loss confermato) | touch morto |
| display, PMIC, I2C, binding dei driver | tutti sani |

**CONCLUSIONE: guasto hardware del controller del touch.** Le mie modifiche (moduli vendor PMIC, esperimenti standby)
sono **ESONERATE**: il sintomo e' identico su Android stock, dove non esistono. Ho accusato a torto i miei moduli e
lo lascio scritto.

**Cosa NON fare (imparato):** `echo <dev> > /sys/bus/platform/drivers/nubia_goodix_ts/unbind`
=> **il kernel crasha** (riavvio immediato del device). Il driver del touch e' fragile sulla remove.

**Cosa NON basta:** un riavvio (anche il reset da pressione lunga) **non taglia i rail delle periferiche**:
il touch resta alimentato e conserva il suo stato. Serve lo **spegnimento vero** (`adb shell su -c 'reboot -p'`,
oppure la voce "Spegni" di Android). Procedura valida in generale per le periferiche che sembrano "morte".

**CONSEGUENZA OPERATIVA:** il touch e' perso => la UI su display non e' piu' navigabile a dito.
Restano: rete/modem (lo scopo principale), display come monitor, SSH, **LuCI via browser**, e il **tasto laterale**
(ora l'unico input fisico!). Da valutare: navigazione della UI con i tasti volume (up/down = scorri/seleziona).

**DA FARE al prossimo avvio**: riattivare i moduli vendor (rinominare `nx679j-modules.sh.off` -> `.sh`):
erano disattivati solo per il test, sono innocenti, e senza di loro il **tasto laterale non esiste** — e ora il tasto
e' l'unico input. (Il caricabatterie resta inutile: si puo' togliere dalla lista.)


---

## 2026-09-25 (sera) — TASTI + STANDBY: CHIUSI E VERIFICATI

### Il bug che spiegava mezza giornata: now_ms() in overflow
- `ui-1-base.c`: `now_ms()` = `(int)(tv_sec*1000+tv_usec/1000)` -> nel 2026 overflow int -> valore NEGATIVO.
  - i gate dei tasti non scattavano MAI: i device dei tasti non venivano aperti;
  - l'anti-tocco-fantasma SCARTAVA ogni tocco prima di eseguirlo.
- Fix: `clock_gettime(CLOCK_MONOTONIC)`. L'harness host non lo vedeva perche' non chiamava `key_scan()`:
  ora ha una prova di regressione che fallisce sul codice vecchio (9 FAIL) e passa sul nuovo.

### Tasti laterali (verificati dall'utente)
- volume giu' = scorre la ghiera arancione; doppio click = attiva; lungo >=1.5 s = indietro.
- VOLUME SU: NON esposto al kernel su questo telefono (`pmic_resin` dichiara solo il bit 114).
- Il nodo del tasto consegna DUE esemplari per click: il secondo riarmava lo standby ->
  guardia di 600 ms al risveglio (binario 5422c5ad).

### STANDBY (il requisito vero: spegnere lo schermo senza burn-in)
- VIETATO spegnere il link: il DPMS-off su questo tree uccide il DSI command link
  (`failed wait_for_idle: -110`, `wr_ptr_irq wait failed`): i nodi sysfs restano "a posto"
  ma il pannello e' nero e non risorge senza riavvio.
- Il design giusto e' quello di Android (`SetDisplayState: state=0, teardown=0`):
  **frame NERO + backlight 0, link VIVO**. Su AMOLED i pixel neri sono spenti -> zero burn-in.
- v180 (`5a8ca8b8`): standby = committa un frame nero + backlight 0; risveglio = backlight
  ripristinato + ridisegno. Il loop continua a committare frame neri (link esercitato).
  VERIFICATO CON LA WEBCAM: off -> schermo nero; on -> interfaccia viva con dati live.
- Comando di servizio: `/tmp/ui-standby` (toggle senza toccare il tasto).
- Ricetta (mai usata) per un eventuale link morto: evento SDE hw-recovery + detach/attach modeset.

### Touch
- La UI ora ACCETTA i tocchi (dimostrato col device sintetico). Se il controller non emette
  eventi (IRQ 395 statico, 0 byte su eventN), il confine e' nell'hardware.
- MAI toccare i nodi del driver `fwupdate/result`, `get_rawdata`, `esd_info`: fanno crashare il kernel.

### Metodo (le regole che hanno salvato la giornata)
1. WEBCOM obbligatoria per lo stato dello schermo (l'utente lo pretende): 1920x1080 + raffica di frame.
2. Verificare l'artefatto DENTRO l'immagine (estrazione dal ramdisk), non quello sul disco.
3. Uptime per i riavvii, mai il comando. Un commit su CRTC spento -> stato D (kill inutile, solo riavvio).
4. L'ORACOLO (Android stock) e' la fonte quando l'API del kernel porta fuori strada.
5. Il gate del flash che confronta il backup ha fermato un flash su premesse sbagliate: tenerlo.

### Immagine corrente
- Ultima flashata: v180, immagine md5 `f8d1324f...`, UI `5a8ca8b8...`, verificata end-to-end.

---

## 2026-09-27 (notte) — IL "PROBLEMA DELLE 4 ORE": CHIUSO (v181)

### Sintomo riportato
Cambio IP delle ~4 ore; al tentativo di riconnessione il pulsante "riconnetti" della UI non faceva nulla.

### Diagnosi (due strati)
1. **Il modem era davvero incastrato**: `dpm/wds` vivi ma muti (nessuna risposta alle query),
   `qrtr_tx_wait` nei log kernel, data stall IPA. Il renew del pulsante moriva sulla query di
   prontezza -> "non l'ha fatto". Recovery: stop/start MSS -> il device e' ripartito (riavvio),
   la catena del boot ha riportato su modem e IP da sola.
2. **CAUSA A MONTE — il probe del link-watch era cieco**: `ping 1.1.1.1` NON risponde
   all'ICMP da questa rete (misurato: 5 target, solo 1.1.1.1 KO; 8.8.8.8 / 8.8.4.4 / 9.9.9.9 /
   208.67.222.222 tutti OK). Il watcher vedeva "guasto" con internet PERFETTO ->
   renew ogni ~74 s -> **1641 rinnovi in ~2 giorni** -> churn che ha incastrato il firmware.
   Anche l'indicatore della UI (`nx679j-ui-sample.sh`) mostrava PING_KO falso.

### Fix (v181, immagine md5 `5e941a821c4fb12ef336258bd865c29f`)
- `nx679j-link-watch.sh`: probe 1.1.1.1 -> 8.8.8.8 (2 righe: probe periodico + ricontrollo a 5 s).
- `nx679j-ui-sample.sh`: indicatore ping 1.1.1.1 -> 8.8.8.8.
- Nient'altro. Gate del flash (diff membro-per-membro dei due ramdisk cpio, 3721 membri):
  differenze di CONTENUTO solo i 2 file (8+4 byte); il resto solo metadati (ino/mtime).
- Flash col protocollo forte: scp -> `dd oflag=direct conv=fsync` -> `sync -f` ->
  2 letture fredde (entrambe = md5 atteso) -> riavvio.
- POST-BOOT verificato: file patchati sul device, watcher attivo, `link-health: up 123 fails=0`
  (primo probe OK), rmnet_data0 10.180.246.58/30, ping 8.8.8.8 = 36 ms.

### Metodo / lezioni
- **Un probe di liveness su UN SOLO target e' fragile**: se quel target e' filtrato, il watcher
  diventa un generatore di guasti. Il modello mwan3track usa PIU' host: miglioria da valutare
  (probe multi-target, guasto solo se TUTTI KO).
- Sintomo "il riconnetti non fa nulla" = modem incastrato, non problema di UI: controllare
  sempre prima WDS/DMS/qrtr_tx_wait.
- I riavvii per applicare il flash vanno attesi su `/proc/uptime`, MAI sul ping (un ping perso
  mi ha fatto innescare un riavvio doppio: innocuo, ma da non ripetere).

### Immagine corrente
- Ultima flashata: v181, immagine md5 `5e941a82...` (file `boot_b-v90-mmwd.img`, 27/09 22:56).
  Backup della precedente: `boot_b-current-v180.img` (md5 `f8d1324f...`).
