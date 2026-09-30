---
name: kernel-debug-validate
description: Valida e debugga codice kernel Linux — build con W=1, checkpatch --strict, sparse/smatch, sanitizer KASAN/UBSAN/lockdep/kmemleak, decodifica di oops, panic e stack trace, ftrace e dynamic debug, test di load/unload in QEMU. Usa questa skill dopo aver scritto o modificato un modulo kernel, quando si analizza un kernel oops o un panic da dmesg, quando un driver si blocca o perde memoria, o quando serve un ambiente di test sicuro invece del kernel dell'host. Contiene la configurazione minima di una VM di test.
version: 1.0.0
license: MIT
metadata:
  hermes:
    tags: [linux-kernel, debugging, kasan, lockdep, qemu, oops, validation]
---

# Kernel debug e validazione

Nel kernel non esiste il debugging per tentativi: un errore non ti dà uno stack trace pulito e un altro tentativo, ti dà una macchina bloccata e un filesystem da controllare. Il modo di lavorare è quindi rovesciato rispetto a userspace — si investe prima, in strumenti che rendono i bug visibili subito, invece di inseguirli dopo.

## Il ciclo in un comando

I primi tre passi sono automatizzati:

```bash
scripts/kcheck.sh [directory-del-modulo]
```

Esegue build con `W=1`, `checkpatch --strict`, sparse quando disponibile, e ispeziona il `.ko` prodotto — licenza, alias, riferimenti a floating point, coesistenza fra spinlock e funzioni che dormono. Rileva da solo se il kernel e' costruito con clang e imposta `LLVM=1`. Esce con codice diverso da zero se qualcosa non passa.

Non carica il modulo: quella resta una decisione separata, da prendere in QEMU.

## Il ciclo, in ordine di costo

Ogni passo trova una classe di problemi che il precedente non vede. L'ordine è per costo crescente: non ha senso avviare una VM per un bug che `checkpatch` avrebbe segnalato in due secondi.

### 1. Compilazione con warning attivi

```bash
K=/usr/lib/modules/$(uname -r)/build
make -C "$K" M=$PWD W=1 modules
```

`W=1` attiva i warning che nel kernel indicano quasi sempre bug reali: prototipi mancanti, funzioni statiche non usate, confronti fra tipi con segno diverso, cast sospetti. Un warning nuovo non si ignora.

### 2. checkpatch

```bash
$K/scripts/checkpatch.pl --strict --no-tree -f driver.c
```

Su una patch invece che su un file:

```bash
$K/scripts/checkpatch.pl --strict -g HEAD
```

Non è solo stile: intercetta errori sostanziali come `printk` senza livello, `msleep` con valori troppo brevi, allocazioni con `sizeof(struct x)` invece di `sizeof(*ptr)`, uso deprecato di API rimosse. Quando decidi di ignorare un avviso, la ragione va scritta.

### 3. Analisi statica

`sparse` e `smatch` trovano una classe di bug che il compilatore non vede: il primo verifica le annotazioni `__user` e `__iomem`, l'endianness e i contesti di lock; il secondo fa analisi di flusso e intercetta null deref e off-by-one.

Per sapere se ci sono su questa macchina, e con quale comando installarli qui, usa `binary-recon/scripts/doctor.sh`: nomi dei pacchetti e gestore variano da distribuzione a distribuzione.

Uso:

```bash
make -C "$K" M=$PWD C=2 CF="-D__CHECK_ENDIAN__" modules             # sparse
make -C "$K" M=$PWD C=2 CHECK="smatch -p=kernel" modules            # smatch
```

`sparse` è particolarmente utile in questo dominio: verifica che i puntatori `__iomem` non vengano dereferenziati direttamente e che i dati `__user` passino sempre per `copy_from_user`. Sono esattamente gli errori tipici di chi porta codice da userspace.

### 4. Kernel di test con i sanitizer

I sanitizer non si attivano su un modulo: sono opzioni del kernel su cui il modulo gira. Servono quindi in una VM con un kernel compilato apposta:

```
CONFIG_KASAN=y                    # use-after-free, buffer overflow
CONFIG_UBSAN=y                    # comportamento indefinito
CONFIG_PROVE_LOCKING=y            # lockdep: ordini di lock errati, deadlock potenziali
CONFIG_DEBUG_ATOMIC_SLEEP=y       # sleep in contesto atomico
CONFIG_DEBUG_KMEMLEAK=y           # memory leak
CONFIG_DEBUG_OBJECTS=y
CONFIG_DEBUG_SPINLOCK=y
CONFIG_DEBUG_MUTEXES=y
CONFIG_DMA_API_DEBUG=y            # mapping DMA sbagliati o non liberati
CONFIG_FRAME_POINTER=y            # stack trace leggibili
CONFIG_DEBUG_INFO_DWARF5=y
CONFIG_GDB_SCRIPTS=y              # script gdb del kernel
```

Il valore di questa configurazione: `lockdep` trova il deadlock la prima volta che l'ordine di lock è sbagliato, anche se il deadlock reale si verificherebbe una volta su un milione. `KASAN` intercetta l'use-after-free nell'istante dell'accesso, non tre secondi dopo quando la memoria è già stata riutilizzata da qualcun altro.

## Test in QEMU

**Il modulo sperimentale non si carica sul kernel dell'host.** Un `insmod` sbagliato non produce un errore recuperabile: produce un kernel panic, e con la peggior fortuna un filesystem da riparare.

Il ciclo minimo con il kernel dell'host più un initramfs di test:

