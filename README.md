# **Note preliminari by RAW - 2026.10.04**

1. Il port non è comunque un OpenWrt “nativo” nel senso stretto = userland OpenWrt eseguito in chroot sullo slot B - sopra il kernel vendor Android 5.10.66 lasciato invariato. Il modem X65 è pilotato direttamente via QMI/QRTR (senza ModemManager) e l’interfaccia è una UI scritta in C su DRM/KMS. Non si tratta quindi di un porting e.g. in implementazione halium/hybris per cui si opera a livello di kernel/boot + system + vendor.

2. L'autore NON ha rilasciato pacchetti chiavi-in-mano per flash & esecuzione - _giustamente, tanto per vincoli di licenza/redistribuzione dei blob proprietari quanto anche per l'assenza di adeguate procedure per recupero/ripristino d'emergenza_. Tuttavia, NON ha nemmeno rilasciato documentazione e risorse adeguate, onde consentire piena riproducibilità delle build sino alle ultime versioni nelle iterazioni (_v180 del 25/09 e v181 del 27/09_)... = si è infatti limitato ad un paragrafo sbrigativo ["Come ricostruire da zero"](/DOCUMENTAZIONE.md#9-come-ricostruire-da-zero) all'interno del mega-file di riepilogo attività :/

\
Per quanto il progetto presenti spunti interessanti, ci sono decisamente non pochi aspetti da rivedere / rifare - e non solo per quanto concerne revisione + riorganizzazione di docs/risorse per loro presentazione pub (_aka switch ad org con repo differenziate, anche per rispettare meglio licenze n.d.r._).

Mancano comunque elementi necessari - tanto per consentire 100% reproducible build con il dispositivo specifico NX679J quanto per replicare metodologia/approccio (cfr. anche impegno agenti/AI) in attività di sviluppo esteso ad altri dispositivi.

\
In attesa di eventuali aggiornamenti da parte dell'autore, ho provveduto intanto a caricare in questo fork :

- **[Prima guida best-effort per la ricostruzione di una build per flash/uso su Nubia RedMagic 7 (NX679J)](Best_Effort_Reconstruction_with_gaps_on_NX679J.pdf)** = build funzionalmente equivalente alla v90 del 23/09, partendo come base dalla compilazione v10 (solo avvio OpenWrt + SSH).

