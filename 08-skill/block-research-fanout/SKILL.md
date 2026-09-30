---
name: block-research-fanout
description: Use when stuck before writing code. Fan out researchers.
---

# Block-Research Fan-out (sguinzagliamento agenti)

## Quando (REGOLA UTENTE — OBBLIGATORIA)

OGNI VOLTA che:

- **(a) appare un NUOVO errore/sintomo non spiegato immediatamente** — messaggio d'errore inatteso, crash, comportamento anomalo, test che fallisce in modo nuovo. Regola utente (verbatim, ripetuta infinite volte): «cerca online prima di metterti a scrivere codice», «ad ogni nuovo errore». **Se l'errore sembra "mai visto", è esattamente il momento di sguinzagliare** — il 90% dei blocchi ha già una soluzione pubblica da qualche parte;
- (b) siamo **bloccati** su un problema tecnico;
- (c) stiamo per **scrivere codice/soluzione custom** (qui TASSATIVO: mai una riga prima di aver setacciato il mondo).

## Metodo: sciame di subagenti per VELOCIZZARE (regola utente)

Regola utente (verbatim, ripetuta): «puoi spawnare tutti i subagenti che vuoi... puoi anche solo orchestrare e far fare tutto agli altri»; «per velocizzare il compito puoi spawnare tutti gli agenti che vuoi e tenerli attivi tutto il tempo che vuoi».

