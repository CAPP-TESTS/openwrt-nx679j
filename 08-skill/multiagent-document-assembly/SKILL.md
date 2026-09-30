---
name: multiagent-document-assembly
description: Use when building or verifying big docs with agent fleets.
version: 1.0.0
author: curator
license: MIT
metadata:
  hermes:
    tags: [documentation, orchestration, verification, agents]
    related_skills: [re-driver-report, block-research-fanout]
---

# Documenti multi-sezione con flotte di agenti

Per consegnare la storia completa di un progetto multi-sessione — o qualunque documento grande fatto di sezioni autonome — la pipeline che funziona è: **sezioni estratte da agenti con output su disco → assemblaggio verbatim → audit indipendente per sezione → correzioni applicate dal parent.** Vale per la documentazione finale di un lavoro di reverse engineering, per una specifica multi-capitolo, per un manuale operativo.

## When to Use

- un documento grande (più sezioni autonome) va prodotto o verificato con flotte di agenti;
- una storia di progetto va ricostruita anche da log/sessioni precedenti, non solo dall'albero;
- un documento esistente va verificato prima della consegna (audit per sezione);
- il lavoro a flotte ha già subito crash di sessione: valgono le regole anti-crash (ondate piccole, output su disco, rilancio immediato).

## Regole sempre valide

1. **Ondate da max 3 agenti** per il lavoro pesante (verifiche, ricostruzioni da log/JSON, assemblaggi). I fan-out numerosi si usano per la ricerca web breve, che è leggera; il lavoro largo e lungo fa morire il processo della sessione e **i figli muoiono con lui**.
2. **Ogni agente salva il suo output su disco** oltre a restituirlo — va scritto nel prompt («Salva la sezione in `/tmp/<lavoro>-parts/NN-nome.md` in aggiunta al risultato»). Un output che vive solo nella risposta dell'agente è perso per sempre.
3. **Dopo un crash, rilancia subito senza chiedere** (direttiva utente: «rimetti tutti a lavoro»): verifica cosa è salvato su disco, ri-dispatcha solo le unità perse con le stesse ondate piccole, riporta in una riga cosa è salvo e cosa è stato rilanciato.
4. **L'ultimo miglio di scrittura resta nel parent**: assemblaggio e correzioni si applicano inline; un agente "fixer" è il primo a cadere col crash, e il suo lavoro parziale non verificato fa più danno che bene.
5. **Chi scrive non verifica**: l'audit lo fanno agenti che non hanno scritto la sezione, **read-only, con l'ordine esplicito «non modificare»**, e con l'elenco delle fonti da confrontare. L'auditor riporta: verificati (con esempi), **sbagliati** (valore del documento vs fonte, con `path:riga`), non verificabili, omissioni.
6. **Anche gli auditor sbagliano**: prima di applicare una correzione, ricontrollare la fonte. Una "correzione" su un file dichiarato inesistente si è rivelata falsa (il file esisteva): mai applicare ciecamente.
7. **Ogni affermazione porta la sua evidenza** (comando, offset, md5, riga di log) e ciò che non è nelle fonti si dichiara «non trovato localmente» — mai inventato. L'istruzione va nel prompt dell'agente, non solo nelle intenzioni.

## Pipeline

1. **Fonti**: albero del progetto + **log delle sessioni precedenti** (vedi sotto). Una sezione = un agente, schema `{section_markdown, uncertainties}`, citazioni esatte.
2. **Assemblaggio** (script o agente dedicato): concatenazione **verbatim**; si aggiungono solo struttura (livelli di heading, indice, abstract). Gli output degli agenti sono JSON con la chiave `section_markdown`: **estrarli**, non incollarli col guscio.
3. **Inserimento con ancore uniche** (`assert count == 1`): l'indice usa link (`[titolo](#...)`), quindi l'heading completo è l'ancona affidabile, il solo titolo no.
4. **Verifica**: un auditor per sezione (regola 5). Tipi di errore che emergono regolarmente: autocontraddizioni interne al documento, percorsi inventati, citazioni che non combaciano col formato reale del codice, misure di un INCIDENTE diverso spacciate per verifica, una fonte sola presentata come fatto.
5. **Correzioni**: dal parent, inline, con backup; coppie esatte old→new ognuna asserita una sola volta; se 0 o >1 occorrenze si salta e si riprova con regex flessibile; a fine giro si riconta (`wc -c -l`, grep dei valori corretti).
6. **Chiusura**: struttura finale verificata con grep degli heading, indice aggiornato, pulizia tipografica (vedi trappole).

## Fonti: albero + sessioni precedenti

I log delle sessioni precedenti contengono le prove grezze che l'albero non ha (tentativi falliti, diagnosi, incidenti): `~/.hermes/profiles/<profilo>/sessions/request_dump_<sessione>_*.json` — JSON con i messaggi, da estrarre con python/grep, mai da leggere interi; `state.db` elenca le sessioni con i titoli. I dump sono periodici: la sessione viva può non esserci ancora. Direttiva utente: quando la storia ha fasi "perdute", **andarle a ricercare nei log delle sessioni precedenti**, non solo nei file del progetto.

## Trappole

- **Mascheramento negli output**: l'harness può sostituire una stringa del progetto nei transcript di OGNI strumento — grep e read_file compresi. Non si può confermare a occhio una sostituzione o una riparazione: si prova in **esadecimale** (`.encode().hex()`) o contando dentro python. Gli agenti che copiano dagli output possono scrivere il segnaposto nel documento: cercarne le varianti e sostituirle prima della consegna.
- **I nomi "redatti" nei transcript non significano file inesistenti**: verificare l'esistenza del file (`search_files` o `ls`) prima di correggere.
- **I transcript troncano le righe lunghe** (`…(+N chars)`): la copia completa di una sezione sta nel file `subagent-summary-*.txt` quando esiste, e nel messaggio di batch nel contesto del parent. Salvare su disco il prima possibile.
- **Virgolette tipografiche nei blocchi di comandi**: rompono il copia-incolla — sostituirle con quelle dritte prima della consegna, come gli apostrofi tipografici nei percorsi.
- **L'assemblaggio non è verifica**: struttura e titoli giusti ≠ contenuto giusto. Senza audit per sezione gli errori restano nel documento consegnato.
- **Le correzioni si applicano una volta sola**: ricontare prima di ogni passata; una fix già applicata e riapplicata rompe il testo.

## Ricette concrete

Estrattori dal guscio JSON, inserimenti con ancore, sostituzioni con conteggio, verifica esadecimale: `references/ricette-assemblaggio.md`.
