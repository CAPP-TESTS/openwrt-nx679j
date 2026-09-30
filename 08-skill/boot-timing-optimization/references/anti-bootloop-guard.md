# Guardia anti-boot-loop per esperimenti sull'ordine di avvio

Depth per quando un esperimento di boot puo' mandare la board in reset o in boot-loop (ordine di
servizi, gate di tempo, bring-up anticipato di sottosistemi). La regola sempre valida sta nel SKILL.md;
qui la ricetta e le trappole del marker.

## La guardia che si auto-annulla

Non si fa "sperando": si scrive una guardia che dopo N fallimenti torna da sola alla modalita'
prudente, in uno storage che **sopravvive al riavvio** (settore dedicato, slot rawdump/misc, U-Boot env).

```sh
# lettura + incremento PRIMA dell'esperimento
RAW=$(dd if=$RD bs=32768 skip=$SLOT count=1 2>/dev/null | head -c 24)
case "$RAW" in *EXP-*) N=${RAW##*EXP- }; N=${N%% *}; N=$((N+1));; *) N=1;; esac
if [ "$N" -ge 3 ]; then MAX=300; else MAX=18; printf '%-23s' "EXP-$N" > /tmp/mk; dd if=/tmp/mk of=$RD bs=32768 seek=$SLOT conv=sync,notrunc; fi
# ...e a FINE script riuscito (se siamo arrivati qui, la board ha retto):
printf '%-23s' "EXP-OK" > /tmp/mk; dd if=/tmp/mk of=$RD bs=32768 seek=$SLOT conv=sync,notrunc
```

Con questa, un fallimento costa due riavvii, non un boot-loop che richiede fastboot/EDL.

## Il marker va chiuso con un esito, a ogni uscita

Il marker serve contro il **crash di chi lo scrive** (morte a meta' sequenza), non contro un boot
brutto. Se il marker significa "tentato, nessun esito" e il codice esce li', **un solo boot andato
male lascia l'utente con lo schermo nero anche al boot dopo** — e la diagnosi e' controintuitiva:
sembra hardware, mentre nel log del launcher c'e' scritto *skip*.

1. scrivi il marker di esito **a ogni uscita normale**, comprese le uscite di fallimento;
2. se lo trovi aperto, **riprova contando i tentativi** (1, 2, 3) e arrenditi solo dal terzo, invece
   di saltare subito;
3. quando qualcosa non appare, **leggi log e marker del launcher prima di sospettare l'hardware**;
4. **il contatore sopravvive al flash.** Una serie di boot interrotti a meta' (campagne, `pkill`,
   riavvii forzati) lascia il contatore a "fallito" e la guardia passa in prudente **in silenzio**:
   osservato il percorso partire a 93 s invece di 37 s, senza nessuna modifica che lo giustificasse.
   Quando i tempi peggiorano senza una causa nuova, leggi e **azzera il marker** prima di indagare il
   percorso critico.

## Scegliere lo slot

**Inventaria prima di scrivere**: gli slot "liberi" sono condivisi con altri componenti (uno slot
dato per libero si e' rivelato occupato da un check-point di un altro sottosistema, e uno dei tuoi
marker e' stato trovato con un valore di formato estraneo).

- Prefissa i tuoi valori (`DL-…`, `RC-…`, `EXP-…`) e progetta la **lettura** perche' un valore
  estraneo significhi *procedi*, mai *bloccati*.
- Annota la mappa degli slot nella documentazione di continuita' del progetto, cosi' la sessione
  successiva non la re-inventa.
- Ricorda che scrivere il tuo marker in uno slot altrui **cancella** il dato di quell'altro
  componente: se non hai uno slot verificato, preferisci un file in una partizione piccola dedicata
  o un settore riservato, non un rawdump condiviso.
