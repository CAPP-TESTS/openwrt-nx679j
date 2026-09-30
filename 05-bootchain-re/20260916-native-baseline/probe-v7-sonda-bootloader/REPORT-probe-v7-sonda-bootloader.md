# Sonda v7/v7b — variante RESTART2 con reason `bootloader` ("sonda bootloader")

Data: 2026-09-17. Solo lavoro locale (build, verifica, QEMU full-system).
**Il telefono non è stato toccato**: nessun adb/fastboot/dd, nessuna connessione
al device.

Unica differenza rispetto alla coppia già verificata `probe-v7-sonda`
(v7/v7b): la chiamata di riavvio.

| | v7 / v7b | questa variante |
|---|---|---|
| syscall | `syscall(SYS_reboot, MAGIC1, MAGIC2, LINUX_REBOOT_CMD_RESTART, NULL)` | `syscall(SYS_reboot, MAGIC1, MAGIC2, LINUX_REBOOT_CMD_RESTART2, "bootloader")` |
| cmd | `0x01234567` | `0xa1b2c3d4` |
| 4º argomento | `NULL` | puntatore a `static const char reset_reason[] = "bootloader"` |

Tutto il resto è identico: statico, nessun `PT_INTERP`, nessun `DT_NEEDED`,
`main()` che non può tornare, nessun `exit/abort`, fallback `mknod` di `/dev/kmsg`,
stesso container Magisk, stesse ramdisk sorgente, stesso metodo di innesto
in-place.

## 1. Artefatti (path + sha256)

```
/home/user/nx679j-stock/experiments/20260916-122926-native-baseline/probe-v7-sonda-bootloader/
  sonda-bootloader-v7.img   100663296 B  sha256=b099bf136895ad6ce3ace744a86d2f6beded63560cd295e2713f1dec1fe0fb51
  sonda-bootloader-v7b.img  100663296 B  sha256=610cfea53d8e06b0b8accee4775b71e34d7077acba7aaa4e2a8d812c482050eb
  candidate-init-sonda-bootloader.c       (sorgente del probe)
  work/init-sonda-bootloader  532352 B    sha256=7b2e0859173909e10e7bc27c945e446a13656506d1a03a3bfa753810b10174af
  v7-ramdisk.lz4   sha256=2cf26721dbb74adb0815dac4df0142695bd77d37d675d1f07b771021bc7e5873
  v7b-ramdisk.lz4  sha256=4501c801f9700b0cfa4c83887fa35cd63a53653aac2b4f41956c31f165301c5f
  build-probe-v7bl.py / verify-probe-v7bl.py / qemu-probe-v7bl.py
  manifest.json, build-probe-v7bl.log, VERIFY-probe-v7bl.log, verify-probe-v7bl.json,
  qemu-probe-v7bl.json, qemu/boot-v7.log, qemu/boot-v7b.log
```

v7 = container Magisk provato + ramdisk v6 (66 voci) con `/init` sostituito.
v7b = stesso container + ramdisk Android (30 voci) con `/init` sostituito.

## 2. Binario `/init` — readelf e disassemblaggio

