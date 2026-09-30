# NX679J — HANDOFF MODEM → INTERNET (aggiornato 2026-09-19 sera)

**Stato: NON RISOLTO — ma il gate è ora un solo punto noto.** Il modem su OpenWrt
ora si avvia COMPLETO (56 servizi QMI, WDS incluso, EFS+mcfg funzionanti).
Manca solo il transito `mode 5 -> mode 0` (vedi sotto). Restano da fare: WDS start,
configurazione rmnet_dataN, ping.

## → LEGGI PRIMA QUESTO: `MODEM-HANDOFF-20260919.md` (432 righe)

Contiene, verificati su hardware:
- la **ricetta completa di avvio** del modem da OpenWrt (msm_sharedmem + /dev/uio0,
  rmtfs con service 14, tqftpserv patchato con /rfs e versioni 1..10, qrtr-smd,
  rproc start + RESTART controllato a ~86s per prevenire il reset del SoC);
- il **crash** (`reboot` del SoC ~200s dopo l'avvio del modem) e la sua **prevenzione**;
- il comportamento `mode 5` ("shutting down"): su Android il modem passa a `0`
  da solo ~200s dopo un restart; su OpenWrt (finora) no — **questo è IL gap**;
- gli strumenti nuovi (qmi-qrtr con rawseq/playback, qmi-trace, crashlog-dump,
  voci refs/ per trace Android, ModemManager, modem_pr);
- la lista dei prossimi passi ordinati (pd-mapper, IPA/ipacm, attesa lunga, SSCTL).

## FATTI CHE SERVONO SEMPRE (non ri-derivarli)

- Trasporto = **QRTR** (AF_QIPCRTR). NS nel kernel (`ns.ko`); `qrtr-smd.ko` da
  caricare a mano (l'init di OpenWrt non lo fa).
- mss su OpenWrt = **remoteproc3**; su Android = **remoteproc4**.
- Partizioni EFS = **sdf2..sdf5** (modemst1/modemst2/fsg/fsc); rawdump = sda11;
  boot_b = sde41 (mknod b 259 25 se manca).
- `/dev/uio0` (239:0, name "rmtfs") esiste SOLO dopo `msm_sharedmem.ko`.
- Niente `dd`/redirect diretti su block device (policy): `cat` su file + `cp`.
- /tmp si svuota a ogni reboot; heredoc ssh >~1KB si troncano (usare script pushati).
- Controllo boot: `reboot2 bootloader` → `fastboot set_active a|b`.

## Criterio di completamento (invariato)

```text
rmnet_dataN con IP assegnato
route default via gateway su rmnet_dataN
ping -c 3 -W 2 8.8.8.8: 3 risposte, 0% packet loss
```

In assenza di tutti e tre, lo stato resta NON RISOLTO.
