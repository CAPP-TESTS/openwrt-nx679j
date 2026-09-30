# UI sul display — architettura, coordinate, trappole

Interfaccia grafica che controlla OpenWrt dal display del telefono (v135-v137).
Sostituisce kiosk3 come client di boot, con kiosk3 come fallback.

## Componenti (tutti in `/usr/lib/nx679j/modem/`)

| file | ruolo |
|---|---|
| `nx679j-ui` | binario C statico aarch64, **zero dipendenze**: DRM grezzo + disegno CPU + touch + azioni |
| `nx679j-ui-fetch.sh` | daemon dati: ogni 3 s scrive `/tmp/ui-data.txt` (key=value). Le chiavi `modem.*` si rinnovano ogni 3 tick e **restano in cache** (`/tmp/ui-modem-cache.txt`) — senza cache ogni tick le cancellava |
| `uinput-touch` | tool di test: inietta un tocco vero via `/dev/uinput` (verifica da remoto) |

Perche' separare: la UI **non deve mai bloccarsi** (il pannello collassa oltre 58 ms senza
commit). Le query `qmicli`/ubus le fa il fetcher; la UI legge solo un file.

## Coordinate (1080x2400)

- barra in alto: y 0..150 · contenuto: 150..2130 · **tab: y >= 2130**, indice `x / 360` -> id `100+t`
- **pulsanti: y 1860..2040**, larghezza piena (x 40..1040)
- pagina MODEM: pulsante RICONNETTI a `(540,1950)` -> id 1
- pagina RETE: WI-FI SU `(x<540)`, WI-FI GIU `(x>=540)`, stessa riga
- pagina SISTEMA: RIAVVIA (due tocchi per confermare)

## Trappola del tocco (bug reale, trovato col test iniettato)