```bash
qemu-system-x86_64 \
  -kernel /boot/vmlinuz-linux \
  -initrd /tmp/test-initramfs.img \
  -append "console=ttyS0 rdinit=/init" \
  -m 2G -smp 2 -nographic -no-reboot
```

Per lavorare seriamente conviene un kernel compilato con i sanitizer sopra e una rootfs minimale. `virtme-ng` semplifica molto il ciclo (avvia una VM sul kernel appena compilato usando il filesystem dell'host in sola lettura) e non è installato:

```bash
# Disponibile come pacchetto su alcune distribuzioni, altrimenti via pip:
#   pipx install virtme-ng
# Il comando esatto per questo sistema lo indica binary-recon/scripts/doctor.sh
vng --build && vng -- insmod /path/mydev.ko
```

Debugging con GDB sul kernel della VM: aggiungi `-s -S` a QEMU, poi

```bash
gdb vmlinux -ex 'target remote :1234'
# per un modulo: lx-symbols (dagli script GDB del kernel) carica i simboli al posto giusto
```

Se il dispositivo è emulabile da QEMU (device PCI o USB standard), la VM diventa un banco di prova completo. Se serve hardware fisico, quello è il momento di dirlo esplicitamente all'utente, con il rischio concreto.

## Test del ciclo di vita

Un driver che carica non è un driver che funziona. Le prove che contano davvero:

```bash
for i in $(seq 100); do insmod mydev.ko && rmmod mydev || break; done
```

Cento cicli load/unload con `kmemleak` attivo, poi:

```bash
echo scan > /sys/kernel/debug/kmemleak
cat /sys/kernel/debug/kmemleak
```

Le altre prove indispensabili, in ordine di frequenza con cui rivelano bug:

- **Fallimento di `probe()`**: forza ogni percorso di errore. È il codice meno testato di ogni driver, ed è quello che gira quando qualcosa va storto.
- **Rimozione durante l'uso**: `rmmod` mentre un processo tiene aperto il device, o disconnessione fisica su USB. Rivela subito i riferimenti mancanti.
- **Sospensione e ripresa**: `echo mem > /sys/power/state` nella VM.
- **Accessi concorrenti**: più processi che usano il device insieme, con `lockdep` attivo.
- **Input malformato**: ioctl con valori fuori range, dimensioni assurde, puntatori non validi.

## Leggere un oops

Un oops contiene già quasi tutta la diagnosi, se lo si legge nell'ordine giusto.

```
BUG: kernel NULL pointer dereference, address: 00**************
...
RIP: 0010:mydev_probe+0x4a/0x120 [mydev]
...
Call Trace:
 platform_probe+0x44/0xa0
 really_probe+0x1be/0x3f0
```

Il percorso di lettura:

1. **La prima riga** classifica il guasto: NULL deref, general protection fault, `BUG: unable to handle page fault`, `KASAN: use-after-free`.
2. **`RIP`** dice dove: funzione, offset nella funzione, e fra parentesi quadre il modulo.
3. **`Call Trace`** dice come ci si è arrivati.
4. **`Tainted:`** dice se il kernel era già compromesso da un modulo precedente, il che può cambiare l'interpretazione.

Dall'offset alla riga di codice:

```bash
$K/scripts/decode_stacktrace.sh vmlinux < oops.txt        # decodifica l'intero trace
addr2line -e mydev.ko -f -i 0x4a                          # singolo offset in un modulo
objdump -dS --start-address=0x40 --stop-address=0x60 mydev.o
```

Errori tipici e loro traduzione:

- Indirizzo piccolo (`0x0`–`0x100`): dereferenziazione di NULL più l'offset di un campo. L'offset identifica *quale* campo, e con `pahole` risali alla struttura.
- Indirizzo tipo `0x6b6b6b6b6b6b6b6b`: memoria liberata (`SLUB_DEBUG` poison). Use-after-free.
- `0xa5a5a5a5`: memoria non inizializzata.
- Indirizzo enorme e sensato ma non mappato: puntatore corrotto, spesso da un overflow adiacente.

## Debugging dinamico

Log condizionali senza ricompilare:

```bash
echo 'module mydev +p' > /sys/kernel/debug/dynamic_debug/control     # abilita i dev_dbg
echo 'file mydev.c line 120 +p' > /sys/kernel/debug/dynamic_debug/control
```

Tracciamento delle chiamate:

```bash
cd /sys/kernel/debug/tracing
echo function_graph > current_tracer
echo 'mydev_*' > set_ftrace_filter
echo 1 > tracing_on
cat trace_pipe
```

`ftrace` con `function_graph` è lo strumento più sottovalutato del kernel: mostra la sequenza reale delle chiamate con i tempi, e risponde in un minuto a domande che con i `printk` richiedono dieci ricompilazioni.

Per l'ispezione di strutture dati:

```bash
pahole -C mydev vmlinux         # layout, buchi di padding, dimensione cache line
```

## Il criterio di completamento

Il lavoro è verificato quando:

- `make W=1` non produce warning nuovi;
- `checkpatch --strict` è pulito, o ogni eccezione ha una motivazione scritta;
- il modulo carica e scarica cento volte senza leak né warning di `lockdep`;
- ogni percorso di errore di `probe()` è stato forzato almeno una volta;
- rimozione durante l'uso e ciclo suspend/resume non producono oops;
- ogni valore magico nel codice è tracciabile a un fatto documentato.

Se uno di questi punti manca, è più onesto dirlo che dichiarare finito il lavoro. Un driver "funziona sul mio hardware" è un'ipotesi, non un risultato.
