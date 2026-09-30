# "Non c'è" non è un risultato (triade falsa-assenza)

Regola: **non dichiarare mai l'assenza di qualcosa da una sola sonda.** Una sonda
che dice "non c'è" ha quasi sempre un'assunzione non verificata al suo interno:
il percorso, il nome, lo strumento o un prerequisito. Se non ho controllato
quelle, il risultato è "la mia sonda non ha trovato nulla", non "non esiste".

## Le cinque conferme del 2026-09-25 (stesso giorno, stesso errore)

| Detto | Verità | Assunzione sbagliata |
|---|---|---|
| "il device tree non dichiara il tasto" | il nodo c'era | percorso: `/proc/device-tree` invece di `/sys/firmware/devicetree/base` |
| "il driver PON non esiste" | esiste, si chiama `qcom-pon.ko` | nome: cercavo `qpnp-power-on.ko` |
| "qmicli non sa leggere la CA" | il comando esiste, il pacchetto è ridotto | non avevo letto quale collection fosse compilata |
| "il modem non risponde" | rispondeva, scrive su stderr | `2>/dev/null` buttava via i dati |
| "opkg non trova i pacchetti" | i pacchetti ci sono | lo strumento è `apk`, non `opkg` (OpenWrt 25.12) |

E una variante: `timeout` non esiste sul device (busybox) → ogni query con
`timeout N` falliva in silenzio, e sembrava che l'hardware non rispondesse. Il
comando che manca è un risultato, non un'assenza.

## Procedura prima di dire "non c'è"

1. **Mai sopprimere lo stderr in diagnosi.** `2>/dev/null` trasforma
   "Permission denied" e "No such file" in un silenzio che sembra assenza.
   Togli il redirect: la maggior parte dei falsi negativi sparisce subito.
2. **Due vie indipendenti.** Percorso alternativo (es. `/proc/...` vs `/sys/...`),
   nome alternativo (pattern largo: `find / -name '*pon*'`), strumento
   alternativo. Una via sola non basta mai.
3. **Chiedi a chi sa.** Il `dmesg` è la voce del driver: se un driver si carica e
   non fa nulla, il probe loggato dice **perché**. Preferisci sempre il log del
   soggetto alla mia ipotesi sul soggetto.
4. **Verifica la catena dei prerequisiti prima di concludere.** Un modulo può
   essere "non supportato" solo perché manca chi crea il dispositivo da legare:
   `pm8941-pwrkey` non si lega finché non carichi il PON (`qcom-pon`). Se il
   soggetto ha un *provider*, il provider è parte della sonda.
5. **Il negativo va scritto come negativo condizionato**: "con questi percorsi,
   questi nomi e questo strumento non l'ho trovato" — mai "non esiste".

## Corollario: il verdetto dell'oracolo

Un sistema funzionante (Android sullo stesso hardware) che *fa* la cosa X
prova che X è possibile **sull'hardware**, quindi il negativo che avevo in mano
era per forza un negativo della mia sonda. Prima di dichiarare un limite
dell'hardware: confrontare con l'oracolo (kernel, moduli, device tree,
dispositivi input). Nel caso di oggi: stesso kernel, stessi moduli → la
differenza era nel *caricamento*, non nella disponibilità.