L'input del kernel manda `ABS_MT_TRACKING_ID` **prima** di `ABS_MT_POSITION_X/Y` nello
stesso frame. Risolvere il click all'istante del TRACKING_ID significa usare le
coordinate del tocco **precedente**: il click non scatta mai (con un dito reale
e' identico). **Il click si risolve a fine frame (`SYN_REPORT`)**, quando le
coordinate sono definitive. Vale per qualunque client touch, non solo questo.

## Nodo `/dev` statico (seconda trappola)

`/dev` e' una directory del ramdisk, **non devtmpfs**: il kernel non crea i nodi dei
device nuovi. Per uinput: `mknod /dev/uinput c 10 223` (lo fa il launcher). Per il
nodo `eventN` del device virtuale: leggerne major:minor da
`/sys/class/input/eventN/dev` e crearlo a mano — l'attributo **name sta su
`eventN/device/name`**, non su `eventN/name` (che non esiste).

## Verifica da remoto (senza dita)

```sh
# 1) la UI e' viva e il pannello scanoutta
pidof nx679j-ui; grep TELEMETRIA /tmp/display-late.log | tail -1   # fps ~45.4, gap_max ~25 ms, fence_timeout=0
awk "/msm_drm/{print \$2}" /proc/interrupts                        # ~136 IRQ/s con la UI attiva (45 fps x 3)

# 2) tocco iniettato: tap sulla tab RETE e poi sul pulsante
/usr/lib/nx679j/modem/uinput-touch 540 2270 --hold 7   # tab centrale (RETE)
/usr/lib/nx679j/modem/uinput-touch 540 1950 --hold 7   # RICONNETTI
# esito: TELEMETRIA mostra pagina=RETE; il pulsante scrive la richiesta e il supervisor rinnova
```

`touch-selftest` (compilato da `touch-selftest.c`, che include `nx679j-ui.c` con
`-Dmain=ui_main_unused`) prova `touch_scan`+`poll_touch`+`hit_test` **senza aprire il
DRM**: si puo' eseguire mentre la UI e' viva.

## Costruzione

1. i sorgenti sono frammenti `ui-1-base.c` ... `ui-6-main.c` concatenati in `nx679j-ui.c`
2. toolchain: `aarch64-openwrt-linux-gcc` dell'SDK 25.12.5, `-static -O2 -Wall -Wextra`
3. **anteprima sull'host**: `ui-preview.c` (include `nx679j-ui.c` con `-Dmain=ui_main_unused`)
   disegna le 3 pagine in PPM; convertirle in PNG e **guardarle** prima di toccare il
   pannello — cosi' si scoprono layout illeggibili senza rischi
4. `build-v90.py` inietta `nx679j-ui`, `nx679j-ui-fetch.sh`, `uinput-touch` nel ramdisk

## Menu a categorie (v142) — copertura della GUI web

Requisito: dal display si deve poter fare tutto cio' che si fa da LuCI via IP.
Struttura: **4 categorie** in basso (STATO/RETE/MODEM/SISTEMA), **riga di pagine**
sotto la barra (18 pagine), contenuto con liste scorrevoli e **tasti per riga**.

| categoria | pagine |
|---|---|
| STATO | Panoramica, Log sistema, Log kernel, Processi, Rotte, Lease DHCP, Mount |
| RETE | Interfacce (SU/GIU per riga + RINNOVA WAN), Wi-Fi, Firewall (+RICARICA), Diagnostica (3 ping) |
| MODEM | Stato, SIM, Sessioni (pagina storica) |
| SISTEMA | Servizi (AVV/FER/ON/OFF per riga), Impostazioni, Backup (+CREA BACKUP), Riavvio |

**Scambio a caldo (kill → scp → start): ~15 s, di solito funziona ma NON e'
esente da rischio.** Su ~8 scambi riusciti consecutivi, uno ha ucciso il link
(700 fence in ritardo, pannello morto fino al reboot). La protezione
`idle_pc_disable` alza la soglia, non azzera la probabilita': il gap e' di
10-20 s e il primo modeset della nuova istanza resta delicato. Regola: farlo
solo quando un reboot e' possibile subito dopo, e non mentre si sta testando
qualcos'altro che poi non si potrebbe piu' verificare.

 **Tastiera a schermo (v145, fatto):** pagina **SISTEMA → Modifica**, un tasto
MOD per campo. **9 campi modificabili**: hostname, SSID, chiave Wi-Fi, password
root (via `ubus call luci setPassword`), **IP LAN, netmask, inizio e numero DHCP,
server DNS**. Verificati sul display: hostname (`OpenWrt`→`OpenWrt0x`) e
`dhcp.lan.start` (100→200→100).

I campi di rete accettano **solo cifre, punti, spazi e virgole** e vengono
rifiutati se non contengono almeno una cifra: un valore malformato cambierebbe
l'indirizzo della LAN o il DHCP. IP/netmask: `uci commit network && ifup lan_wifi`
(fa ripartire l'AP: i client Wi-Fi si riconnettono); DHCP/DNS:
`uci commit dhcp && /etc/init.d/dnsmasq restart`.
Geometria: 4 righe da 10 tasti (108x150 px) da `KB_Y 1420`, barra comandi a
`KB_BAR`; id `700+indice` per i caratteri (righe `1234567890 qwertyuiop
asdfghjkl# zxcvbnm@_.-`) e `800..804` per CANC/SPAZIO/OK/ANNULLA/PULISCI.
Il testo viene ripulito (apici, backtick, `$`, backslash, caratteri di controllo)
prima di finire nella riga di shell; i campi vuoti annullano.
La tastiera e' **modale**: `hit_test_menu` la interroga per prima e `kbd_draw`
disegna sopra tutto.

**Resta da fare per la parita' piena:** i campi che oggi sono solo in lettura
(IP statici, DNS, hostname statici, regole firewall, fuso orario a
lista) e la gestione dell'upload (restore backup, sysupgrade).

### Mappa degli id (imparata a caro prezzo)

`1..29` azioni fisse · `30..35` pulsanti di pagina (RINNOVA_WAN 30, BACKUP 31,
PING 32/33/34, FW_RELOAD 35) · `100+` era A_TAB0 del vecchio menu a 3 tab ·
`200..` pagine · `220/221` scorrimento · `400..` categorie · `500..` tasti di
riga (`500 + riga*4 + tasto`).

### Geometria (condivisa fra disegno E hit-test)

tab categorie `y 2210-2360`, x = 40 + i*(1000/CAT_N);
riga pagine `y 150+`, 4 per riga, cella 258 px;
barra pulsanti di pagina `PB_Y 1870, h 120`, larghezza (1000-20*(n-1))/n;
liste da `LIST_Y 330`, riga 92 px, **max (PB_Y-20-LIST_Y)/ROW_H = 16 righe**;
scorrimento `y 2030-2126`, x 700-860 e 880-1040.

### Tre trappole, tutte costate un giro a vuoto

1. **Pulsanti fuori dall'hit-test**: disegnavo i pulsanti di pagina dentro le
   pagine con coordinate a mano, e `hit_test_menu` non li conosceva → non
   rispondevano. Ora c'e' `page_btn()`/`page_btn_hit()`, **una sola geometria**.
2. **Ramo "tab" del vecchio menu nel loop**: `if (id >= A_TAB0) page = id-100;`
   intercettava gli id nuovi (403 → page=303) e `do_action` non veniva mai
   chiamato. Con il menu a categorie **ogni id passa da do_action()**.
3. **`idle_pc_disable` letta troppo tardi**: il flag si leggeva dopo il primo
   commit, che quindi scriveva `idle_pc_state=0` — disattivando la protezione
   proprio durante il primo modeset dopo uno scambio a caldo (pannello morto,
   300 fence in ritardo). **Il flag si legge PRIMA di qualunque commit** e la
   proprieta' va messa anche in `commit_cnp`.

### Ascoltatori/liste: chiavi numerate E con nome

`list_rows()` accetta entrambe: `log.1/proc.1/route.3` (posizionali) e
`svc.wpad/net.if.lan_wifi` (con nome). Guardare solo la prima forma lasciava
**vuote** le liste di servizi e interfacce. I valori hanno il **nome come primo
campo** (`wpad ON auto`), cosi' il tasto di riga sa su cosa agire.

### Lease statici (v148)

Pagina **RETE → Lease statici** (5a pagina): righe `sh.@host[N]` con **DEL**, piu'
**AGGIUNGI** che apre la tastiera su `nome ip [mac]` (un solo campo, come il
wizard di LuCI). Comando: `set -- <testo>; uci add dhcp host; uci set …name/-ip/-mac;
uci commit dhcp && /etc/init.d/dnsmasq restart`.
Collaudato: cancellazione dal display (1 → 0 lease); il comando di aggiunta
verificato a mano (`esito=0`).

**Trappola (due volte) — le regex su `uci show` con sezioni ANONIME:**
la sezione si chiama `@host[N]`, non `host`: cercare `host\.name=` non
corrisponde a nulla e la lista resta **vuota senza errori**. Usare
`@host.*\.name=`. Lo stesso vale per ogni `@tipo[N]` di uci (dnsmasq, system).
Sintomo tipico: la pagina si apre, l'hit-test e' giusto, ma la lista dice
"(nessun dato)". **Prima di sospettare il tocco, guardare il file dati.**

**Posizioni dei tasti: si contano, non si stimano.** La riga 3 della tastiera e'
`zxcvbnm@_.-`: il `.` e' l'ULTIMO carattere (indice 9 → x=1026), non l'ottavo,
e `@` e' l'ottavo (x=810). Un test che calcola la posizione "a occhio" digita un
altro carattere, la validazione rifiuta il valore e sembra che la funzione non
funzioni — mentre sta funzionando correttamente. Formula: x = indice*108 + 54,
y = KB_Y + riga*150 + 75 (KB_Y=1420).

### Guardare i pixel veri del device (senza webcam)

Quando una sovrapposizione non si riproduce nelle anteprime (perche' li' la geometria
puo' differire), la UI scrive su richiesta il frame che ha disegnato:
`touch /tmp/ui-dump` → al primo ridisegno scrive `/tmp/ui-dump.ppm` (PPM 1080x2400).
Si scarica con scp e si converte; per giudicare una fascia si ritaglia **a piena
risoluzione, 1:1** (il font e' 24x48 px: a mezza scala i modelli di visione leggono
male e inventano — e' successo: "WINDTRE" letto come etichetta di un tab).

### Le coordinate verticali (imparate a caro prezzo)

Layout definitivo: barra in alto `0..144` (titolo y=26 scala 4, data a destra
y=34, categoria + nome pagina y=96 scala 3 → 96..144), barra tab pagina
`170..258` (una riga sola!), contenuto da `LIST_Y 300`.

**Le due pagine storiche (ui-5) erano scritte per il layout vecchio:** la pagina
MODEM disegnava l'operatore a **y=180 con scala 5 → 180..260**, cioe' SOPRA la
barra dei tab (170..258) — e la pagina MODEM e' quella che si vede all'avvio,
quindi l'utente vedeva "WINDTRE" sovrapposto ai tab **a ogni boot**; la pagina
Wi-Fi partiva a y=170. Ora: modem 300, Wi-Fi 360.

**Regola:** ogni pagina nuova o modificata si controlla col dump del frame, non
solo con l'anteprima host; e il ridisegno va segnalato quando i dati cambiano
(`kv_load` ritorna 1 e il loop mette `dirty`), altrimenti lo schermo resta fermo
fra un tocco e l'altro.

### Anti-residuo sul vetro (v156) — il difetto "testi sovrapposti"

Sintomo: sul display restano visibili le scritture **vecchie** sotto quelle nuove
(l'utente legge etichette che nel codice non esistono piu'). Causa: il pannello
riscrive solo le zone che il driver considera cambiate, quindi i pixel vecchi
sopravvivono. Cura applicata in `draw_page` (ui-5):

```c
static unsigned bg_flip = 0;
fill_rect(m, pitch, 0, 0, W, H, C_BG ^ (bg_flip++ & 1u));
```

Ogni ridisegno cambia lo sfondo di 1 LSB (invisibile) su TUTTI i pixel -> il
driver riscrive l'area intera -> nessun residuo. **Diagnosi discriminante:**
confrontare cio' che la UI *disegna* (dump) con cio' che si *vede* (webcam):
se il dump e' pulito e il vetro no, il problema e' di refresh, non di layout.

### I titoli di pagina blu (v158) — LA sovrapposizione vera

Sintomo utente: *"il titolo della pagina in blu è sovrascritto sui tasti"*.
Causa: **ogni funzione di pagina** (ui-7) scriveva il proprio titolo con
`draw_text(m, pitch, 40, 200, "Panoramica", C_ACCENT, 4);` -> scala 4 = 64 px,
quindi **y 200..264**, mentre la barra dei tasti sta a **y 170..258**:
sovrapposizione totale. Rimossi tutti (7 occorrenze): il tasto evidenziato
basta e l'utente lo aveva chiesto esplicitamente.

**Metodo che ha trovato la causa (dopo aver sbagliato bersaglio due volte):**
`grep -n C_ACCENT ui-*.c` — elencare TUTTI gli usi del colore/attributo sospetto
e controllare la geometria di ciascuno, invece di correggere il primo che
sembra colpevole. Il testo che avevo tolto prima (topbar, y 108) era quello
giusto ma non quello che l'utente vedeva.

### Attivita' pianificate (v146)

Pagina **SISTEMA → Crontab** (6a pagina, si apre dalla 2a colonna della 2a riga
di tasti): righe di `/etc/crontabs/root` con **MOD** (modifica) e **DEL**
(cancella), piu' **AGGIUNGI** nella barra dei pulsanti di pagina. Ogni azione
scrive SUBITO il file e riavvia `cron` (niente bozza da salvare), con
`awk -v n=N` per la riga giusta: il numero mostrato e' quello REALE del file
(le righe vuote si saltano per la vista ma la numerazione non cambia).
Collaudato dal display: aggiunta di una riga (`x`) e sua cancellazione.

**Firewall: NON toccarlo da qui.** Su questo device `fw4` non e' in esecuzione
e l'interfaccia USB non e' in una zona: un `fw4 reload` avvierebbe il firewall
e taglierebbe fuori il canale preferito. Le regole restano in sola lettura.

### Verifica

- `nx679j-ui-sample.sh` sul device + `nx679j-ui-watch.sh` dal host: campiona
  **release e retire fence separate** (la UI guarda solo la release: con lo
  schermo fermo direbbe comunque "ok").
- Tocchi senza dita: `uinput-touch <x> <y> --hold 7`, poi si legge
  `pagina=<categoria>/<pagina>` nella TELEMETRIA (ogni 600 frame).
- Anteprima: `ui-preview2.c` compilato per l'host rende le pagine in PPM → PNG,
  da guardare PRIMA di toccare il pannello (cosi' si sono trovati il troncamento
  dei nomi lunghi e le liste vuote).


## Verificare che lo schermo MOSTRI davvero (non basta "il commit e' passato")

`/sys/kernel/debug/dri/0/crtc152/fence_status` contiene **piu' sezioni** e le due
fence dicono cose diverse:

- `Release fence` (crtc 152) = il commit e' stato **accettato**
- `Retire fence` (connector 56) = il frame e' stato **ritirato dal pannello**

Un client che guarda solo la release (e' quello che fa `wait_fence` col
OUT_FENCE_PTR) puo' restare a `fence_timeout=0` con lo schermo **fermo**: le due
possono divergere. Criterio operativo: campionare il **retire** due volte a
distanza nota — deve avanzare al ritmo dell'fps; in piu' `msm_drm` IRQ ≈ fps × 3
(es. 45 fps → ~136 IRQ/s).

**Trappola dello strumento (2026-09-24)**: estrarre la fence con `sed ... | tail -1`
prende l'ULTIMA sezione del file, non quella voluta — ha prodotto letture
incomprensibili ("done fermo, commit che cresce") su un pannello sano. Estrarre
per sezione, esplicitamente:
`sed -n '/===Retire fence===/{n;s/.*done_count:\([0-9]*\) commit_count:\([0-9]*\).*/\1\/\2/;p;}'`.

**Trappola `pidof`**: conta anche gli **zombie**; una UI uccisa (parent che non fa
`wait`) resta visibile e fa credere che ci siano piu' istanze. Contare solo i
processi con stato != Z (`awk '{print $3}' /proc/$p/stat`).

## Collasso in idle: la via d'uscita (v139+, esperimento controllato)

Il link DSI collassa se resta senza commit oltre ~58 ms e **non si rianima** (serve
reboot). Il driver pero' espone sul CRTC la proprieta' **`idle_pc_state`** (id 166,
ENUM: `0=idle_pc_none`, `1=idle_pc_enable`, **`2=idle_pc_disable`**).

Esperimento (stesso procedimento, una sola variabile — kill -9 + 20 s di silenzio + riavvio):

| condizione | esito |
|---|---|
| valore **2** (`idle_pc_disable`) | fence completano, **59.9-61.2 fps**, `fence in ritardo: 0` |
| valore **0** (default di boot) | **200 fence consecutivi in ritardo** = pannello morto |

Quindi con la proprieta' attiva il pannello **sopravvive alle pause** e il client DRM si
puo' riavviare a caldo. Conseguenze operative:

- il launcher la attiva al boot (`touch /tmp/ui-idle-pc-disable` **prima** della UI)
- **iterazione UI in ~15 s**: `kill -9 $(pidof nx679j-ui)` -> `scp` del binario -> `setsid ...ui &`
  (niente flash, niente reboot). `scp` sul binario in esecuzione da' "Text file busy": prima si uccide
- la proprieta' e' **appiccicosa**: smettere di scriverla NON riporta il default. Va scritta
  sempre in modo esplicito (`g_idle_pc_off ? 2 : 0`), altrimenti il flag file non commuta nulla
- attivarla **dopo** il collasso non lo resuscita (verificato: 200 timeout restano)
- il commit atomico puo' portare la proprieta' solo se appartiene **davvero** al CRTC:
  verificarlo con `OBJ_GETPROPS` (`0xc02064b9`, obj_type `0xcccccccc`) — un nome puo'
  esistere su piu' oggetti e un commit con la proprieta' sbagliata viene rifiutato in blocco
- resta comunque il commit continuo: la proprieta' e' la rete di sicurezza, non il meccanismo

La UI scrive sempre `idle_pc_state` in `commit_plane_only` **nello stesso blocco del
CRTC** dell'out-fence: due blocchi per lo stesso oggetto fanno rifiutare il commit.

## Leggere lo stato senza indovinare

`drm-probe` (compilato da `drm-probe.c`, sola lettura) apre card0 **senza** SET_MASTER —
sicuro se un altro client e' master, pericoloso solo se nessuno lo e' — ed elenca le
proprieta' di un oggetto con il valore attuale:

```sh
/tmp/drm-probe --crtc 152        # 54 proprieta' del CRTC, incl. idle_pc_state e sde_drm_roi_v1
/tmp/drm-probe idle_pc_state autorefresh
```

## Regole che restano

- il client DRM si riavvia a caldo **solo con `idle_pc_disable` attivo**; senza, un riavvio
  uccide il pannello fino al reboot
- se `pidof nx679j-ui` e' 0 ma il display e' acceso, e' kiosk3 che sta disegnando
- **`pidof` conta anche gli zombie**: dopo un kill non raccolto il conteggio e' >1 e non
  significa istanze multiple. Verificare con `awk '/^State:/{print $2}' /proc/PID/status`
  (stato `Z` = zombie) oppure con `/sys/kernel/debug/dri/0/clients` (una riga per client)
- i font sono estratti dal kernel (`ui-font8x16.h`, GPL-2.0); `draw_text_fit` non
  scende sotto scala 2 (sotto non si legge su un telefono)
