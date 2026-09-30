# Modem X65 su NX679J — catena, MM standard, recovery

## Architettura attuale (v84+)

1. **Catena custom** `S95«redacted»-modem` → `chain.sh` (stages, log `/tmp/chain.log`): bootstrap MSS → READY → dms (mode 5→0) → prepare → dpm → ingress → data (egress/wda/mux/wds) → cell → **DONE** con `criteria=1`.
2. **mm-standard-boot.sh** (il boot script, dal ramdisk): aspetta il DONE della catena (fino a 8 min) → ferma la WDS della catena → inietta l'evento `rmnet_ipa0` → **MM restart forte** (`kill -9` + 10s + start) → attesa probe → `ifup modem` → ping.
3. **ModemManager 1.24.0** (patch 0100–0204) + **libqmi** (patch 100–106) + `netlink-watch` v3 (`S71`) + proto `modemmanager` standard.

## Log atteso di un boot sano
```
[269] chain: 1 done-marker(s)
[269] wds-session della catena fermato (pid 11759)
[272] evento rmnet_ipa0 iniettato
[306] OK: iface modem UP (inet 10.96.49.67/29 ... qmapmux2_1)
[307] OK: ping 1.1.1.1
```
Se `chain.log` dice `STOP bootstrap` / `MSS=offline` → il boot auto è saltato: procedura di recovery (sotto).

## Recovery (procedura verificata più volte)

**Caso A — il MSS non è registrato affatto:**
- sintomo: `dmesg | grep mss` → `remoteproc remoteproc0: releasing 4080000.remoteproc-mss` a ~19s; `/sys/class/remoteproc/remoteproc3` = `spss` o assente (5 remoteproc invece di 4+).
- **causa**: strascico di riavvii bruschi (crash display ecc.).
- **fix**: un reboot pulito → il MSS torna registrato. **NON fare rebind sysfs** (`echo ... > /sys/bus/platform/drivers/.../bind`) — su questa unità ha CRASHATO il device.

**Caso B — il MSS c'è (offline) ma la catena è saltata:**
```sh
D=$(ls -d /usr/lib/*/modem)
for f in "$D"/*; do b="${f##*/}"; [ -e "/tmp/$b" ] || ln -s "$f" "/tmp/$b"; done
setsid sh "$D/chain.sh" > /tmp/chain.out 2>&1 &
# poll di /tmp/chain.log fino a DONE (bootstrap ~1-3 min)
# se si ferma con "STOP dms": aspettare READY_FOR_OBSERVED_DMS_TEST nel bootstrap log,
#   poi rilanciare: sh "$D/openwrt-dms-observed-check.sh" && di nuovo chain.sh
# poi il boot script (se vivo) prosegue: wds stop → inject → MM restart → ifup
```

## Riconnessione automatica (WINDTRE ~4h)

Meccanismo STANDARD: `/usr/lib/ModemManager/connection.d/10-report-down` cerca l'interfaccia UCI con `option device == modem.generic.device` (= `qcom-soc`). Config necessaria in `network.modem`:
```
uci set network.modem.device='qcom-soc'
uci set network.modem.force_connection='1'
uci commit network
```
Testato: `mmcli -m any --simple-disconnect` → `hotplug: Reconnecting 'modem' on 'disconnected' event` → nuovo IP in pochi secondi. (Senza `device` → netifd non riconnette mai.)

## Patch (nel SDK)

**libqmi** (`feeds/packages/libs/libqmi/patches/`): 100 fix netlink hdr, 101 ifinfomsg azzerato, 102 sendto kernel, 103 sock raw, 104 hexdump debug, 105 mask=flags, **106 probe mux EBUSY** (create+delete di prova per ogni mux candidato — risolve i mux fantasma).

**ModemManager** (`feeds/packages/net/modemmanager/patches/`): 0100 filter net dev virtuali, 0101 skip dataformat QRTR, 0102 rmnet come data port, 0200/0201 net_driver=ipa, 0202 link port timeout 10s, 0203 grab link port via iflink, 0204 nome qmapmux senza punto.

Build: `cd $SDK && make package/feeds/packages/{libqmi,modemmanager}/compile V=s`.

## Comandi utili (device)
```sh
mmcli -L; mmcli -m any | head -20
ubus call network.interface.modem status | head -12
ifup modem / ifdown modem
cat /tmp/chain.log | tail -10
ps w | grep -E "[c]hain|[M]odemManager" | head
grep -nE "Creating RMNET link|dynamic mux|connection attempt" /var/log/mm.log | tail
```

## Fatti chiave
- `rmnet_ipa0` = PHYSDEV_UID; `rmnet_data0` = IGNORE (regola 80-mm).
- Una sola WDS per porta EMBEDDED: la WDS della catena va killata prima del connect MM.
- Il primo probe MM del boot scarta il modem (rmnet_ipa0 non esisteva): serve il restart forte.
- `strerror(-16)` su musl = "No error information" (EBUSY mascherato — era il mistero dei mux).
- Il mux >= 10 è rifiutato dal vendor.
- WAIT_LINK_PORT_TIMEOUT_MS=2500 in MM: l'evento netdev deve arrivare entro 2.5s (per questo esiste netlink-watch).

## Chi scopre un netdev tardivo (dettaglio in `references/netdev-hotplug.md`)
- MM è buildato `-Dudev=false`: **non ha nessun listener uevent**, l'unico ingresso porta è D-Bus `ReportKernelEvent`
  (via `/etc/hotplug.d/net/25-modemmanager-net` → `mmcli`, oppure diretto). sysfspath va omesso: MM risolve `/sys/class/net/<nome>`.
- L'uevent del kernel esiste sempre, una volta, a sysfs completo; se manca la notifica il problema è a valle (timing MM, filtro virtuali, mmcli fallito, cache evento persa), non nel kernel.
- Sintesi sicura = `echo add > /sys/class/net/<if>/uevent` (stessa primitiva del coldplug di procd); l'iniezione netlink raw di `netlink-watch3.c` è **morta** (`nlmsg_type=0` = NLMSG_NOOP → scartata dal kernel, payload troncato al primo NUL, DEVPATH hardcoded). Annunciare il physdev (`rmnet_ipa0`/`qmapmux*`), mai `rmnet_data0`.
