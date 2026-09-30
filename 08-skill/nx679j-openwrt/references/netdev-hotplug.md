# Hotplug netdev: chi scopre cosa, e come sintetizzare un evento

Report completo con evidenze: `nx679j-stock/experiments/refs/session5-20260920/HOTPLUG-NETDEV-MECHANICS.md`.

## Catena reale (immagine con MM build `-Dudev=false`)

1. Kernel: `register_netdevice()` → `netdev_register_kobject()` sopprime l'uevent, `netdev_uevent_add()` lo emette
   **una sola volta a sysfs completo** (ACTION/DEVPATH/SUBSYSTEM=net/INTERFACE/IFINDEX). Ogni netdev registrato
   normalmente (rmnet/qmapmux runtime inclusi) genera quindi un `add` uevent vero.
2. procd (PID 1): `NETLINK_KOBJECT_UEVENT` group 1, nessun check di mittente → `/etc/hotplug.json`
   (dispatcha **solo add/remove**) → `hotplug-call <SUBSYSTEM>` → `/etc/hotplug.d/net/25-modemmanager-net` →
   `mmcli --report-kernel-event` (+ cache `events.cache`). procd ripubblica anche un evento ubus.
3. netifd: due canali indipendenti — rtnetlink `RTNLGRP_LINK` che **aggiorna solo device già noti**
   (`device_find() → return` se sconosciuto), e socket `NETLINK_KOBJECT_UEVENT` (`system-linux.c:374`) che crea il
   device object esterno; scarta i messaggi con `nl_pid != 0`.
4. ModemManager 1.24: **nessun listener uevent**. L'unico ingresso porta è D-Bus `ReportKernelEvent`
   (`handle_kernel_event`), che con `WITH_UDEV` non definito costruisce un MMKernelDevice generico da
   `/sys/class/<subsystem>/<name>` e valuta da sé `/lib/udev/rules.d/*.rules` (quindi le regole 80-mm-* valgono
   anche senza udev).

## Finestre temporali che decidono la corsa

- plugin manager: min 2000 ms dopo la 1a porta, `MIN_PROBING_TIME_MSECS` 4000, `EXTRA_PROBING_TIME_MSECS` 4000
  senza udev (2000 con udev).
- bearer: `WAIT_LINK_PORT_TIMEOUT_MS` 2500 → dopo la creazione del link WDS/mux MM attende 2.5 s che la porta
  `net/<nome>` sia *grabbed*; altrimenti "Timed out waiting for link port".
- Un report che arriva a device context finito **non aggiunge la porta al modem esistente** → serve restart MM.

## Sintesi di un evento: cosa è sicuro

- **Sì, sicuro e kernel-blessed**: `echo add > /sys/class/net/<if>/uevent` (root). `store_uevent()` →
  `kobject_uevent(KOBJ_ADD)` → il kernel rigenera tutto l'environment. Effetti collaterali idempotenti: gli script
  hotplug rieseguono (come al coldplug), MM riceve un report duplicato, 00-sysctl riapplica i sysctl.
  Usare solo `add`/`remove` (mai `change`: non viene dispatchato).
- **No, non funziona la iniezione raw** in `netlink-watch3.c`: `nlmsg_type = 0` è `NLMSG_NOOP` e il kernel lo
  scarta prima dell'handler uevent; inoltre `\0` nella format string tronca il payload (spariscono
  ACTION/SUBSYSTEM/INTERFACE) e DEVPATH è hardcoded su `/devices/virtual/net/`. L'iniezione raw è comunque
  possibile in teoria (`CAP_SYS_ADMIN`, `nlmsg_type` valido, il kernel riscrive `portid = 0`), ma non offre nulla
  in più e può mentire sul DEVPATH.
- **Sì, da tenere**: `mmcli --report-kernel-event="action=add,name=<if>,subsystem=net"` **senza** `sysfspath`
  (MM risolve `/sys/class/net/<nome>` da sé). Annunciare il physdev (`rmnet_ipa0`, `qmapmux*`), mai `rmnet_data0`.
- **Mai**: `ip link set down/up` o rename per forzare un evento → solo `RTM_NEWLINK`, netifd ignora device
  sconosciuti e MM non ha listener rtnetlink.

## Se un hotplug "non arriva" — ordine dei controlli

1. timing (MM già oltre la finestra di probing di quel physdev);
2. filtro device virtuali in `25-modemmanager-net` e sysfspath `/sys${DEVPATH}` corretto;
3. `mmcli` fallito/racing (dbus, MM fermo, `/var/run/modemmanager` assente) → evento solo in `events.cache`;
4. evento emesso prima del primo start di MM e mai cachato → invisibile (senza udev MM non enumera mai);
5. non è il kernel: l'uevent c'è, una volta sola, a sysfs completo.
