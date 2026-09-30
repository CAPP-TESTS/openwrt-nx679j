# RICERCA MODEM SoC Qualcomm (2026-09-20) — OpenWrt + mainline/postmarketOS

Due ricerche indipendenti (subagent, solo web, cite-URL) sul supporto modem on-SoC
Qualcomm (QRTR + IPA + rmnet) — cosa esiste oggi e cosa non stiamo considerando.

## SINTESI OPERATIVA PER NOI

1. **OpenWrt NON ha supporto end-to-end per modem on-SoC.** I proto cellulari
   (qmi/uqmi, mbim/umbim, ncm, 3g/ppp) = solo char device USB cdc-wdm o MHI/PCIe.
   **uqmi NON parla QRTR** (verificato: zero occorrenze nel codice). Nessun kmod IPA
   in OpenWrt (CONFIG_QCOM_IPA 'not set' perfino su qualcommax).
2. **PERÒ i mattoni ci sono, e li abbiamo già usati senza saperlo al massimo:**
   - `libqrtr-glib 1.2.2` + **`libqmi 1.36` con `-Dqrtr=y`** + `qmi-utils` → **PROVATO:
     `qmicli -d qrtr://0` parla col nostro X65** (DMS online, NAS signal info, IDs).
   - **`modemmanager 1.24`** nel feed 25.12: build `-Dudev=false` + `MODEMMANAGER_WITH_QRTR=y`
     + **plugin `qcom-soc`** (matcher: SUBSYSTEM==net DRIVERS==ipa|bam-dmux, wwan|rpmsg
     DRIVERS==qcom-q6v5-mss). **MM ≥1.18 (2021) = gestisce nativamente QRTR+IPA+rmnet**
     (DPM open port, WDA data-format con endpoint IPA, creazione link rmnet via netlink).
   - `modemmanager-rpcd` + **proto netifd `modemmanager`** + `luci-proto-modemmanager`:
     stack completa già pacchettizzata.
   - `kmod-rmnet` upstream dal 2024-10 (Robert Marko).
3. **MA per il NOSTRO caso (kernel vendor 5.10 + SM8450):**
   - Il plugin qcom-soc si aspetta i driver MAINLINE ("ipa"/"bam-dmux"); il nostro
     rmnet è quello **vendor** → **la discovery di MM va testata** (esperimento proposto).
   - **IPA SM8450 (v5.1) NON è upstream**: serie "[PATCH v2 0/3] SM8450 IPA support"
     del 2026-09-09, changes-requested; nei fork community (XEC/sm8450-mainline) il
     modem funziona (dati/SMS/chiamate senza audio) ma è fuori standard.
   - ⇒ **Sul kernel vendor l'unico datapath oggi è la nostra catena** (confermato).
4. **Percorso ufficiale pmOS per kernel NON mainline ("downstream"):**
   `msm-modem-downstream` + `libqipcrtr4msmipc` (LD_PRELOAD che emula AF_QIPCRTR)
   + `libsmdpkt_wrapper` → MM/qmicli utilizzabili su kernel vendor senza patch.
   (Nota: noi abbiamo già AF_QIPCRTR nel kernel vendor, quindi la parte emulazione
   potrebbe non servirci.)
5. **Cose che NON stiamo considerando (checklist):**
   - **MM come motore** (al posto dei nostri script) se la discovery regge: automatismo
     al boot, PDC/carrier profiles, offload tx/rx da sysfs, multiplexing obbligatorio.
   - **qmi-proxy / serializzazione QRTR**: più processi NON possono tenere lo stesso
     nodo; se il device QMI si chiude tra due chiamate si perde la mappa WDS/CID
     (noi la teniamo viva con gli holder; MM la tiene aperta di suo).
   - **Nomi iface dinamici col multiplexing** (rmnet_dataN a runtime): impatta il nostro
     proto a nome fisso e le regole firewall/NAT → da gestire se si automatizza.
   - **EFS/NV**: modemst1/2+fsg via rmtfs (lo facciamo) + EFS-sync responder; senza,
     il modem può spegnersi dopo i primi comandi QMI (caso SDX55M documentato).
   - **PDC/MCFG**: profili carrier; il MCFG può essere servito via TFTP-on-QRTR
     (lo facciamo via tqftpserv + modem_pr) o gestito da MM/PDC.
   - **IMS/VoLTE** (81voltd/OpenIMSd/IMSDCM): fuori dal nostro perimetro attuale.
   - **Backport kernel** possibili: le 3 patch SM8450 IPA (in review) o i data v5.0;
     remoteproc modem SM8450 già upstream (5.18).
   - **wwand** (ucode, PR openwrt/packages#30185): connection manager moderno ma
     niente QRTR/IPA dichiarato → non per noi ora.
   - Attenzione regressione rmnet+MAP su USB (stable 662dc80a5e86, v6.12.74).
6. **Fonti chiave:** wiki OpenWrt wwan/start (2026-04-19) e ltedongle (2026-09-06)
   coperte solo per USB; pagina modemmanager wiki = ferma al 2022; pmOS wiki Modem;
   linux-msm mainline-status (SM8450: IPA/PDC vuoti); NEWS libqmi/MM; patchwork serie
   SM8450 IPA; openwrt/packages net/modemmanager; meizu-m2172-mainline/modem-userspace;
   osmocom Qualcomm_Kernel.

## ESPERIMENTI PROPOSTI (in ordine)
1. **Test discovery ModemManager** sul device (install + debug, senza lasciarlo pilotare
   la catena già su; valutare su boot fresco) → se trova modem+porta dati, valutare
   l'adozione.
2. **Sostituire i tool custom con qmicli** dove possibile (letture già ok; da valutare le
   scritture delicate: DMS set 0x2e osservato, DPM, WDS — oggi provate e funzionanti).
3. Immagine: includere `libqmi/qmi-utils` (+MM se il test 1 riesce) per avere gli
   strumenti standard a bordo.