- **[Analisi grezza di repo/deliverable con focus su incidenza/impatto delle quote d'impegno Agenti/AI](Rapporto_openwrt-nx679j.pdf)** (_disponibile anche in [EN](Report_openwrt-nx679j_EN.pdf)_) per le fasi di attività svolte per il progetto nell'arco dei 3 mesi (Luglio, Agosto, Settembre).

----------------
----------------

# NX679J — OpenWrt nativo su un Nubia RedMagic 7 (SM8450)

Port completo di **OpenWrt 25.12.5** su un telefono **Nubia RedMagic 7 (NX679J, Snapdragon 8 Gen 1 / SM8450)**: OpenWrt gira in chroot sullo **slot B** con il **kernel vendor 5.10.66 lasciato intatto**, il **modem X65** come uplink, e un'**interfaccia utente nativa sul display del telefono** (tasti laterali + standby senza burn-in).

> ⚖️ **Progetto indipendente.** Non è affiliato, approvato né sponsorizzato dal progetto OpenWrt. «OpenWrt» è un marchio registrato di Software Freedom Conservancy, usato qui in forma descrittiva e fattuale (il sistema portato è la release OpenWrt 25.12.5); nessun logo di terzi è utilizzato. «Nubia», «RedMagic», «Qualcomm» e gli altri nomi citati appartengono ai rispettivi titolari.

> 📄 **Il documento principale è [`DOCUMENTAZIONE.md`](DOCUMENTAZIONE.md)** (~249 KB, 22 sezioni): la storia completa e verificata del progetto — le radici (reverse-engineering della catena di boot, secure boot/AVB, EDL/QDL, XBL/ABL/UEFI), il port (immagine, sistema, modem X65, interfaccia, kernel e display), i **crediti** (strumenti e fonti di conoscenza, con URL), più cronologia, scheda hardware, manuale operativo e archivio del progetto.

> 🔧 **Gli artefatti non inclusi (firmware, moduli, boot image…) si ricostruiscono tutti**: [`ESTRAZIONE-BLOB.md`](ESTRAZIONE-BLOB.md) contiene le **procedure complete e verificate** (dall'OTA ufficiale e dal proprio dispositivo).

## Cos'è questo progetto

Un telefono Android vincolato che diventa un router Linux. La catena di boot Qualcomm è stata studiata fino in fondo; l'immagine di boot viene ricostruita attorno al kernel stock; il sistema vive in un chroot sul ramdisk; il modem viene pilotato direttamente via QMI/QRTR (senza ModemManager); una UI C scritta a mano disegna su DRM/KMS le funzioni di controllo. Ogni affermazione nella documentazione porta la sua evidenza: comandi, offset, md5, righe di log.

## Nomi redatti: convenzione

In 12 file compaiono segnaposto come `«redacted»` (questo README e `08-skill/LEGGIMI.md` spiegano la convenzione): la redazione automatica ha mascherato percorsi e nomi ritenuti sensibili. Il percorso del progetto è `/home/user/nx679j-stock`; la dir degli strumenti sul device è `/usr/lib/*/modem/`. I file del progetto hanno prefisso `nx679j-`: se in una copia il prefisso appare troncato, il file reale è quello con `nx679j-`.

## Contenuto del repository

| Cartella | Cosa contiene |
|---|---|
| `DOCUMENTAZIONE.md` | La storia completa e verificata del progetto — **il punto di partenza** |
| `ESTRAZIONE-BLOB.md` | **Come ricostruire ogni artefatto escluso**: firmware stock, partizioni, moduli vendor, firmware touch, DTB/DTBO, boot image, loader Firehose, sorgenti GPL, GPT, EFS |
| `LICENSES/` | I **testi integrali** delle licenze dei componenti di terzi (GPL-2.0, LGPL-2.1, Apache-2.0, BSD-3-Clause, musl, svg-spinners) con la provenienza di ciascuno |
| `THIRD_PARTY_LICENSES.md` | Licenze e attribuzione del codice di terzi incluso (e di quello citato ma escluso) |
| `01-documenti/` | Documenti storici e snapshot datati degli stessi; i documenti singoli identici a uno snapshot sono stati deduplicati (l'elenco con le copie canoniche è in `DEDUPLICA.md`) |
| `02-sorgenti/` | Script e sorgenti: builder delle immagini (`build-v*.py`), payload del ramdisk (`payload-build-v64/`), UI C, strumenti, workdir delle fasi init |
| `04-persistenza/` | I tar di persistenza (etc/luci/tools) **sanificati**, come iniettati nel ramdisk |
| `05-bootchain-re/` | Il reverse-engineering della catena: analisi ABL/XBL/UEFI (testo, offset, quote), readback EDL verificato, fatti del dispositivo |
| `06-ricerche/` | Le ricerche su display/DSI/idle_pc_state e il lavoro di porting (DTB swap, device facts) |
| `07-evidenze-runtime/` | Prove raccolte dal device: interrupts, dmesg, properties, getvar, pinctrl |
| `08-skill/` | Le procedure operative del progetto: passi verificati e **trappole già pagate** |
| `09-albero-originale/` | Il resto dell'albero di lavoro, non-blocco: RE di EDL/QDL (`edl-recovery/`), analisi dei fallimenti (`phase2-failure-analysis/`), build headless e log dei tentativi di boot (`native-openwrt-usb-build/`), verifiche UEFI/OTA (`verified-*`), prove sul bootloader (`hypervisor-bypass/`), i **tool QMI/QRTR** e altri strumenti del progetto |

## Cosa NON troverai qui (e perché)

Immagini di boot, moduli vendor, firmware e dump di partizioni restano fuori **per scelta precisa**: contengono software proprietario Qualcomm/Nubia, e la documentazione li descrive e li referenzia come artefatti locali del dispositivo — coerentemente con la natura di interoperabilità del progetto. In questa revisione sono stati rimossi anche gli artefatti vendor che erano rimasti (firmware modem `.mbn`, DTB/DTBO, ramdisk e `init` vendor) e le copie del rootfs OpenWrt.

**E per ricostruirli tutti c'è la procedura completa**: [`ESTRAZIONE-BLOB.md`](ESTRAZIONE-BLOB.md) — firmware stock dall'OTA ufficiale, immagini di partizione dal proprio device (root), moduli vendor, firmware del touch (debugfs), DTB/DTBO, boot image, loader Firehose, sorgenti kernel GPL, GPT ed EFS, con i comandi verificati del progetto.

## Metodo

Una sola regola: **nessun risultato senza verifica**. Il documento è stato revisionato sezione per sezione contro i sorgenti originali (audit indipendenti), e ogni contributo esterno — strumenti, repository, thread, documentazione — è accreditato per intero nelle due sezioni di crediti. Questo lavoro poggia su molto di altrui: i crediti appartengono ai loro autori.

## Licenza

**Codice: MIT** (file [`LICENSE`](LICENSE)) — **documentazione: CC BY 4.0** (file [`LICENSE-docs`](LICENSE-docs)).
Attribuzione richiesta: «Saddytech — NX679J OpenWrt» con link a questo repository.

Il codice e i documenti di **terzi** inclusi mantengono le proprie licenze: l'attribuzione è in [`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md).

## Avvertenza

Progetto di ricerca e interoperabilità su un dispositivo di proprietà degli autori. Nessuna garanzia: le procedure di flash vanno seguite solo con backup verificati, come descritto nel manuale operativo della documentazione.