- **Limite temporale per ricerca: massimo 5 minuti** (correzione esplicita dell'utente). Specifica il limite nel prompt; raccogli i risultati disponibili e interrompi i ritardatari, senza aspettare il batch completo. Non presentare il limite richiesto come durata effettivamente rispettata. Per un errore meccanico immediatamente spiegato (es. import mancante o chiusura `fi` persa), non aprire sciami ridondanti: verifica localmente la correzione minima; riserva il fan-out ai sintomi non spiegati e alle nuove soluzioni custom.
- **Sciami per ricerca/verifica/ragionamento**: 8-20 agenti SIMULTANEI su assi diversi (procedura sotto) — esplorare ipotesi in parallelo invece che in serie. Un blocco che sembrava di settimane è caduto con **10 agenti in 11 minuti** (evidenza sotto).
- **LINGUA DELLE RICERCHE: INGLESE o CINESE — MAI ITALIANO** (regola utente, verbatim: «quando usi i subagenti per la ricerca, falla in lingua inglese o cinese perchè altrimenti in italiano troviamo molti meno risultati»). Vale per le **query dei motori**, per il **prompt dell'agente** e per la lingua in cui si chiede di cercare. La documentazione tecnica (kernel, driver, OpenWrt, Qualcomm) è in inglese; i bring-up di device e i workaround vendor stanno spesso solo in cinese. L'italiano taglia fuori quasi tutti i risultati: se serve, la sintesi finale si può riportare in italiano, ma la RICERCA va fatta in inglese/cinese.
- **Termini tecnici identici al codice**: cercare i simboli ESATTI (`remoteproc3`, `qmapmux`, `mm_report_event`, `--report-kernel-event`, path sysfs) e non le loro parafrasi: è il modo in cui si trovano commit, issue e thread utili.
- **Il lavoro meccanico va fatto inline, con script, dal parent** — direttiva utente più recente: builds immagini, download, tar, flash «siamo lenti» se delegati; la macchina non è il collo di bottiglia, il processo sì. Delegare SOLO ciò che è reasoning-heavy o genuinamente parallelo.
- **Gli agenti NON toccano il device fisico**: solo ricerche web, analisi host-side, comandi read-only. Il parent esegue sul device.
- **Il parent VERIFICA ogni self-report**: scarica gli artefatti su disco, grep delle stringhe chiave, controlla i numeri citati sul device. «Verifica TU i numeri» (regola utente) — i summary degli agenti sono ipotesi finché non verificate.

## Perché funziona (evidenza)
NX679J 2026-09-22: settimane di lavoro con la convinzione "quelle patch non esistono da nessuna parte" → **10 agenti in 11 minuti** → trovate 6 patch di Qualcomm stessa (qualcomm-linux/meta-qcom), un fork funzionante sulla STESSA versione (meizu-m2172-mainline/ModemManager, MM 1.24), e i parametri esatti confermati. Il 90% dei blocchi ha già una soluzione pubblica da qualche parte — spesso NON sul canale ovvio: layer Yocto vendor, fork di telefoni, thread in russo/cinese, code-search.

## Procedura
1. **Definisci il blocco in modo STRETTO**: stringa errore ESATTA (es. `Unsupported QMI kernel driver for 'net/rmnet_ipa0': (null)`), obiettivo ESATTO, fatti osservati vs ipotesi.
2. **Context block condiviso** (ripetuto in OGNI task): hardware/versioni, errore esatto, cosa già provato, lead noti, regole ferree, schema output.
3. **Assi DISTINTI — 8-10 agenti, un asse ciascuno** (modello collaudato):
   a. Progetto upstream (TUTTI i MR/issue/commit — anche chiusi/rifiutati + mailing list)
   b. Tooling/kernel afferente (es. libqmi + kernel ML + docs driver)
   c. Ecosistema distro (es. OpenWrt packages/forum/PR)
   d. Lingua RUSSA (4pda, habr, linux.org.ru, forum locali)
   e. Lingua CINESE (right.com.cn/Enshan, CSDN, Gitee, forum vendor)
   f. Dominio affine (es. linux mobile: postmarketOS, port dello stesso SoC/device)
   g. Layer VENDOR/BDK (meta-qcom, meta-radxa, SDK Quectel/Telit/Fibocom, TUTTI i repo del vendor del device)
   h. Forum modder/hacker (XDA, whirlpool, ROOter, reddit, blog)
   i. CODE SEARCH (grep.app, sourcegraph, github search) con identificatori ESATTI: nomi funzione, stringhe errore, macro, path
   j. Alternativi/multipiattaforma (tool alternativi, doc vendor, altre lingue minori)
4. **Regole ferree nei prompt (copiarle testuali)**: solo URL reali MAI inventare; quote/diff ESATTI; tag lingua; scarta tutorial senza codice; dichiara i vicoli ciechi espliciti; applicabilità high/med/low + what_we_should_do; 15-25 ricerche/estrazioni ciascuno.
5. **Schema output**: `{findings:[{url, what_it_is, language, exact_quote_or_diff, applicability, what_we_should_do}], dead_ends:[...], conclusion}`.
6. **IL PARENT VERIFICA (mai fidarsi dei self-report)**: scarica gli artefatti (patch/script) SU DISCO, grep le stringhe chiave, controlla sul device i fatti citati. Solo dopo si scrive codice.
7. **Codice SOLO da template trovati** (patch vendor/fork), diff minimale, cita la fonte nel commento/commit.
8. **Salva** URL+patch+parametri verificati nella skill del progetto (reference dedicata).

## Trappole note (verificate sul campo)
- **gitlab.freedesktop.org**: web UI dietro Anubis ("Access Denied 4d1dbaddfcc0f385"); l'API `/api/v4/.../changes|commits` funziona anonima; clonare i MR con `git fetch refs/merge-requests/N/head`.
- **grep.app**: checkpoint Vercel (usare browser reale o abbandonare); **searchcode.com**: API morta; **GitHub code search**: richiede login → usare API + raw.githubusercontent + `.patch` dei commit (`github.com/OWNER/REPO/commit/SHA.patch` funziona!).
- **forum.openwrt.org**: search.json rate-limited (429) → usare indice di motori esterni.
- Chiedere SEMPRE i `dead_ends` espliciti (i subagenti a volte saltano fonti silenziosamente).
- I summary completi restano in `~/.hermes/profiles/kernel-re/cache/delegation/subagent-summary-*.txt`.
- **musl + errno**: `strerror()` musl con valori negativi/non noti → "No error information": può MASCHERARE un EBUSY (−16). Per errori netlink/kernel: leggere l'errno signed o rispedire il messaggio raw (tool `nl-send`) e stampare l'ACK del kernel.

## Quando il blocco dura o rischia loop: pattern STATO-ATTUALE.md

Richiesta utente (2026-09-22): "un file di tutte le modifiche e le cose che stiamo facendo che ti ricorda di leggerlo".

1. Sessione lunga o tentativi che si ripetono → dispatcha **1 agente leaf** per creare/aggiornare `STATO-ATTUALE.md` nel progetto con: obiettivo, inventario COMPLETO modifiche (device + host/SDK, con md5 reali), cosa funziona con evidenza, cosa NON funziona coi messaggi d'errore ESATTI, **lista tentativi già fatti + esito (anti-loop)**, prossimi passi, comandi utili. L'agente lavora **READ-ONLY** sul device e cita comandi+output reali; in memoria va il puntatore al file ('leggerlo prima di ogni passo').
2. Il parent lo LEGGE prima di ogni nuovo passo e lo fa aggiornare ai milestone: dopo ogni breakthrough, altrimenti resta stale e descrive ipotesi superate.
3. Le IPOTESI degli agenti entrano nel file come 'DA TESTARE', mai come fatti. Esempio reale: un agente ipotizzò 'NLA_F_NESTED assente' come causa del fail netlink; l'esperimento discriminante (rispedire l'HEX esatto del messaggio con `nl-send` → ACK −16 dal kernel) dimostrò che il messaggio era PERFETTO, e la causa vera era EBUSY mascherato da musl. Verificare con test che discriminano, non con l'autorità dell'agente.
4. 3 tentativi falliti di fila = stop: metti in discussione l'assunzione di base (spesso è l'INTERPRETAZIONE dell'errore a sbagliare, non il codice).

## Esempio (NX679J, datapath ModemManager)
Blocco: MM non connette (whitelist driver hardcoded). 10 agenti → **meta-qcom** spedisce 6 patch datapath QTI (MR!1452/1443, `Upstream-Status: Submitted`); **meizu-m2172-mainline/ModemManager** (stessa base 1.24) ha il chaining+tag mux; parametri X65 verificati dalla catena locale (ep=EMBEDDED/1/mux=1). Patch scaricate in `qti-patches/` e verificate con grep prima di scrivere una riga di codice.

**Epilogo (fine sessione):** obiettivo raggiunto — boot 100% automatico → ifup 3s → ping 0%. I fix decisivi NON erano nelle patch trovate online: libqmi **patch 106 'probe mux'** (il driver vendor lascia mux EBUSY 'fantasma' dopo tentativi falliti; `musl strerror(-16)`='No error information' mascherava tutto), **kill della WDS della catena** (una sola per porta EMBEDDED) e **'MM restart forte'** (kill -9+10s+start nel boot: al 1° probe il modem viene scartato). Dettagli+log in `STATO-ATTUALE.md`.