`readelf -h` (dal file spedito dentro l'immagine):

```
Class: ELF64   Data: 2's complement, little endian
Type: EXEC (Executable file)
Machine: AArch64
Entry point address: 0x4007c0
```

* `readelf -l`: `PT_INTERP` = **0** (7 program header, nessun INTERP)
* `readelf -d`: **"There is no dynamic section in this file."** → `DT_NEEDED` = 0
* `file`: `ELF 64-bit LSB executable, ARM aarch64, statically linked, ... stripped`
* `.text` spedito == `.text` di una ricompilazione fresh del sorgente
  (`348776 B sha a19de3c219f3aa04`), quindi il disassemblaggio qui sotto è il
  codice realmente spedito.
* `main()`: 141 istruzioni, **nessun `ret`**, nessun salto a
  `exit/_exit/abort/quick_exit`, ultima istruzione `b 40056c <main+0xb4>`.
* il sorgente (commenti/stringhe rimossi) non contiene `exit(`/`abort(`.

Riga di disassemblaggio che prova RESTART2 + `bootloader`
(`work/objdump-main-sonda-bootloader.txt`, funzione `main`, binario non-stripped
usato solo per leggere i nomi — stesso `.text`):

```
  400628:	aa1603e4 	mov	x4, x22            ; x22 = adrp 455000 + add #0x709 -> 0x455709 = "bootloader"
  40062c:	52987a83 	mov	w3, #0xc3d4        // #50132
  400630:	52832d22 	mov	w2, #0x1969        // #6505
  400634:	529bd5a1 	mov	w1, #0xdead        // #57005
  400638:	b90002bf 	str	wzr, [x21]
  40063c:	72b43643 	movk	w3, #0xa1b2, lsl #16   ; w3 = 0xa1b2c3d4 = LINUX_REBOOT_CMD_RESTART2
  400640:	72a50242 	movk	w2, #0x2812, lsl #16   ; w2 = 0x28121969 = MAGIC2
  400644:	72bfdc21 	movk	w1, #0xfee1, lsl #16   ; w1 = 0xfee1dead = MAGIC1
  400648:	d28011c0 	mov	x0, #0x8e          // #142 = __NR_reboot
  40064c:	9400280d 	bl	40a680 <syscall>
```

(le istruzioni sono quelle realmente stampate da `objdump -d`; i commenti
`;` sono aggiunti qui. `0x455709` è la VA risolta dal costruttore `adrp/add`
di `x22` a `4005e4`/`4005f0`.)

Riscontro automatico (metodo del register-tracker, dal build):

```
0x40064c: bl syscall  with x0=142 (__NR_reboot), x1=0xfee1dead (MAGIC1),
          x2=0x28121969 (MAGIC2), x3=0xa1b2c3d4 (LINUX_REBOOT_CMD_RESTART2),
          x4=0x455709 -> file offset 349961 = b'bootloader\x00'
```

I valori `__NR_reboot=142`, `MAGIC1`, `MAGIC2`, `RESTART2=0xa1b2c3d4` sono letti
dagli header del compilatore (`.../aarch64-linux-gnu/include/{asm/unistd.h,linux/reboot.h}`),
non hardcoded nello script. In `main()` `0x01234567` (RESTART semplice) **non
compare**.

In verifica lo stesso puntatore è confermato con due metodi indipendenti:
* scansione testuale di `objdump -d` (nessun register tracker): sequenza
  `mov w3,#0xc3d4` + `movk w3,#0xa1b2,lsl #16` + `mov x0,#0x8e` prima di
  `bl syscall`, con il 4º argomento costruito da `adrp/add` a `0x455709`;
* `objdump -s -j .rodata` alla stessa VA:
  `455700 736c6565 70696e67 00626f6f 746c6f61  sleeping.bootloa` → i byte a
  `0x455709` sono `bootloader\0`;
* mappatura VA→file offset via `PT_LOAD` **del binario estratto dall'immagine**
  (`0x455709 -> offset 349961 = b'bootloader\x00'`).

## 3. Verifiche sulle due immagini

* lunghezza **100663296** B per entrambe (= intera partizione boot_b).
* header v4: `kernel_size=49108324`, `ramdisk_size` = 1014206 (v7) / 2352934
  (v7b), `header_size=1584`, `header_version=4`, `signature_size=4096`.
* area kernel **byte-identica** al container Magisk che il 2026-09-17 è stato
  bootato da slot B (sha `f7ea707eb916bf1f` su 49111040 B); nulla toccato dopo
  la ramdisk, byte-identico agli offset assoluti (50533954 B / 49195226 B di coda).
* tutte le differenze vs container confinate al campo `ramdisk_size` (offset
  12..15) e alla regione ramdisk: **0 byte fuori**.
* differenze vs `boot_b-probe-v7.img` / `-v7b.img` confinate al campo size e alla
  regione ramdisk: **0 byte fuori** → "il resto è identico alle v7/v7b".
* ramdisk: frame lz4 legacy (`02214c18`), round-trip `lz4 -dc` → cpio identico
  byte-per-byte; albero estratto con `bsdtar` == albero sorgente tranne `/init`;
  metadata (mode/nlink/uid/gid) di tutte le 59/29 voci invariate; `/init` con il
  mode originale (`-rwxr-xr-x` per v7, `-rwxr-x---` per v7b).
* `/init` nell'immagine = il probe (sha `7b2e08…`) e contiene il letterale
  `bootloader\0` all'offset 349961.

Tally: build **senza errori** (script fail-fast, exit 0);
`verify-probe-v7bl.py` **40/40 PASS**; `qemu-probe-v7bl.py` **14/14 PASS**.

## 4. QEMU full-system (kernel del device, `-no-reboot`)

Comando (identico per v7 e v7b):

```
qemu-system-aarch64 -machine virt -cpu cortex-a57 -accel tcg -smp 2 -m 2048 \
  -nodefaults -nic none -display none -monitor none -serial stdio -no-reboot \
  -kernel qemu/v7-kernel -initrd qemu/v7-ramdisk \
  -append "console=ttyAMA0 earlycon loglevel=8 panic=10 rdinit=/init"
```

Output reale (estratto da `qemu/boot-v7.log`, `qemu/boot-v7b.log`):

```
[    0.687058][    T1] nx679j-sonda: init reached userspace as pid 1 reboot reason=bootloader (LINUX_REBOOT_CMD_RESTART2) attempt=1 uptime_ms=626
[    0.687477][    T1] nx679j-sonda: marker channel /dev/kmsg attempt=1 uptime_ms=627
[    0.687677][    T1] nx679j-sonda: calling reboot(2) LINUX_REBOOT_CMD_RESTART2 arg='bootloader' -> ABL is expected to come up in fastboot attempt=1 uptime_ms=627
[    0.689374][    T1] reboot: Restarting system with command 'bootloader'
```

(QEMU esce da solo, exit code 0. v7b identico: marker a 0.700198, restart a
0.703390, exit code 0. Nessun panic, nessun "Attempted to kill init".)

Quindi: il kernel del device prende davvero la stringa dal puntatore userspace e
la instrada nel path RESTART2 — la riga
`reboot: Restarting system with command 'bootloader'` è la prova, non
un'ipotesi. Lo stesso kernel contiene entrambe le stringhe
`reboot: Restarting system` e `reboot: Restarting system with command '%s'`.

## 5. Cosa NON è provato

1. **Che l'ABL di questa unità onori il reason `bootloader`.** QEMU non ha
   bootloader: nulla qui dice che il telefono si presenterà in fastboot.
   È un'ipotesi (documentata per Qualcomm), non un fatto.
2. Nessuna evidenza, offline, che il kernel del device inoltri la stringa a un
   canale letto dall'ABL: nella `Image` del kernel (non compressa) **non**
   compaiono `restart_reason`, `reset_reason`, `reboot_reason`, `PON_REASON`,
   `SYSTEM_RESET2`, `qcom_scm`, `msm_restart`, `IMEM`/`imem`, né una tabella che
   mappi `"bootloader"` (le 8 occorrenze di `bootloader` sono LCD/misc e messaggi
   di boot). L'assenza di stringhe non è prova di assenza (i simboli possono non
   essere stringhe), ma non c'è supporto positivo.
3. Che ABL/AVB accetti queste immagini sul telefono, e l'effetto sul contatore
   A/B (per costruzione non osservabile in QEMU).
4. Che il timing/ordine della sonda cambi qualcosa: come per v7/v7b, se la
   sonda non parte il telefono resta come dopo v5/v6 (nessun reset).

Modi in cui la variante può dare un falso negativo (non un guasto): se il path
di restart della piattaforma ignora la stringa, il telefono si riavvia
normalmente senza entrare in fastboot; se `strncpy_from_user` fallisse il kernel
restituirebbe `-EFAULT` e la riga di marker riporterebbe `rc=/errno=`.

## 6. Nota operativa (per chi flasherà)

- L'esito atteso, lato host: il telefono ricompare come USB **`18d1:d00d`**
  (fastboot) invece di restare sul logo. Da lì si esce con `fastboot reboot`
  (o si resta in fastboot per riflashare).
- Non ho eseguito nulla di tutto questo: nessun comando verso il device.
- Attenzione a non confondere il device in fastboot con il flashing in corso:
  `fastboot devices`/`lsusb` vanno letti prima di inviare comandi.
