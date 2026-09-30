# `qmicli -d qrtr://0`: uso ripetuto e concorrente — verifica avversariale (24/09/2026)

Domanda sotto test: **«`qmicli -d qrtr://0` è affidabile per uso ripetuto e concorrente su questo platform?»**

Verdetto: **l'assunzione NON è falsificata per uso sequenziale / bassa concorrenza su modem sano** (nessun report upstream di fallimento di `qmicli` su QRTR ripetuto; il kernel notifica la chiusura al modem; MM e qmicli convivono — già verificato sul device). **È però indebolita** su tre assi: (a) raffiche parallele, (b) finestre di ri-registrazione servizi (restart modem), (c) `-p`/qmi-proxy come strategia di serializzazione. In più le due librerie spedite sul device hanno **due bug di memoria non corretti in nessuna release**.

## Versioni sul device (fatti, non ipotesi)

| Componente | Versione sul NX679J | Fix noti | Contiene il fix? |
|---|---|---|---|
| libqmi / qmicli | **1.36.0** (25.12; master OpenWrt ha 1.38.0) | `93e65e4`+`fd79fd3` (2026-06-26) — OOB read nel path QRTR (issue #133) | **NO**. Nessun tag di release contiene i fix: ultima release `1.38.0` = 2026-01-01 |
| libqrtr-glib | **1.2.2-3** | `f4fd658` (2026-07-02) — UAF in `qrtr_node_remove_service_info` (issue #7, DEL_SERVER) | **NO**. `git tag --contains f4fd658` = vuoto; non in 1.4.0 |

Conseguenza pratica: **qualsiasi crash di `qmicli` sul device è un esito possibile e non riparabile senza rebuild**, e va trattato dal chiamante (retry/isolamento), mai come «impossibile».

## Cosa è per-processo e cosa è condiviso (verificato nel sorgente)

- **Il servizio CTL non esiste su QRTR**: libqmi lo emula in locale (`qmi-endpoint-qrtr.c`, `handle_ctl_message`) e alloca i CID da un contatore interno (`client_info_new`, CID 1..255). **Nessun round trip di Allocate CID** ⇒ le collisioni di CID fra processi diversi non sono un problema.
- **Un `QrtrClient` (socket QRTR dedicato) per ogni client QMI (servizio)** (`qrtr_client_new()` → `socket(AF_QIPCRTR, SOCK_DGRAM)`), più un socket di controllo per il bus. Ogni processo = N porte QRTR effimere, chiuse all'uscita.
- **Lo stato client è per-processo**: un messaggio su un CID che quel processo non conosce viene rifiutato con `Unknown client %u for service %s`. Quindi `--client-cid` / `--client-no-release-cid` **non attraversano il confine di processo** su QRTR (MR !382, che tentava di rilassare questo, è chiuso).
- **QRTR è stateful come rpmsg**: alla chiusura del socket il kernel manda in broadcast `QRTR_TYPE_DEL_CLIENT` al modem (`qrtr_port_remove()`), e la ns elimina le lookup di quel client. Ogni invocazione di `qmicli` **apre e chiude davvero** un endpoint verso il modem — non è una lettura passiva.
- Il broadcast DEL_CLIENT è best-effort (`qrtr_bcast_enqueue`: `break` se l'allocazione fallisce, skb scartati se la coda del node è piena) ⇒ sotto churn forte lo stato client lato modem può restare stale (inferenza da meccanismo, non bug riportato). Vedi issue #51: su porte stateful (rpmsg) il modem «pulisce tutti i CID non rilasciati» alla chiusura, e l'autore scrive «likely also QRTR ports».

## Modalità di fallimento misurate/dedotte per uso ripetuto o a raffica

1. **Timeout di lookup iniziale: 1 s, per ogni processo.** `qmicli` chiama `qrtr_bus_new (1000)`; il bus non è condiviso fra processi. Errore esatto: `error: couldn't access QRTR bus: Timed out waiting for the initial bus lookup` (G_IO_ERROR_TIMED_OUT, `qrtr-bus.c:initable_timeout`). Al boot o durante un re-announce la lookup deve drenare la lista servizi di ogni node (48 servizi su node 0) entro 1 s.
2. **Cap kernel sulle lookup concorrenti: `QRTR_NS_MAX_LOOKUPS = 128`** (`net/qrtr/ns.c`). Oltre il cap `ctrl_cmd_new_lookup()` ritorna `-ENOSPC`, il kernel logga `QRTR client node exceeds max lookup limit!` e **non risponde**: il client resta appeso fino al timeout di 1 s. Le lookup vengono liberate quando arriva il DEL_CLIENT del socket chiuso → si accumulano solo con processi appesi. (Altri cap: `MAX_NODES 512`, `MAX_SERVERS 256`.)
3. **Datagrammi scartati in silenzio**: se la coda di ricezione del socket è piena, `qrtr_local_enqueue()` fa `kfree_skb` e ritorna `-ENOSPC` senza avvisare il mittente; lo stesso vale per la coda del node in TX. Una risposta QMI persa = timeout di transazione apparente (`No transaction matched in received message`).
4. **Riutilizzo immediato delle porte**: range effimero `0x4000..0x7fff`, assegnato con XArray → **la porta più bassa libera**, quindi il processo successivo eredita la porta di quello appena uscito. Un datagramma tardivo del modem indirizzato al vecchio client può finire nel nuovo processo.
5. **In-processo (thread/contesti) è rotto**: `QrtrBus`/`QrtrClient`/`QrtrNode` agganciano le GSource a `g_main_context_get_thread_default()`; con più thread/contesti solo l'ultimo creato viene servito (issue #122). «Concorrenza» può significare **solo processi separati**, mai thread nello stesso processo.
6. **`-p` non è la risposta**: `qmi-proxy` accetta `qrtr://0` (la URI passa indenne da `qmi_helpers_get_devpath`, poi `qrtr_get_node_for_uri` → `device_from_node`), ma la race di apertura concorrente del device è **aperta** (issue #113: «all requests get a timeout» + proxy che non risponde più ai segnali; il maintainer l'ha riconosciuta) e con 15 client concorrenti si osserva `ClientIdsExhausted` (issue #62). Il commento «some other concurrent request already did it» in `bus_new_ready()` copre solo l'oggetto bus, non il device.
7. **Finestra di porte stale**: la mappa servizio→porta è uno snapshot della lookup iniziale (`qrtr_node_lookup_port` legge la lista cachata). Se il modem ri-annuncia i servizi dopo lo snapshot, l'invio va su porta morta (`-ENODEV`) o sull'istanza sbagliata. **Si auto-ripara all'invocazione successiva** (lookup fresca) — argomento a favore della ripetizione contro il client long-lived.

## Regole operative consigliate (read-only)

- Concorrenza **≤ 2-3 invocazioni** in parallelo; mai raffiche > ~10 (1 s di budget lookup, cap 128, drop silenziosi).
- **Retry con backoff** solo su questi esiti, mai su errori QMI di stato: `couldn't access QRTR bus:` / `Timed out waiting for the initial bus lookup` / `node with id 0 not found in QRTR bus` / `No transaction matched in received message` / `Transaction timed out` / `Node is not present on bus`.
- **Non usare `-p`** per serializzare su QRTR (issue #113 aperta) e **non usare `--client-cid`/`--client-no-release-cid`** come stato condiviso fra processi.
- Un crash/segfault isolato di `qmicli` è **atteso possibile** (bug #133 non corretto in nessuna release): il chiamante deve isolare l'errore, non entrare in loop, non toccare kernel/MM.
- Preferire un **singolo processo long-lived** (o MM) solo se serve stato client persistente; per polling read-only la ripetizione per-invocazione è più robusta perché ri-snapshotta il bus ogni volta.

## Come ricontrollare i fatti (senza token)

```bash
P="mobile-broadband%2Flibqmi"; Q="mobile-broadband%2Flibqrtr-glib"
curl -sS "https://gitlab.freedesktop.org/api/v4/projects/$P/issues?search=qrtr&state=all&per_page=100" | jq -r '.[]|"\(.iid) [\(.state)] \(.title)"'
curl -sS "https://gitlab.freedesktop.org/api/v4/projects/$Q/issues?state=all&per_page=100" | jq -r '.[]|"\(.iid) [\(.state)] \(.title)"'
# i NOTES richiedono token (401); i bug report completi sono nel campo "description".
# Versioni/patch: git clone --depth 80 dei mirror GitHub (linux-mobile-broadband/{libqmi,libqrtr-glib})
#   git tag --contains <sha>   → se vuoto, il fix NON è in nessuna release
```

Evidenze kernel citate da `net/qrtr/{af_qrtr.c,ns.c,qrtr.h}` (master, letto 24/09/2026): `QRTR_NS_MAX_LOOKUPS 128`, `QRTR_MIN/MAX_EPH_SOCKET 0x4000/0x7fff`, `qrtr_port_remove()`, `qrtr_bcast_enqueue()`, `qrtr_local_enqueue()`, `ctrl_cmd_del_client()`, `ctrl_cmd_new_lookup()`.
