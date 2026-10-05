# reproducible_builds.md — OpenWrt su Nubia RedMagic 7 (NX679J)

**Manuale passo-passo: firmware → sblocco → blob → build → flash → avvio, prima della v10 (sezioni 1–6) e poi della v90 completa (sezioni 7–9)**

Ricostruito dal repository `openwrt-nx679j` (commit `f9a92fc`). Data: 5 ottobre 2026.

---

## 0. Leggere prima di iniziare

### 0.1 Cosa ottieni seguendo questo manuale

Il manuale procede in due tappe.

1. **Traguardo v10** (sezioni 1–6): il telefono avvia dallo **slot B** un ramdisk personalizzato sopra il kernel originale del produttore, entra in un **chroot OpenWrt 25.12.5** e risponde in **SSH** su `10.0.0.1` attraverso il cavo USB. Si ricostruisce interamente dagli script del repository.
2. **Versione completa v90** (sezioni 7–9): l'immagine prodotta dal builder finale degli autori, `build-v90.py`, con Wi-Fi, LuCI, modem X65 via QRTR e interfaccia sul display.

La seconda tappa ha un limite da conoscere subito: `build-v90.py` parte da un archivio del rootfs "vivo" degli autori (`owrt-live19.tar.gz`) che **non è pubblicato**. Il manuale lo **ricostruisce** dal rootfs ufficiale, dai pacchetti dei feed OpenWrt e dai file del progetto (passo 7.5). Il risultato è un'immagine **funzionalmente equivalente**, non identica byte per byte, con ModemManager e libqmi standard al posto delle versioni patchate degli autori (dettagli al passo 7.0).

### 0.2 Rischio

> **Questa procedura può rendere il telefono inutilizzabile in modo permanente.**
>
> Sull'esemplare degli autori la modalità di emergenza Qualcomm EDL (9008) è risultata **non funzionante**. Non contare su di essa. **L'unica rete di sicurezza è lo slot A** (Android originale con root). Il manuale non scrive mai sullo slot A dopo la sezione 3, e tu non devi farlo.
>
> Lo **sblocco del bootloader cancella tutti i dati** del telefono.
>
> Procedi solo su un telefono tuo, dopo aver fatto un backup completo, e accettando il rischio.

### 0.3 Regole per seguire il manuale

1. Esegui i comandi **uno alla volta, nell'ordine**, copiandoli esattamente.
2. I blocchi `bash` vanno eseguiti sul **PC**. Quando un comando va eseguito **sul telefono**, è indicato esplicitamente.
3. Ogni volta che trovi un riquadro **🛑 STOP**, controlla la condizione. **Se non è soddisfatta, fermati.** Non andare avanti "per vedere cosa succede".
4. Non saltare i controlli degli hash (`sha256sum`). Sono loro che impediscono di mandare un'immagine sbagliata al telefono.
5. Le righe che iniziano con `#` sono commenti: non vanno eseguite.

### 0.4 Cosa ti serve

| Cosa | Dettaglio |
|---|---|
| Telefono | Nubia RedMagic 7, modello **NX679J**, variante globale (NA/Asia), firmware **V311** (verificato ai passi 2.1 e 3.4) |
| PC | Linux **Debian 12 / Ubuntu 22.04 o 24.04**, 64 bit, con accesso `sudo` |
| Spazio disco | almeno **25 GB** liberi |
| Cavo | USB-C dati (non solo ricarica), collegato direttamente al PC, senza hub |
| Batteria | telefono carico almeno al **70%** |
| Rete | connessione Internet sul PC (circa 4 GB di download) |

---

## 1. Preparare il PC e scaricare il firmware ufficiale V311

### 1.1 Installare i pacchetti

```bash
sudo apt update
sudo apt install -y git wget curl unzip python3 lz4 cpio file binutils xxd \
    gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
    device-tree-compiler mkbootimg e2fsprogs openssh-client \
    netcat-openbsd android-sdk-platform-tools-common
```

> Se il pacchetto `mkbootimg` non viene trovato dalla tua versione di Ubuntu/Debian, installalo dal sorgente AOSP:
>
> ```bash
> git clone https://android.googlesource.com/platform/system/tools/mkbootimg ~/mkbootimg
> sudo ln -sf ~/mkbootimg/mkbootimg.py /usr/local/bin/mkbootimg
> sudo ln -sf ~/mkbootimg/unpack_bootimg.py /usr/local/bin/unpack_bootimg
> ```

Verifica che gli strumenti principali rispondano:

```bash
aarch64-linux-gnu-gcc --version | head -1
lz4 --version
mkbootimg --help | head -1
unpack_bootimg --help | head -1
dtc --version
```

> **🛑 STOP** se uno di questi comandi risponde `command not found`.

### 1.2 Installare adb e fastboot ufficiali (Google platform-tools)

```bash
cd ~
wget https://dl.google.com/android/repository/platform-tools-latest-linux.zip
unzip -o platform-tools-latest-linux.zip
echo 'export PATH="$HOME/platform-tools:$PATH"' >> ~/.bashrc
source ~/.bashrc
adb version
fastboot --version
```

### 1.3 Creare le cartelle di lavoro

Questo manuale usa sempre due variabili. **Ripeti questi due `export` ogni volta che apri un nuovo terminale.**

```bash
export NX="$HOME/nx679j-stock"
export REPO="$HOME/openwrt-nx679j"
mkdir -p "$NX/firmware" "$NX/backup" "$NX/experiments"
```

### 1.4 Scaricare il firmware ufficiale V311

La versione corretta è **V311** (North America & Asia). Il repository dimostra che i bootloader del telefono degli autori sono identici byte per byte a quelli dell'OTA V311. La **V411 è la build europea**: certificati e hash diversi, non usarla su un esemplare globale.

```bash
cd "$NX/firmware"
wget -c "https://rom.download.nubia.com/Europe%26Asia/NX679J/V311/NX679J-update.zip"
ls -l NX679J-update.zip
```

> **🛑 STOP** se il file non pesa **3916284140 byte** (circa 3,9 GB). Se il download si interrompe, rilancia lo stesso comando `wget -c`: riprende da dove si era fermato.

### 1.5 Estrarre le immagini con payload-dumper-go

```bash
cd "$NX/firmware"
URL=$(curl -s https://api.github.com/repos/ssut/payload-dumper-go/releases/latest \
      | grep browser_download_url | grep linux_amd64 | cut -d'"' -f4)
echo "$URL"
wget -O pdg.tar.gz "$URL"
tar -xzf pdg.tar.gz payload-dumper-go
chmod +x payload-dumper-go

unzip -o NX679J-update.zip payload.bin payload_properties.txt
./payload-dumper-go -o extracted payload.bin
ls -l extracted/boot.img extracted/vendor_boot.img extracted/dtbo.img
```

> **🛑 STOP** se `echo "$URL"` stampa una riga vuota (in quel caso apri `https://github.com/ssut/payload-dumper-go/releases`, scarica a mano il file `linux_amd64.tar.gz` nella cartella `$NX/firmware` con nome `pdg.tar.gz` e riprendi dal comando `tar`), oppure se manca uno dei tre file `.img`.

Controllo di integrità del payload, lo stesso usato dagli autori: l'hash del file deve coincidere con quello dichiarato dentro il pacchetto.

```bash
sha256sum payload.bin | cut -d' ' -f1 | xxd -r -p | base64
grep FILE_HASH payload_properties.txt
```

> **🛑 STOP** se la stringa della prima riga non è identica a quella che segue `FILE_HASH=` nella seconda.

Controlla che il kernel dentro `boot.img` sia quello del progetto:

```bash
mkdir -p "$NX/firmware/boot_unpack"
unpack_bootimg --boot_img extracted/boot.img --out boot_unpack > boot_unpack/info.txt
sha256sum boot_unpack/kernel
stat -c%s extracted/boot.img
```

> **🛑 STOP** se:
> - l'hash del kernel **non** è `f0aa949c95d1421381850cb1982bd2a505e96fcd7565543ec28d5b1f7d81a0cc`;
> - la dimensione di `boot.img` **non** è `100663296`.

---

## 2. Sbloccare il bootloader

> Questa sezione **cancella tutti i dati** del telefono. Fai il backup prima.

### 2.1 Verificare la versione del telefono (prima di cancellare tutto)

Sul **telefono**:

1. Impostazioni → Informazioni sul telefono → tocca 7 volte **Numero build** finché appare "Sei uno sviluppatore".
2. Impostazioni → Sistema → Opzioni sviluppatore → attiva **Debug USB**.
3. Collega il cavo. Sul telefono accetta la richiesta "Consentire il debug USB?" spuntando "Consenti sempre".

Sul **PC**:

```bash
adb devices
adb shell getprop ro.product.name
adb shell getprop ro.build.display.id
adb shell getprop ro.build.version.incremental
```

> **🛑 STOP** se:
> - `adb devices` non mostra il telefono come `device` (se mostra `unauthorized`, accetta il messaggio sul telefono);
> - il nome prodotto non contiene `NX679J`;
> - la versione mostrata non corrisponde a **V311 / 3.11**. Se il telefono ha un firmware **più recente**, **non** tentare di tornare a V311: il downgrade dei bootloader Qualcomm può attivare la protezione anti-rollback e bloccare il telefono. Questo manuale vale solo per V311.

### 2.2 Attivare "Sblocco OEM"

Sul **telefono**: Opzioni sviluppatore → attiva **Sblocco OEM** (OEM unlocking) → conferma.

### 2.3 Sbloccare

```bash
adb reboot bootloader
# attendi la schermata fastboot sul telefono (circa 10 secondi)
fastboot devices
```

> **🛑 STOP** se `fastboot devices` non mostra nulla. Prova un'altra porta USB del PC, oppure ripeti con `sudo $(which fastboot) devices`.

```bash
fastboot flashing unlock
```

Sul **telefono** compare una richiesta di conferma: usa i **tasti volume** per selezionare "UNLOCK THE BOOTLOADER" e il **tasto di accensione** per confermare.

> Se il comando risponde con un errore, prova la variante specifica Nubia presente nella tabella comandi dell'ABL del telefono:
>
> ```bash
> fastboot oem nubia_unlock NUBIA_NX679J
> fastboot flashing unlock
> ```

Verifica e riavvia:

```bash
fastboot getvar unlocked
fastboot reboot
```

> **🛑 STOP** se `getvar unlocked` non risponde `unlocked: yes`.

Il telefono si riavvia con i dati cancellati. Rifai la configurazione iniziale di Android, poi **riattiva Opzioni sviluppatore e Debug USB** come al passo 2.1.

---

## 3. Root dello slot A ed estrazione dei blob

### 3.1 Installare Magisk e creare l'immagine patchata

Il progetto ha usato **Magisk 30.7**.

```bash
cd "$NX/firmware"
wget https://github.com/topjohnwu/Magisk/releases/download/v30.7/Magisk-v30.7.apk
adb install Magisk-v30.7.apk
adb push extracted/boot.img /sdcard/Download/boot.img
```

> Se il link di Magisk non funziona, apri `https://github.com/topjohnwu/Magisk/releases` e scarica l'APK della versione 30.7.

Sul **telefono**:

1. Apri l'app **Magisk**.
2. Accanto a "Magisk" tocca **Installa**.
3. Scegli **"Seleziona e patcha un file"** e seleziona `Download/boot.img`.
4. Attendi "All done!".

Sul **PC**:

```bash
F=$(adb shell ls /sdcard/Download/ | grep magisk_patched | tr -d '\r' | tail -1)
echo "$F"
adb pull "/sdcard/Download/$F" "$NX/magisk_patched-30700_Zx2eF.img"
stat -c%s "$NX/magisk_patched-30700_Zx2eF.img"
```

> Il nome `magisk_patched-30700_Zx2eF.img` è quello che gli script del progetto si aspettano. Il file è il **tuo**, non quello degli autori, quindi il suo hash sarà diverso: lo gestiamo al passo 4.4.

> **🛑 STOP** se `echo "$F"` stampa una riga vuota, oppure se la dimensione non è `100663296`. Non tentare di correggere la dimensione a mano.

### 3.2 Installare l'immagine patchata sullo slot A

```bash
adb reboot bootloader
fastboot getvar current-slot
```

> **🛑 STOP** se `current-slot` non è `a`. In quel caso esegui `fastboot set_active a`, poi `fastboot reboot`, attendi l'avvio di Android e ricomincia dal passo 3.2.

```bash
fastboot flash boot_a "$NX/magisk_patched-30700_Zx2eF.img"
fastboot reboot
```

Dopo il riavvio, apri l'app Magisk sul telefono: deve mostrare la versione installata. Poi:

```bash
adb shell su -c id
```

Sul telefono compare la richiesta di permesso superutente per "Shell": concedila. Rilancia il comando.

> **🛑 STOP** se la risposta non contiene `uid=0(root)`.

### 3.3 Backup delle partizioni (fondamentale)

Questi file sono il tuo kit di recupero. Conservali anche fuori dal PC.

```bash
cd "$NX/backup"
BN=/dev/block/bootdevice/by-name
for p in boot_a boot_b dtbo_a dtbo_b vendor_boot_a vendor_boot_b vbmeta_a vbmeta_b misc; do
  echo "== $p"
  adb shell su -c "dd if=$BN/$p of=/sdcard/$p.img bs=1M" < /dev/null
  adb pull "/sdcard/$p.img" "$NX/backup/$p.img"
  adb shell su -c "rm /sdcard/$p.img" < /dev/null
done
ls -l
sha256sum *.img | tee SHA256SUMS
```

> **🛑 STOP** se manca uno dei nove file `.img`, o se `boot_a.img` / `boot_b.img` non pesano `100663296` byte.

### 3.4 Verificare che i due slot siano allineati al firmware V311

Il kernel che metteremo in `boot_b` userà `vendor_boot_b` e `dtbo_b` dello **slot B**. Devono essere quelli di V311.

```bash
cd "$NX"
for p in vendor_boot dtbo; do
  echo "== $p"
  sha256sum "firmware/extracted/$p.img" "backup/${p}_a.img" "backup/${p}_b.img"
done
```

> **🛑 STOP / scelta:**
> - Se i **tre** hash di ciascun gruppo sono uguali: tutto a posto, prosegui.
> - Se `_a` è uguale all'OTA ma `_b` è diverso: lo slot B contiene un firmware diverso. Puoi allinearlo scrivendo **solo** sullo slot B (lo slot A non viene toccato):
>
>   ```bash
>   adb reboot bootloader
>   fastboot flash vendor_boot_b "$NX/firmware/extracted/vendor_boot.img"
>   fastboot flash dtbo_b "$NX/firmware/extracted/dtbo.img"
>   fastboot reboot
>   ```
>
>   Poi ripeti il passo 3.3 e il passo 3.4: ora gli hash devono coincidere.
> - Se è diverso **`_a`**: il telefono non è su V311. **Fermati.**

### 3.5 Estrarre il ramdisk vendor (moduli del kernel)

Il ramdisk vendor contiene i moduli `.ko` caricati dal kernel. Gli script ne hanno bisogno in tre posizioni.

```bash
cd "$NX"
rm -rf "$NX/vb_a" && mkdir -p "$NX/vb_a"
unpack_bootimg --boot_img backup/vendor_boot_a.img --out vb_a > vb_a/info.txt
ls -l vb_a/vendor_ramdisk00

# 1) copia per build-candidate-v6.py
mkdir -p "$NX/experiments/20260916-122926-native-baseline/unpacked-current/vendor_boot_a"
cp vb_a/vendor_ramdisk00 \
   "$NX/experiments/20260916-122926-native-baseline/unpacked-current/vendor_boot_a/"

# 2) ramdisk estratto per v6-module-plan.py
rm -rf "$NX/port-work/stock_vendor_ramdisk" && mkdir -p "$NX/port-work/stock_vendor_ramdisk"
( cd "$NX/port-work/stock_vendor_ramdisk" && lz4 -dc "$NX/vb_a/vendor_ramdisk00" | cpio -idm --quiet )

# 3) copia temporanea per build-v10.py
#    (/tmp si svuota al riavvio del PC: in quel caso ripeti solo queste due righe)
rm -rf /tmp/vr && mkdir -p /tmp/vr
( cd /tmp/vr && lz4 -dc "$NX/vb_a/vendor_ramdisk00" | cpio -idm --quiet )

ls /tmp/vr/lib/modules | wc -l
test -f /tmp/vr/lib/modules/modules.load && echo "modules.load OK"
```

> **🛑 STOP** se non compare `modules.load OK` o se il conteggio dei file è `0`.

> Per il traguardo v10 non servono altri blob: firmware del touch, moduli completi del modem e firmware Wi-Fi riguardano le versioni successive.

---

## 4. Ricostruire l'immagine boot_b (traguardo v10)

La catena di build ha cinque stadi, ognuno costruito sul precedente:

| Stadio | Script | Produce |
|---|---|---|
| v6 | `build-candidate-v6.py` | init statico minimo e gadget USB |
| v7 | `build-probe-v7.py` | ramdisk v7 misurato |
| v8 | `build-candidate-v8.py` | PID 1 supervisore e worker |
| v9 | `build-candidate-v9.py` | relay di sola lettura sulla porta 9999 → `boot_b-init-v9.img` |
| v10 | `build-v10.py` | rootfs OpenWrt, chroot, SSH → `boot_b-init-v10.img` |

> **Importante.** Ogni script controlla gli hash SHA-256 dei propri input e si **ferma** se non coincidono con quelli degli autori. Poiché la tua immagine Magisk e i binari che compilerai sono tuoi, i loro hash saranno diversi. Ai passi 4.4–4.8 aggiorneremo quelle costanti con i **tuoi** valori, misurati sui **tuoi** file. **Non disattivare mai i controlli:** aggiornali solo con valori calcolati come indicato.

### 4.1 Scaricare il repository

```bash
cd ~
git clone https://github.com/Saddytech/openwrt-nx679j.git "$REPO"
cd "$REPO"
git checkout f9a92fc5a1765ccecd4ca50ea92abe50d8a39914
git log -1 --format='%H'
```

> **🛑 STOP** se l'hash stampato non è `f9a92fc5a1765ccecd4ca50ea92abe50d8a39914`.

### 4.2 Disporre i file come li vogliono gli script

Gli script usano percorsi fissi del tipo `/home/user/nx679j-stock/...`. Ricreiamo la stessa struttura dentro `$NX`.

```bash
E="$NX/experiments"

# v6 e v7 (senza sovrascrivere i file messi al passo 3.5)
cp -rn "$REPO/05-bootchain-re/20260916-native-baseline/." "$E/20260916-122926-native-baseline/"

# v8 e v9
cp -r "$REPO/02-sorgenti/20260917-init-v8" "$E/20260917-init-v8"
cp -r "$REPO/02-sorgenti/20260917-init-v9" "$E/20260917-init-v9"

# v10
cp "$REPO/09-albero-originale/experiments-extra/build-v10.py" "$E/build-v10.py"

# il contenitore di boot letto dallo slot A = la tua immagine Magisk
mkdir -p "$E/20260916-122926-native-baseline/current-readback"
cp "$NX/magisk_patched-30700_Zx2eF.img" "$E/20260916-122926-native-baseline/current-readback/boot_a.img"

# controllo dei sorgenti necessari
ls "$E/20260916-122926-native-baseline/candidate-init-v6.c" \
   "$E/20260916-122926-native-baseline/v6-module-plan.py" \
   "$E/20260916-122926-native-baseline/candidate-minimal-gadget-v5/manifest.json" \
   "$E/20260916-122926-native-baseline/probe-v7-sonda/candidate-init-sonda.c" \
   "$E/20260917-init-v8/nx679j-init-v8.c" "$E/20260917-init-v8/nx679j-worker.c" \
   "$E/20260917-init-v9/nx679j-init-v9.c" "$E/20260917-init-v9/nx679j-relay.c"
```

> **🛑 STOP** se `ls` segnala che uno dei file non esiste.

### 4.3 Correggere i percorsi negli script

```bash
cd "$NX"
grep -rl "/home/user/nx679j-stock" --include=*.py experiments/ | while read f; do
  sed -i "s#/home/user/nx679j-stock#$NX#g" "$f"
done

# controllo: questo comando non deve stampare nulla
grep -rn "/home/user/nx679j-stock" --include=*.py experiments/
```

> **🛑 STOP** se l'ultimo comando stampa delle righe.

### 4.4 Preparare l'aggiornamento degli hash

Definisci due piccole funzioni. **Vanno ridefinite in ogni nuovo terminale**, insieme agli `export` del passo 1.3.

```bash
pin() {  # pin FILE NOME_COSTANTE HASH64
  sed -i -E "s/^($2 *= *\(?')[0-9a-f]{64}/\1$3/" "$1"
  grep -nE "^$2 *=" "$1"
}
pinlen() {  # pinlen FILE NOME_COSTANTE NUMERO
  sed -i -E "s/^($2 *= *)[0-9]+/\1$3/" "$1"
  grep -nE "^$2 *=" "$1"
}
```

Calcola l'hash della **tua** immagine Magisk e inseriscilo nei tre script che la controllano:

```bash
E="$NX/experiments"
MSHA=$(sha256sum "$NX/magisk_patched-30700_Zx2eF.img" | cut -d' ' -f1)
echo "$MSHA"
pin "$E/20260916-122926-native-baseline/probe-v7-sonda/build-probe-v7.py" MAGISK_SHA "$MSHA"
pin "$E/20260917-init-v8/build-candidate-v8.py"                         MAGISK_SHA "$MSHA"
pin "$E/20260917-init-v9/build-candidate-v9.py"                         MAGISK_SHA "$MSHA"
```

> **🛑 STOP** se una delle tre righe stampate non mostra il tuo hash di 64 caratteri.

### 4.5 Stadio v6

```bash
cd "$NX/experiments/20260916-122926-native-baseline"
python3 build-candidate-v6.py 2>&1 | tee build-v6-mio.log
ls -l candidate-minimal-gadget-v6/boot_b-staticinit-v6.img candidate-minimal-gadget-v6/work/ramdisk.cpio.gz
```

> **🛑 STOP** se lo script termina con un errore o se manca uno dei due file. In particolare, se si lamenta del **kernel** (hash `KERNEL_SHA` diverso), significa che la tua immagine Magisk contiene un kernel diverso da quello di V311: non correggere la costante, fermati.

### 4.6 Stadio v7

Lo script v7 ha bisogno del ramdisk Android della tua immagine Magisk, decompresso in `/tmp/magisk.cpio`:

```bash
cd "$NX"
rm -rf magisk_unpack && mkdir magisk_unpack
unpack_bootimg --boot_img magisk_patched-30700_Zx2eF.img --out magisk_unpack > magisk_unpack/info.txt
lz4 -dc magisk_unpack/ramdisk > /tmp/magisk.cpio
head -c 6 /tmp/magisk.cpio; echo
```

> **🛑 STOP** se l'ultima riga non stampa `070701` (l'intestazione di un archivio cpio).

Aggiorna gli hash che v7 controlla, usando i file appena prodotti:

```bash
B="$NX/experiments/20260916-122926-native-baseline"
V7="$B/probe-v7-sonda/build-probe-v7.py"
pin    "$V7" ANDROID_CPIO_SHA  "$(sha256sum /tmp/magisk.cpio | cut -d' ' -f1)"
pin    "$V7" V6_RAMDISK_GZ_SHA "$(sha256sum "$B/candidate-minimal-gadget-v6/work/ramdisk.cpio.gz" | cut -d' ' -f1)"
pinlen "$V7" V6_RAMDISK_GZ_LEN "$(stat -c%s "$B/candidate-minimal-gadget-v6/work/ramdisk.cpio.gz")"
```

Esegui v7:

```bash
cd "$B/probe-v7-sonda"
rm -rf inputs
python3 build-probe-v7.py 2>&1 | tee build-v7-mio.log
ls -l work/v7-ramdisk.cpio
```

> Il messaggio `lz4 self-test skipped (/tmp/p6.cpio absent)` è normale: quel test confronta l'output con un'immagine degli autori che non hai.

> **🛑 STOP** se lo script termina con un errore o se `work/v7-ramdisk.cpio` non esiste.

### 4.7 Stadio v8

```bash
cd "$NX/experiments/20260917-init-v8"
python3 build-candidate-v8.py 2>&1 | tee build-v8-mio.log
ls -l boot_b-init-v8.img work/v8-ramdisk.cpio work/worker-v8
ls work/
```

> **🛑 STOP** se lo script termina con un errore o se manca uno dei tre file. Lo script verifica tra l'altro che il PID 1 usi solo le chiamate di sistema consentite: se questo controllo fallisce con il tuo compilatore, non aggirarlo.

### 4.8 Stadio v9

Aggiorna gli hash dei prodotti v8 che v9 controlla:

```bash
V8D="$NX/experiments/20260917-init-v8"
V9="$NX/experiments/20260917-init-v9/build-candidate-v9.py"
pin "$V9" V8_CPIO_SHA   "$(sha256sum "$V8D/work/v8-ramdisk.cpio" | cut -d' ' -f1)"
pin "$V9" V8_WORKER_SHA "$(sha256sum "$V8D/work/worker-v8"       | cut -d' ' -f1)"
```

> Lo script contiene anche la costante `V8_INIT_SHA`, ma non è tra quelle che fermano la build: puoi lasciarla com'è.

Esegui v9:

```bash
cd "$NX/experiments/20260917-init-v9"
python3 build-candidate-v9.py 2>&1 | tee build-v9-mio.log
ls -l boot_b-init-v9.img
```

> **🛑 STOP** se lo script termina con un errore o se `boot_b-init-v9.img` non esiste.

### 4.9 Scaricare il rootfs ufficiale OpenWrt 25.12.5

```bash
mkdir -p "$NX/experiments/openwrt-rootfs"
cd "$NX/experiments/openwrt-rootfs"
U=https://downloads.openwrt.org/releases/25.12.5/targets/armsr/armv8
curl -s "$U/" | grep -oE 'openwrt-25\.12\.5-armsr-armv8[a-z0-9-]*rootfs\.tar\.gz' | sort -u
```

Il comando elenca i file rootfs disponibili. Scarica quello mostrato (di norma ce n'è uno solo) e verificalo con la lista hash ufficiale:

```bash
RF=$(curl -s "$U/" | grep -oE 'openwrt-25\.12\.5-armsr-armv8[a-z0-9-]*rootfs\.tar\.gz' | sort -u | head -1)
echo "$RF"
wget "$U/$RF"
wget -O sha256sums "$U/sha256sums"
sha256sum -c --ignore-missing sha256sums
```

> **🛑 STOP** se `echo "$RF"` stampa una riga vuota, o se non compare `<nomefile>: OK`.

Rinomina ed estrai dove lo script se lo aspetta:

```bash
cp "$RF" owrt-aarch64-rootfs.tar.gz
rm -rf root && mkdir root
tar -xzf owrt-aarch64-rootfs.tar.gz -C root --no-same-owner
ls root/bin/busybox root/sbin/init root/lib/ld-musl-aarch64.so.1
sha256sum owrt-aarch64-rootfs.tar.gz
```

> Gli autori hanno registrato per il loro archivio l'hash `2aaddba935ea4e6ad300bfc9946c4d944e2693af6246251e528ed8e27133e962`. Se il tuo coincide, hai lo stesso file degli autori. Se non coincide, non è un errore: conta la verifica ufficiale fatta sopra.

> **🛑 STOP** se `ls` segnala che uno dei tre file non esiste.

### 4.10 Stadio v10 (immagine finale)

Verifica che il ramdisk vendor sia ancora in `/tmp/vr`. Se nel frattempo hai riavviato il PC, rifai il punto 3) del passo 3.5.

```bash
test -f /tmp/vr/lib/modules/modules.load && echo OK
```

Esegui v10:

```bash
cd "$NX/experiments"
python3 build-v10.py 2>&1 | tee build-v10-mio.log
```

Lo script, se non esiste già, crea la chiave SSH `~/.ssh/nx679j_key` e la inserisce nell'immagine. Alla fine stampa la presenza dei file chiave. Controlla:

```bash
grep -A9 "presenza file chiave" build-v10-mio.log
ls -l "$NX/experiments/20260917-init-v9/boot_b-init-v10.img"
stat -c%s "$NX/experiments/20260917-init-v9/boot_b-init-v10.img"
ls -l ~/.ssh/nx679j_key ~/.ssh/nx679j_key.pub
```

> **🛑 STOP** se:
> - una delle otto righe dopo "presenza file chiave" dice `ASSENTE`;
> - la dimensione dell'immagine non è `100663296`;
> - la chiave SSH non esiste.

Copia l'immagine in un posto comodo e salva il suo hash:

```bash
cp "$NX/experiments/20260917-init-v9/boot_b-init-v10.img" "$NX/boot_b-v10.img"
sha256sum "$NX/boot_b-v10.img" | tee "$NX/boot_b-v10.sha256"
```

---

## 5. Scrivere l'immagine nello slot B

> Da qui in avanti **non si scrive mai sullo slot A**. Leggi ogni comando prima di premere Invio e verifica che contenga `_b`.

### 5.1 Scrivere boot_b

Il telefono è acceso in Android (slot A). Entra in fastboot e scrivi **solo** `boot_b`. Lo slot attivo resta A.

```bash
adb reboot bootloader
fastboot getvar current-slot
```

> **🛑 STOP** se non risponde `current-slot: a`.

```bash
fastboot flash boot_b "$NX/boot_b-v10.img"
fastboot reboot
```

### 5.2 Verificare la scrittura rileggendo la partizione

Il telefono riparte in Android (slot A). Prima di avviare lo slot B, rileggi `boot_b` due volte e confronta gli hash con quello dell'immagine. Questa verifica è il passaggio più importante del manuale.

```bash
adb wait-for-device
sleep 20
BN=/dev/block/bootdevice/by-name
adb shell su -c "dd if=$BN/boot_b of=/sdcard/rb1.img bs=1M" < /dev/null
adb pull /sdcard/rb1.img /tmp/rb1.img
sleep 3
adb shell su -c "dd if=$BN/boot_b of=/sdcard/rb2.img bs=1M" < /dev/null
adb pull /sdcard/rb2.img /tmp/rb2.img
adb shell su -c "rm /sdcard/rb1.img /sdcard/rb2.img" < /dev/null

sha256sum /tmp/rb1.img /tmp/rb2.img
cat "$NX/boot_b-v10.sha256"
```

> **🛑 STOP** se i **tre** hash (le due letture e l'immagine) non sono identici. **Non avviare lo slot B.** Ripeti il passo 5.1 e poi il 5.2.

### 5.3 dtbo_b: non serve per questo traguardo

La correzione del pannello nell'overlay `dtbo_b` descritta dal progetto serve solo quando si usa il **display**. L'immagine v10 lavora solo via USB e SSH, quindi **lascia `dtbo_b` invariato**: meno scritture significa meno rischio. La procedura per il display è nell'Appendice A e si esegue con la v90 (passo 8.1).

---

## 6. Avviare OpenWrt dallo slot B

### 6.1 Annotare le interfacce di rete del PC (prima dell'avvio)

```bash
ip -br link | tee /tmp/link-prima.txt
```

### 6.2 Attivare lo slot B e avviare

```bash
adb reboot bootloader
fastboot set_active b
fastboot getvar current-slot
```

> **🛑 STOP** se non risponde `current-slot: b`.

```bash
fastboot reboot
```

Lo schermo del telefono potrebbe restare **nero o fermo sul logo**: è normale, perché questa versione non usa il display. Attendi **almeno 60 secondi**.

### 6.3 Configurare la rete USB sul PC

```bash
lsusb
ip -br link | tee /tmp/link-dopo.txt
diff /tmp/link-prima.txt /tmp/link-dopo.txt
```

Il `diff` mostra la nuova interfaccia creata dal telefono (di solito un nome che inizia con `enx` o `usb`). Impostala così, sostituendo `NOME` con quel nome:

```bash
IF=NOME
sudo ip addr add 10.0.0.2/24 dev "$IF"
sudo ip link set "$IF" up
ping -c 3 10.0.0.1
```

> Se usi NetworkManager e l'indirizzo sparisce dopo pochi secondi, esegui prima `nmcli device set "$IF" managed no` e ripeti i due comandi `sudo ip ...`.

> Se dopo 2 minuti non compare nessuna interfaccia nuova, vai al passo 6.6.

### 6.4 Leggere il diario di avvio (relay di sola lettura)

Il PID 1 espone il diario di avvio sulla porta 9999, in sola lettura:

```bash
nc -w 5 10.0.0.1 9999
```

Cerca righe come `switch.sh: start`, `mount ... OK`, `chiave host pronta`. Sono il racconto dell'avvio, utili se qualcosa non va.

### 6.5 Entrare in SSH

```bash
ssh -i ~/.ssh/nx679j_key -o IdentitiesOnly=yes \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    root@10.0.0.1
```

> La chiave host di dropbear viene rigenerata a ogni avvio: per questo si usano le due opzioni che ignorano la verifica della chiave host.

Una volta dentro, sul **telefono via SSH**:

```sh
cat /etc/openwrt_release
uname -r
ip addr show usb0
```

Risultato atteso:
- `DISTRIB_RELEASE='25.12.5'`;
- kernel `5.10.66-android12-9-...`;
- `usb0` con indirizzo `10.0.0.1`.

**Hai raggiunto il traguardo v10.**

Per uscire dalla sessione SSH: `exit`.

### 6.6 Se qualcosa va storto, e per tornare ad Android

| Situazione | Cosa fare |
|---|---|
| Nessuna interfaccia USB dopo 2 minuti | Tieni premuto **Accensione** per 15–20 secondi finché il telefono si spegne. Poi tieni premuti **Volume giù + Accensione** fino alla schermata fastboot. Dal PC: `fastboot set_active a` e poi `fastboot reboot`. |
| Interfaccia presente ma `ping` non risponde | Ricontrolla il nome dell'interfaccia e l'indirizzo `10.0.0.2/24` (passo 6.3). Prova `nc -w 5 10.0.0.1 9999`. |
| Il relay risponde ma SSH no | Attendi 30 secondi: dropbear parte qualche secondo dopo il resto. Rileggi il diario (6.4) cercando `dropbear`. |
| Il telefono riparte da solo in Android | Il bootloader può tornare automaticamente allo slot A dopo avvii non confermati. Ripeti il passo 6.2. |

Per **tornare ad Android** a fine prova, la procedura è sempre la stessa:

1. Spegni il telefono tenendo premuto **Accensione** per 15–20 secondi. (Dalla sessione SSH, `echo b > /proc/sysrq-trigger` riavvia, ma di nuovo sullo slot B.)
2. Tieni premuti **Volume giù + Accensione** fino alla schermata fastboot.
3. Dal PC:

```bash
fastboot set_active a
fastboot reboot
```

Lo slot A non è mai stato modificato dopo la sezione 3: Android con root riparte come prima.

---

## 7. Costruire la versione completa v90

> Le sezioni 7, 8 e 9 completano rispettivamente la 4, la 5 e la 6: portano dal traguardo v10 all'immagine finale `build-v90.py` degli autori (Wi-Fi, LuCI, modem X65 via QRTR, interfaccia sul display).

**Prima di iniziare la sezione 7:**

- Devi aver completato la sezione 4 (servono i file prodotti da `build-v10.py`).
- È **fortemente consigliato** aver avviato con successo la v10 (sezioni 5 e 6): conferma che kernel, catena di init e rete USB funzionano sul tuo telefono.
- Il telefono deve essere tornato in **Android sullo slot A**, con root (passo 6.6): alcuni blob si prendono da lì.

### 7.0 Cosa cambia rispetto alla v10, e cosa è ricostruito

`build-v90.py` è presente nel repository e, nonostante il nome, è la ricetta aggiornata fino alla v180 (lo conferma l'hash del binario `nx679j-ui` incluso nel repository). Il builder però legge file che **non sono pubblicati**. Questa tabella dice da dove li prendiamo:

| File richiesto da `build-v90.py` | Nel repository? | Come lo otteniamo |
|---|---|---|
| `owrt-live19.tar.gz` (rootfs OpenWrt "vivo") | **No** | **Ricostruito** (passo 7.5): rootfs ufficiale + pacchetti dai feed OpenWrt + file del progetto |
| `boot_b-live.img` (contenitore) | No | La tua `boot_b-v10.img` |
| `switch-v58-live.sh` | No, ma c'è l'equivalente | `build-v64/overlay/nx679j/switch.sh`: è l'uscita dell'ultima esecuzione di `build-v90.py` |
| `v61-wifi-services.sh` | No, ma c'è l'equivalente | `payload-build-v64/etc/nx679j-wifi-services.sh` |
| `persist-tars/{etc,luci,tools}.tar` | Sì (`04-persistenza/`) | Copiati e **personalizzati** (chiave SSH, Wi-Fi, APN, DNS) |
| `kernmods/*.ko`, moduli Wi-Fi, `goodix-fw/*.bin` | No (blob proprietari) | Dal **tuo** telefono |
| `qmicli-full`, `libqmi-glib.so.5.11.0` | No | Dal pacchetto OpenWrt standard (passo 7.6); versione "full" facoltativa (7.10) |
| Tutti gli altri script, binari UI, toolkit modem | Sì | Usati così come sono |

> Ho eseguito `build-v90.py` con esattamente questa disposizione dei file (con binari segnaposto al posto dei blob proprietari) e lo script è arrivato fino a `VERIFICA PASS`. Non ho potuto verificare il comportamento sul telefono reale.

> **Differenze rispetto all'immagine degli autori, da conoscere prima:**
> - ModemManager e libqmi sono quelli **standard** dei feed OpenWrt, non le versioni patchate degli autori. Il percorso dati del modem nel progetto finale non dipende da ModemManager (lo gestisce la catena QRTR in `tools.tar`), ma la pagina "Cellular" di LuCI basata su ModemManager potrebbe non vedere il modem.
> - Senza `qmicli` "full" (passo 7.10, facoltativo) l'interfaccia non mostra i dati di carrier aggregation.
> - L'immagine **non sarà identica byte per byte** a quella degli autori.

### 7.1 Pacchetti aggiuntivi sul PC

```bash
sudo apt install -y qemu-user-static binfmt-support openssl
ls /proc/sys/fs/binfmt_misc/ | grep aarch64
```

> **🛑 STOP** se l'ultimo comando non stampa `qemu-aarch64`. Riavvia il PC e ricontrolla.

### 7.2 Variabili e cartella di lavoro della v90

Ripeti sempre questi comandi quando apri un nuovo terminale:

```bash
export NX="$HOME/nx679j-stock"
export REPO="$HOME/openwrt-nx679j"
export E="$NX/experiments"
export SES="$E/20260920-wifi-luci"
export KV="5.10.66-android12-9-00005-gf6e6376090be-ab8060604"
export L="$NX/live-root"
```

Copia la cartella dei sorgenti v90 e correggi i percorsi:

```bash
rm -rf "$SES"
cp -r "$REPO/02-sorgenti/wifi-luci" "$SES"
sed -i "s#/home/user/nx679j-stock#$NX#g" "$SES/build-v90.py"
grep -n "/home/user" "$SES/build-v90.py"
```

> **🛑 STOP** se l'ultimo comando stampa delle righe.

Prepara i tre file sostitutivi. **Il primo va copiato ora**, perché `build-v90.py` cancella la cartella `build-v64` all'avvio:

```bash
cp "$SES/build-v64/overlay/nx679j/switch.sh"           "$SES/switch-v58-live.sh"
cp "$SES/payload-build-v64/etc/nx679j-wifi-services.sh" "$SES/v61-wifi-services.sh"
cp "$NX/boot_b-v10.img"                                 "$SES/boot_b-live.img"

ls -l "$E/20260917-init-v9/v10-work/v9.cpio" "$E/20260917-init-v9/v10-work/new-uid0.cpio"
grep -c "export PATH=/usr/sbin:/usr/bin:/sbin:/bin" "$SES/switch-v58-live.sh"
grep -c "ujail.off" "$SES/v61-wifi-services.sh"
```

> **🛑 STOP** se `ls` segnala un file mancante (rifai il passo 4.10), o se uno dei due `grep -c` stampa `0`.

### 7.3 Prendere i blob proprietari dal telefono

Il telefono deve essere in **Android, slot A**, con root (verifica con `adb shell su -c id` → `uid=0(root)`). Il ramdisk vendor deve essere ancora in `/tmp/vr` (altrimenti rifai il punto 3 del passo 3.5).

Elenca i moduli presenti sul telefono:

```bash
mkdir -p "$NX/blobs/ko" "$NX/blobs/goodix"
adb shell su -c "find /vendor /vendor_dlkm /odm -name '*.ko' 2>/dev/null" < /dev/null \
    | tr -d '\r' > "$NX/blobs/ko-sul-telefono.txt"
wc -l "$NX/blobs/ko-sul-telefono.txt"
```

Copia i 16 moduli necessari: 7 per tasti, alimentazione e Type-C; 9 per il Wi-Fi. Per ognuno si usa prima la copia del ramdisk vendor, altrimenti quella del telefono:

```bash
for m in pm8941-pwrkey pmic_glink pmic-pon-log qcom-pon qti_battery_charger \
         charger-ulog-glink ucsi_glink \
         cnss_prealloc cnss_utils cnss_nl wlan_firmware_service cnss_plat_ipc_qmi_svc \
         qcom_ramdump qrtr-mhi cnss2 qca_cld3_qca6490; do
  if [ -f "/tmp/vr/lib/modules/$m.ko" ]; then
    cp "/tmp/vr/lib/modules/$m.ko" "$NX/blobs/ko/"; echo "$m: dal ramdisk vendor"; continue
  fi
  p=$(grep "/$m\.ko$" "$NX/blobs/ko-sul-telefono.txt" | head -1)
  if [ -z "$p" ]; then echo "MANCANTE: $m"; continue; fi
  adb shell su -c "cp $p /sdcard/$m.ko" < /dev/null
  adb pull "/sdcard/$m.ko" "$NX/blobs/ko/" > /dev/null
  adb shell su -c "rm /sdcard/$m.ko" < /dev/null
  echo "$m: dal telefono ($p)"
done
ls "$NX/blobs/ko" | wc -l
md5sum "$NX/blobs/ko/qca_cld3_qca6490.ko"
```

> **🛑 STOP** se:
> - compare anche una sola riga `MANCANTE`;
> - il conteggio non è `16`;
> - l'md5 di `qca_cld3_qca6490.ko` **non** è `977053f2eec388e65f7a4584b06ca32e`. È il valore che `build-v90.py` controlla alla fine: se è diverso, il tuo telefono non ha il firmware V311 degli autori.

Firmware del touch:

```bash
adb shell su -c "cp /vendor/firmware/goodix_firmware.bin /vendor/firmware/goodix_cfg_group.bin /sdcard/" < /dev/null
adb pull /sdcard/goodix_firmware.bin  "$NX/blobs/goodix/"
adb pull /sdcard/goodix_cfg_group.bin "$NX/blobs/goodix/"
adb shell su -c "rm /sdcard/goodix_firmware.bin /sdcard/goodix_cfg_group.bin" < /dev/null
stat -c'%n %s' "$NX/blobs/goodix/"*.bin
```

> **🛑 STOP** se le dimensioni non sono `182528` (`goodix_firmware.bin`) e `4614` (`goodix_cfg_group.bin`).

Metti i file dove `build-v90.py` li cerca:

```bash
mkdir -p "$SES/kernmods" "$SES/goodix-fw"
for m in pm8941-pwrkey pmic_glink pmic-pon-log qcom-pon qti_battery_charger charger-ulog-glink ucsi_glink; do
  cp "$NX/blobs/ko/$m.ko" "$SES/kernmods/"
done
cp "$NX/blobs/goodix/"*.bin "$SES/goodix-fw/"
ls "$SES/kernmods" | wc -l
```

> **🛑 STOP** se il conteggio non è `7`.

### 7.4 Scaricare iptables (versione Alpine, come gli autori)

Il kernel del telefono non ha `nf_tables`: la condivisione di Internet usa `iptables-legacy`. Gli autori hanno usato i binari della distribuzione Alpine (musl, aarch64), che `build-v90.py` cerca in `/usr/sbin/xtables-legacy-multi` e `/usr/lib/xtables/`.

```bash
mkdir -p "$NX/alpine" && cd "$NX/alpine"
AL=https://dl-cdn.alpinelinux.org/alpine/v3.21/main/aarch64
wget -q -O APKINDEX.tar.gz "$AL/APKINDEX.tar.gz"
tar -xzf APKINDEX.tar.gz APKINDEX
for p in iptables iptables-legacy libxtables libip4tc libip6tc; do
  v=$(awk -v p="$p" 'BEGIN{RS="";FS="\n"}{n="";v="";for(i=1;i<=NF;i++){if($i~/^P:/)n=substr($i,3);if($i~/^V:/)v=substr($i,3)} if(n==p)print v}' APKINDEX)
  echo "$p $v"
  [ -n "$v" ] && wget -q "$AL/$p-$v.apk"
done
ls *.apk
rm -rf root && mkdir root
for f in *.apk; do tar -xzf "$f" -C root 2>/dev/null; done
find root -name xtables-legacy-multi
ls root/usr/lib/xtables/libxt_MASQUERADE.so
```

> **🛑 STOP** se una riga stampa il nome del pacchetto **senza versione**, se `ls *.apk` non mostra 5 file, o se uno degli ultimi due comandi non trova il file.

### 7.5 Ricostruire il rootfs "vivo" (sostituto di owrt-live19.tar.gz)

Gli autori avevano un archivio del rootfs OpenWrt in esecuzione sul telefono, con i pacchetti installati. Lo ricostruiamo sul PC: si parte dal rootfs ufficiale, si installano gli stessi pacchetti dai feed ufficiali (eseguendo il gestore pacchetti `apk` di OpenWrt tramite l'emulatore qemu), poi si aggiungono i file del progetto e i blob.

**a) Estrarre il rootfs ufficiale e provare l'emulazione:**

```bash
sudo rm -rf "$L" && sudo mkdir -p "$L"
sudo tar -xzf "$E/openwrt-rootfs/owrt-aarch64-rootfs.tar.gz" -C "$L"
sudo chroot "$L" /bin/busybox uname -m
```

> **🛑 STOP** se l'ultimo comando non stampa `aarch64`. Se compare `Exec format error`, prova `sudo cp /usr/bin/qemu-aarch64-static "$L/usr/bin/"` e ripeti il comando.

**b) Installare i pacchetti dai feed OpenWrt 25.12.5:**

```bash
ls -l "$L/etc/resolv.conf"
sudo rm -f "$L/etc/resolv.conf"
echo "nameserver 1.1.1.1" | sudo tee "$L/etc/resolv.conf"
sudo mkdir -p "$L/tmp" "$L/proc"
sudo mount -t proc proc "$L/proc"

sudo chroot "$L" /bin/sh -c 'apk update && apk add \
  luci luci-ssl luci-proto-modemmanager luci-proto-ppp \
  modemmanager qmi-utils mbim-utils libqrtr-glib \
  wpad-basic-mbedtls iw iwinfo wireless-regdb dbus-utils procd-ujail'

sudo umount "$L/proc"
sudo rm -f "$L/etc/resolv.conf"
sudo ln -s /tmp/resolv.conf "$L/etc/resolv.conf"
ls -l "$L/usr/sbin/wpad" "$L/usr/sbin/ModemManager" "$L/usr/bin/qmicli" "$L/sbin/ujail" "$L"/usr/lib/libqmi-glib.so*
```

> **🛑 STOP** se `apk` termina con un errore, o se `ls` segnala un file mancante.

**c) Aggiungere i file del progetto e i blob:**

```bash
# ujail rinominato (come nel rootfs degli autori)
sudo mv "$L/sbin/ujail" "$L/sbin/ujail.off"

# moduli Wi-Fi nel percorso letto da switch.sh
sudo mkdir -p "$L/lib/modules/$KV"
for m in cnss_prealloc cnss_utils cnss_nl wlan_firmware_service cnss_plat_ipc_qmi_svc \
         qcom_ramdump qrtr-mhi cnss2 qca_cld3_qca6490; do
  sudo cp "$NX/blobs/ko/$m.ko" "$L/lib/modules/$KV/"
done

# protocollo netifd di ModemManager del progetto, con la correzione "device=any" anche nel teardown
sudo cp "$SES/mm-final/modemmanager.sh" "$L/lib/netifd/proto/modemmanager.sh"
sudo sed -i '/^\tjson_get_vars device lowpower iptype$/a\\t[ -n "${device}" ] || device="any"' \
    "$L/lib/netifd/proto/modemmanager.sh"
sudo chmod 755 "$L/lib/netifd/proto/modemmanager.sh"

# strumenti ModemManager del progetto
sudo install -D -m755 "$SES/mm-final/netlink-watch"        "$L/usr/lib/nx679j/modem/netlink-watch"
sudo install -D -m644 "$SES/mm-final/80-mm-nx679j.rules"   "$L/lib/udev/rules.d/80-mm-nx679j.rules"
sudo install -D -m644 "$SES/mm-final/25-modemmanager-net"  "$L/etc/hotplug.d/net/25-modemmanager-net"
sudo install -m755    "$SES/nx679j-wan-share.sh"           "$L/etc/nx679j-wan-share.sh"

# configurazioni e servizio netlink-watch dal tar di persistenza ORIGINALE
rm -rf "$NX/persist-orig" && mkdir -p "$NX/persist-orig"
tar -xpf "$REPO/04-persistenza/etc.tar" -C "$NX/persist-orig"
for f in wireless network dhcp; do sudo cp "$NX/persist-orig/etc/config/$f" "$L/etc/config/$f"; done
sudo sed -i "/option device 'qcom-soc'/d" "$L/etc/config/network"
sudo cp -a "$NX/persist-orig/etc/init.d/mm-netlink-watch"   "$L/etc/init.d/"
sudo cp -a "$NX/persist-orig/etc/rc.d/S71mm-netlink-watch"  "$L/etc/rc.d/"

# iptables Alpine
X=$(find "$NX/alpine/root" -name xtables-legacy-multi | head -1)
sudo install -D -m755 "$X" "$L/usr/sbin/xtables-legacy-multi"
for n in iptables-legacy iptables-legacy-save iptables-legacy-restore; do
  sudo ln -sf xtables-legacy-multi "$L/usr/sbin/$n"
done
sudo mkdir -p "$L/usr/lib/xtables"
sudo cp -a "$NX/alpine/root/usr/lib/xtables/." "$L/usr/lib/xtables/"
for lib in $(cd "$NX/alpine/root" && find usr/lib lib -maxdepth 1 \
      \( -name 'libxtables.so*' -o -name 'libip4tc.so*' -o -name 'libip6tc.so*' \) 2>/dev/null); do
  sudo cp -an "$NX/alpine/root/$lib" "$L/usr/lib/"
done
[ -e "$L/lib/libc.musl-aarch64.so.1" ] || sudo ln -s ld-musl-aarch64.so.1 "$L/lib/libc.musl-aarch64.so.1"
sudo chroot "$L" /usr/sbin/iptables-legacy -V
```

> **🛑 STOP** se l'ultimo comando non stampa una riga del tipo `iptables v1.8.x (legacy)`.

> Le configurazioni copiate qui (`network`, `dhcp`) contengono ancora l'APN `internet.it` e i DNS degli autori: è voluto, perché `build-v90.py` le controlla prima di applicare i tar di persistenza. I **tuoi** valori vanno nei tar di persistenza al passo 7.7, che sovrascrivono questi.

**d) libqmi con il nome atteso:**

```bash
ls -l "$L"/usr/lib/libqmi-glib.so*
if [ ! -e "$L/usr/lib/libqmi-glib.so.5.11.0" ]; then
  Q=$(readlink -f "$L/usr/lib/libqmi-glib.so.5"); echo "file reale: $Q"
  sudo cp "$Q" "$L/usr/lib/libqmi-glib.so.5.11.0"
fi
ls -l "$L/usr/lib/libqmi-glib.so.5.11.0"
```

> Se il pacchetto installa già `libqmi-glib.so.5.11.0`, il blocco non fa nulla. Altrimenti crea una copia con quel nome: i programmi la trovano comunque tramite il nome `libqmi-glib.so.5`, che `build-v90.py` ricrea.

**e) Controllo completo del rootfs prima di impacchettarlo.** Crea lo script di controllo (riproduce i controlli che `build-v90.py` fa su questo archivio):

```bash
cat > "$NX/check-live.py" <<'EOF'
import sys, pathlib
L = pathlib.Path(sys.argv[1]); KV = '5.10.66-android12-9-00005-gf6e6376090be-ab8060604'
ok = True
def c(cond, msg):
    global ok
    print(('OK      ' if cond else 'MANCA   ') + msg); ok = ok and bool(cond)
def txt(p):
    p = L / p
    return p.read_text() if p.exists() else ''
c((L/'usr/sbin/wpad').exists(), 'usr/sbin/wpad')
c((L/f'lib/modules/{KV}/qca_cld3_qca6490.ko').exists(), 'qca_cld3_qca6490.ko')
c((L/'etc/config/wireless').exists(), 'etc/config/wireless')
c((L/'sbin/ujail.off').exists(), 'sbin/ujail.off')
pt = txt('lib/netifd/proto/modemmanager.sh')
c(pt.count('[ -n "${device}" ] || device="any"') >= 2 and 'add_device' not in pt
  and 'json_get_vars device apn' in pt, 'proto modemmanager.sh corretto')
nw = L/'usr/lib/nx679j/modem/netlink-watch'
c(nw.exists() and nw.stat().st_size > 200000, 'netlink-watch')
s71 = L/'etc/rc.d/S71mm-netlink-watch'
c((L/'etc/init.d/mm-netlink-watch').exists() and (s71.is_symlink() or s71.exists()), 'servizio mm-netlink-watch')
mm = L/'usr/sbin/ModemManager'
c(mm.exists(), 'ModemManager (%d byte)' % (mm.stat().st_size if mm.exists() else 0))
c((L/'usr/lib/libqmi-glib.so.5.11.0').exists(), 'libqmi-glib.so.5.11.0')
c('qmapmux' in txt('lib/udev/rules.d/80-mm-nx679j.rules'), 'regola udev 80-mm-nx679j')
c((L/'usr/sbin/xtables-legacy-multi').exists(), 'xtables-legacy-multi')
c((L/'usr/lib/xtables/libxt_MASQUERADE.so').exists(), 'libxt_MASQUERADE.so')
c((L/'etc/nx679j-wan-share.sh').exists(), 'nx679j-wan-share.sh')
lm = L/'lib/libc.musl-aarch64.so.1'
c(lm.is_symlink() or lm.exists(), 'libc.musl-aarch64.so.1')
net, dh = txt('etc/config/network'), txt('etc/config/dhcp')
sec = net.split("config interface 'modem'")[1].split('config ')[0] if "config interface 'modem'" in net else ''
c("proto 'modemmanager'" in net and "device 'phy0-ap0'" in net and 'usb0' not in net, 'network')
c("option apn 'internet.it'" in sec and 'disable_modem' in sec and 'option device' not in sec, 'network: sezione modem')
c('lan_wifi' in dh and '151.5.216.30' in dh and 'phy0-ap0' in dh, 'dhcp')
print('\nRISULTATO:', 'TUTTO OK' if ok else 'CI SONO VOCI MANCANTI')
sys.exit(0 if ok else 1)
EOF
sudo python3 "$NX/check-live.py" "$L"
```

> **🛑 STOP** se l'ultima riga non è `RISULTATO: TUTTO OK`. La riga `MANCA` indica cosa rifare.

**f) Dimensione di ModemManager.** `build-v90.py` pretende un ModemManager più grande di 3.000.000 byte, perché gli autori usavano una versione patchata e compilata a parte. Controlla il tuo:

```bash
stat -c%s "$L/usr/sbin/ModemManager"
```

Se il numero è **minore di 3000000**, abbassa la soglia nei due controlli dello script (e solo in quelli):

```bash
sed -i 's/st_size > 3000000/st_size > 500000/' "$SES/build-v90.py"
grep -n "st_size > " "$SES/build-v90.py"
```

> Questa è l'unica modifica a un controllo di `build-v90.py` in tutto il manuale. Serve solo perché ModemManager è la versione standard, e ne accetti le conseguenze descritte al passo 7.0.

**g) Impacchettare:**

```bash
sudo tar -czf "$SES/owrt-live19.tar.gz" --numeric-owner \
    --exclude='./proc/*' --exclude='./sys/*' --exclude='./dev/*' --exclude='./tmp/*' \
    -C "$L" .
sudo chown "$USER:" "$SES/owrt-live19.tar.gz"
ls -l "$SES/owrt-live19.tar.gz"
tar -tzf "$SES/owrt-live19.tar.gz" | head -3
```

> **🛑 STOP** se le righe elencate non iniziano con `./` (lo script se lo aspetta).

### 7.6 qmicli e libqmi per lo script

```bash
cp "$L/usr/bin/qmicli" "$SES/qmicli-full"
cp -L "$L/usr/lib/libqmi-glib.so.5.11.0" "$SES/libqmi-glib.so.5.11.0"
chmod 755 "$SES/qmicli-full" "$SES/libqmi-glib.so.5.11.0"
strings "$SES/qmicli-full" | grep -c nas-get
```

> Il numero stampato conta i comandi NAS: con il pacchetto standard (collezione "basic") ti aspetti circa `11`. Gli autori, con la collezione "full", ne avevano di più, compreso quello per la carrier aggregation. Se vuoi la versione completa, vedi il passo 7.10.

### 7.7 Personalizzare i tar di persistenza (chiave SSH, Wi-Fi, APN, DNS, password)

I tar del repository sono **sanificati**: niente chiave SSH, password di root bloccata, password del Wi-Fi sostituita da `[REDACTED]`, APN e DNS dell'operatore degli autori (WindTre). Questi tar finiscono nell'immagine **dopo** tutto il resto, quindi qui vanno i tuoi valori.

> Se salti questo passo, l'immagine si avvia ma **non puoi entrare in SSH**: il file delle chiavi nel tar di persistenza sostituisce quello della v10.

Imposta i tuoi valori (modifica le quattro righe tra virgolette):

```bash
WIFI_PASS="UnaPasswordWiFiLunga"     # almeno 8 caratteri
ROOT_PASS="UnaPasswordRoot"          # per entrare in LuCI
APN="internet.it"                    # APN del tuo operatore
DNS1="1.1.1.1"; DNS2="9.9.9.9"       # DNS da usare
```

Estrai, modifica, ricomponi:

```bash
P="$NX/persist-mio"
rm -rf "$P" && mkdir -p "$P/etc" "$P/luci" "$P/tools"
for t in etc luci tools; do tar -xpf "$REPO/04-persistenza/$t.tar" -C "$P/$t"; done

# chiave SSH (la stessa creata da build-v10.py)
cat ~/.ssh/nx679j_key.pub > "$P/etc/etc/dropbear/authorized_keys"
chmod 600 "$P/etc/etc/dropbear/authorized_keys"

# Wi-Fi
sed -i "s/option key '\[REDACTED\]'/option key '$WIFI_PASS'/" "$P/etc/etc/config/wireless"

# APN e DNS
sed -i "s/option apn 'internet.it'/option apn '$APN'/" "$P/etc/etc/config/network"
sed -i "s/list server '151.5.216.30'/list server '$DNS1'/; s/list server '151.5.216.130'/list server '$DNS2'/" \
    "$P/etc/etc/config/dhcp"

# password di root (hash SHA-512)
H=$(openssl passwd -6 "$ROOT_PASS")
sed -i "s|^root:[^:]*:|root:$H:|" "$P/etc/etc/shadow"

# l'APN è scritto anche negli script della catena modem
grep -rl "wds-session internet.it " "$P/tools" "$SES/nx679j-link-watch.sh" \
  | xargs sed -i "s/wds-session internet.it /wds-session $APN /"

# ricomposizione
for t in etc luci tools; do
  tar --numeric-owner --owner=0 --group=0 -cf "$SES/persist-tars/$t.tar" -C "$P/$t" .
done
ls -l "$SES/persist-tars/"
```

Controllo:

```bash
tar -xOf "$SES/persist-tars/etc.tar" ./etc/dropbear/authorized_keys
tar -xOf "$SES/persist-tars/etc.tar" ./etc/config/wireless | grep -E "ssid|key"
tar -xOf "$SES/persist-tars/etc.tar" ./etc/config/network | grep apn
tar -xOf "$SES/persist-tars/etc.tar" ./etc/shadow | head -1 | cut -c1-12
grep -rh "qmi-qrtr-observed wds-session" "$P/tools" "$SES/nx679j-link-watch.sh"
```

> **🛑 STOP** se:
> - la prima riga non è la tua chiave (`ssh-ed25519 ...`);
> - la password del Wi-Fi è ancora `[REDACTED]`;
> - l'hash di root non inizia con `root:$6$`;
> - le righe `wds-session` non contengono il tuo APN.

> Rete Wi-Fi configurata: SSID `NX679J-TEST`, banda 2,4 GHz, canale 6, indirizzo del telefono `192.168.77.1`. Puoi cambiare SSID e canale nello stesso file `wireless`, prima della ricomposizione.

### 7.8 Eseguire build-v90.py

```bash
cd "$SES"
python3 build-v90.py 2>&1 | tee build-v90-mio.log
tail -3 build-v90-mio.log
ls -l "$SES/boot_b-v90-mmwd.img"
stat -c%s "$SES/boot_b-v90-mmwd.img"
```

> **🛑 STOP** se:
> - l'ultima riga del log non è `VERIFICA PASS -> .../boot_b-v90-mmwd.img`;
> - la dimensione non è `100663296`.
>
> Se lo script si ferma su un controllo, il messaggio dice quale file manca o è sbagliato: torna al passo che lo prepara. **Non modificare i controlli**, tranne quello del passo 7.5 f).

### 7.9 Percorso del firmware del touch nella riga di comando del kernel

Al boot, `switch.sh` monta la partizione del firmware del modem sopra `/lib/firmware`, nascondendo i file del touch. Gli autori hanno risolto aggiungendo `firmware_class.path=/owrt/lib/firmware` alla riga di comando del kernel con il loro script `goodix-cmdline-path.py`. La documentazione degli autori indica che l'applicazione di questa modifica alle ultime immagini è "da verificare": senza di essa il touch potrebbe non funzionare, il resto sì.

```bash
cd "$SES"
python3 goodix-cmdline-path.py boot_b-v90-mmwd.img
unpack_bootimg --boot_img boot_b-v90-mmwd.img --out /tmp/v90chk | grep -i "command line"
stat -c%s boot_b-v90-mmwd.img
```

> **🛑 STOP** se la riga della command line non contiene `firmware_class.path=/owrt/lib/firmware`, o se la dimensione non è più `100663296`.

Copia l'immagine finale e salva l'hash:

```bash
cp "$SES/boot_b-v90-mmwd.img" "$NX/boot_b-v90.img"
sha256sum "$NX/boot_b-v90.img" | tee "$NX/boot_b-v90.sha256"
```

### 7.10 (Facoltativo) qmicli con collezione "full"

Serve solo per vedere sul display e in LuCI i dati di carrier aggregation. Richiede l'SDK OpenWrt (circa 1 GB) e un'ora di compilazione. **Non verificato**: i nomi delle opzioni di configurazione vanno confermati con il comando `grep` indicato.

```bash
sudo apt install -y build-essential zstd gawk rsync unzip file
mkdir -p "$NX/sdk" && cd "$NX/sdk"
U=https://downloads.openwrt.org/releases/25.12.5/targets/armsr/armv8
SDK=$(curl -s "$U/" | grep -oE 'openwrt-sdk-25\.12\.5-armsr-armv8_[^"]*\.tar\.(zst|xz)' | sort -u | head -1)
echo "$SDK"
wget "$U/$SDK"
tar -xf "$SDK"
cd openwrt-sdk-25.12.5-armsr-armv8_*/
./scripts/feeds update packages
./scripts/feeds install libqmi
make defconfig
grep -n LIBQMI_COLLECTION .config
```

Se `grep` mostra le opzioni `CONFIG_LIBQMI_COLLECTION_*`, seleziona la "full":

```bash
sed -i 's/^CONFIG_LIBQMI_COLLECTION_BASIC=y/# CONFIG_LIBQMI_COLLECTION_BASIC is not set/' .config
echo 'CONFIG_LIBQMI_COLLECTION_FULL=y' >> .config
make defconfig
grep -n LIBQMI_COLLECTION .config
make package/feeds/packages/libqmi/compile -j"$(nproc)"
Q=$(find build_dir -path '*ipkg-install*' -name qmicli -type f | head -1)
LQ=$(find build_dir -path '*ipkg-install*' -name 'libqmi-glib.so.5.*' -type f | head -1)
echo "$Q"; echo "$LQ"
strings "$Q" | grep -c nas-get
```

> **🛑 STOP** se `grep -n LIBQMI_COLLECTION` non mostra `CONFIG_LIBQMI_COLLECTION_FULL=y`, o se il conteggio finale non è maggiore di quello del passo 7.6.

Se tutto è a posto, sostituisci i due file e **ripeti il passo 7.8 e il 7.9**:

```bash
cp "$Q"  "$SES/qmicli-full"
cp "$LQ" "$SES/libqmi-glib.so.5.11.0"
```

> Gli autori avevano anche applicato patch proprie a libqmi (`qti-patches/`). Non sono necessarie per il percorso dati finale e non è verificato che si applichino alla versione dei feed 25.12.5: questo passo non le usa.

---

## 8. Scrivere la v90 nello slot B

> Come nella sezione 5: **mai scrivere sullo slot A**. Ogni comando di scrittura deve contenere `_b`.

### 8.1 Correzione del pannello in dtbo_b (per il display)

L'interfaccia della v90 usa il display. Il progetto indica che, con l'overlay originale, il pannello resta spento. Esegui ora l'**Appendice A** di questo manuale (trovare la voce, rinominare l'etichetta, reinserire con `dtbo-replace.py`, scrivere e verificare `dtbo_b`).

> Se preferisci provare prima senza toccare `dtbo_b`, puoi saltare questo passo: modem, Wi-Fi, LuCI e SSH funzionano indipendentemente dal display. Potrai eseguire l'Appendice A in un secondo momento.

### 8.2 Scrivere boot_b

Il telefono è in Android, slot A:

```bash
adb reboot bootloader
fastboot getvar current-slot
```

> **🛑 STOP** se non risponde `current-slot: a`.

```bash
fastboot flash boot_b "$NX/boot_b-v90.img"
fastboot reboot
```

### 8.3 Verificare la scrittura

```bash
adb wait-for-device
sleep 20
BN=/dev/block/bootdevice/by-name
adb shell su -c "dd if=$BN/boot_b of=/sdcard/rb1.img bs=1M" < /dev/null
adb pull /sdcard/rb1.img /tmp/rb1.img
sleep 3
adb shell su -c "dd if=$BN/boot_b of=/sdcard/rb2.img bs=1M" < /dev/null
adb pull /sdcard/rb2.img /tmp/rb2.img
adb shell su -c "rm /sdcard/rb1.img /sdcard/rb2.img" < /dev/null

sha256sum /tmp/rb1.img /tmp/rb2.img
cat "$NX/boot_b-v90.sha256"
```

> **🛑 STOP** se i tre hash non sono identici. **Non avviare lo slot B**: ripeti 8.2 e 8.3.

Se in qualsiasi momento vuoi tornare alla v10 già provata, la procedura è la stessa con `boot_b-v10.img`.

---

## 9. Avviare la v90

### 9.1 Avvio

```bash
ip -br link | tee /tmp/link-prima.txt
adb reboot bootloader
fastboot set_active b
fastboot getvar current-slot
```

> **🛑 STOP** se non risponde `current-slot: b`.

```bash
fastboot reboot
```

Tempi indicativi, dalla documentazione degli autori:

| Dopo circa | Cosa succede |
|---|---|
| 20–40 s | interfaccia USB sul PC, relay sulla porta 9999, poi SSH |
| 30–60 s | rete Wi-Fi `NX679J-TEST` |
| 70–150 s | interfaccia sul display (se hai eseguito l'Appendice A) |
| 2–6 min | connessione dati del modem |

Non giudicare da un solo avvio: gli autori hanno misurato variazioni di decine di secondi da un boot all'altro.

### 9.2 Rete USB e SSH

```bash
ip -br link | tee /tmp/link-dopo.txt
diff /tmp/link-prima.txt /tmp/link-dopo.txt
```

Con il nome della nuova interfaccia:

```bash
IF=NOME
sudo ip addr add 10.0.0.2/24 dev "$IF"
sudo ip link set "$IF" up
ping -c 3 10.0.0.1
ssh -i ~/.ssh/nx679j_key -o IdentitiesOnly=yes \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    root@10.0.0.1
```

> Nella v90 il telefono offre anche DHCP sull'interfaccia USB: se il PC riceve un indirizzo da solo, i due comandi `sudo ip ...` non servono.

> Se `ssh` chiede una password, il tar di persistenza non contiene la tua chiave (passo 7.7).

### 9.3 Verifiche (sul telefono, via SSH)

```sh
cat /etc/openwrt_release | grep RELEASE
ps w | grep -E "dnsmasq|hostapd|wpad|uhttpd|qmi-qrtr|nx679j-ui|kiosk3" | grep -v grep
ip addr show phy0-ap0
```

Dopo qualche minuto, il modem:

```sh
ip addr show rmnet_data0
ping -c 3 1.1.1.1
sh /usr/lib/nx679j/modem/nx679j-cell.sh 2>&1 | head -20
```

Risultato atteso:
- `DISTRIB_RELEASE='25.12.5'`;
- processi `dnsmasq`, `wpad`/`hostapd`, `uhttpd` attivi;
- `phy0-ap0` con indirizzo `192.168.77.1`;
- `rmnet_data0` con un indirizzo IP dell'operatore e `ping` che risponde;
- righe `cell.*` con banda e segnale.

> Comandi `qmicli`: scrivono i risultati su **stderr**, quindi usa sempre `2>&1`. Sul telefono non esistono `timeout`, `pkill`, `stat -c`: è busybox.

### 9.4 LuCI e Wi-Fi

- LuCI via USB: apri `http://10.0.0.1/` sul PC. Utente `root`, password `ROOT_PASS` del passo 7.7.
- Wi-Fi: collegati a `NX679J-TEST` con `WIFI_PASS`; LuCI è anche su `http://192.168.77.1/`, e il telefono condivide la connessione dati del modem.

### 9.5 Regole di sicurezza (dalla documentazione degli autori)

1. **Non lanciare a mano programmi che usano il display** (`nx679j-ui`, `kiosk3`, `atom*`, `drmtest`): un commit su un display in stato sconosciuto blocca il pannello, e l'unica uscita è lo spegnimento forzato (Accensione 15–20 s).
2. **Non scrivere nei nodi del driver del touch** (`fwupdate`, `get_rawdata`, `esd_info`) e non fare `unbind` del driver: il kernel va in crash.
3. Per riavviare usa `reboot -f` (il `reboot` normale non attraversa il chroot).
4. Se SSH non risponde e il ping va perso al 100%, non insistere: spegni tenendo premuto Accensione 15–20 secondi.
5. Ogni modifica fatta sul telefono si perde al riavvio: il sistema vive nel ramdisk. Le modifiche permanenti si fanno ricostruendo l'immagine.

### 9.6 Tornare ad Android

Dalla sessione SSH, senza toccare i tasti:

```sh
/usr/lib/nx679j/modem/reboot2 bootloader
```

Poi dal PC:

```bash
fastboot set_active a
fastboot reboot
```

In alternativa: spegnimento forzato (Accensione 15–20 s), poi **Volume giù + Accensione** fino a fastboot, poi gli stessi due comandi.

---

## Appendice A — Correzione del pannello in dtbo_b (per il display della v90)

> **Non serve per il traguardo v10.** Serve per l'interfaccia sul display della v90 (passo 8.1). Usa la versione corretta di `dtbo-replace.py`, che verifica da sola il file che scrive.

Sul `dtbo_b`, il nodo `qcom,dsi-default-panel` di uno degli overlay punta al pannello sbagliato (`dsi_r66451_amoled_cmd`) invece di quello reale (`dsi_nubia_r6130_amoled_cmd_dphy`).

In un overlay compilato il riferimento **non** compare come `<&nome>`: compare come `<0xffffffff>`, e il nome dell'etichetta sta nel nodo `__fixups__`. La modifica corretta consiste quindi nel **rinominare la proprietà** dentro `__fixups__`.

### A.1 Trovare quale overlay contiene il nodo

```bash
mkdir -p "$NX/dtbo_work/work" && cd "$NX/dtbo_work"
cp "$REPO/02-sorgenti/20260917-boot-chain/re/dtbo-parse.py" .
cp "$NX/backup/dtbo_b.img" dtbo_b-orig.img
python3 dtbo-parse.py dtbo_b-orig.img | head -3      # riga 2: dt_entry_count
python3 dtbo-parse.py dtbo_b-orig.img $(seq 0 43) > /dev/null
for f in work/dtbo_entry*.dtb; do
  dtc -I dtb -O dts "$f" 2>/dev/null | grep -q dsi_r66451_amoled_cmd && echo "$f"
done
```

> Se `dt_entry_count` non è 44, sostituisci `43` con (numero − 1).

> **🛑 STOP** se il ciclo non stampa **esattamente un** file. Gli autori indicano la voce 35: se ottieni un numero diverso, segui il tuo risultato. Se ne ottieni più di uno, o nessuno, fermati.

### A.2 Verificare che il pannello di destinazione esista nel DTB base

```bash
grep -c dsi_nubia_r6130_amoled_cmd_dphy "$NX/vb_a/dtb"
```

> **🛑 STOP** se il numero è `0`.

### A.3 Modificare l'overlay

Imposta `N` al numero trovato al passo A.1 (per esempio 35):

```bash
N=35
dtc -I dtb -O dts -o e$N.dts work/dtbo_entry$N.dtb
grep -n "dsi_r66451_amoled_cmd" e$N.dts
```

> **🛑 STOP** se la riga trovata **non** è dentro il blocco `__fixups__`, oppure se tra virgolette contiene **più di un** percorso, oppure se il percorso non termina con `:qcom,dsi-default-panel:0`.

```bash
sed -i 's/^\(\s*\)dsi_r66451_amoled_cmd = /\1dsi_nubia_r6130_amoled_cmd_dphy = /' e$N.dts
dtc -I dts -O dtb -o e$N-mod.dtb e$N.dts
dtc -I dtb -O dts e$N-mod.dtb 2>/dev/null | grep -A6 __fixups__
```

La riga sotto `__fixups__` deve ora iniziare con `dsi_nubia_r6130_amoled_cmd_dphy = ` e avere lo stesso percorso di prima.

### A.4 Reinserire la voce e scrivere dtbo_b

Usa la versione corretta dello script `dtbo-replace.py` (file `dtbo-replace.py.txt`: copia la parte a partire dalla riga `#!/usr/bin/env python3` in un file `dtbo-replace.py` dentro `$NX/dtbo_work`).

> Lo script rifiuta di scrivere sopra il file di ingresso, controlla che la zona di destinazione sia vuota e, dopo la scrittura, rilegge il file e confronta tutte le altre voci con l'originale.

```bash
cd "$NX/dtbo_work"
python3 dtbo-replace.py dtbo_b-orig.img $N e$N-mod.dtb dtbo_b-mod.img
python3 dtbo-parse.py dtbo_b-mod.img $N
sha256sum dtbo_b-mod.img | tee dtbo_b-mod.sha256

adb reboot bootloader
fastboot getvar current-slot          # deve essere a
fastboot flash dtbo_b dtbo_b-mod.img
fastboot reboot
```

> **🛑 STOP** prima di flashare se lo script non stampa `verifica OK`.

Dopo il riavvio in Android, verifica la scrittura come al passo 5.2, usando `dtbo_b` al posto di `boot_b` e l'hash in `dtbo_b-mod.sha256`.

> Dopo questa modifica l'hash di `dtbo_b` non coincide più con quello registrato in `vbmeta`. Va bene solo con il bootloader **sbloccato**: prima di un eventuale ri-blocco, riporta `dtbo_b` all'originale.

Per annullare la modifica: `fastboot flash dtbo_b "$NX/backup/dtbo_b.img"`.

---

## Appendice B — Limiti noti di questo manuale

| Punto | Nota |
|---|---|
| Traguardi | v10 (sezioni 1–6) interamente dagli script del repository. v90 (sezioni 7–9) con il rootfs vivo ricostruito: funzionalmente equivalente, non identica byte per byte. |
| Rootfs vivo ricostruito | Ricetta del manuale (passo 7.5), non degli autori. `build-v90.py` è stato eseguito fino a `VERIFICA PASS` con questa disposizione dei file usando binari segnaposto; non è stato provato sul telefono. |
| ModemManager e libqmi | Versioni standard dei feed OpenWrt, non quelle patchate degli autori. Il percorso dati finale non dipende da ModemManager; la pagina "Cellular" di LuCI potrebbe non vedere il modem. Unico controllo di `build-v90.py` modificato: la soglia di dimensione di ModemManager (passo 7.5 f). |
| iptables | Binari Alpine 3.21 (aarch64, musl) come gli autori; versione scelta da questo manuale. |
| qmicli "full" | Facoltativo e non verificato (passo 7.10). Senza di esso mancano i dati di carrier aggregation. |
| Firmware del touch | `goodix-cmdline-path.py` è applicato come indicato dagli autori, che ne segnalano l'applicazione come "da verificare". |
| Hash degli autori | Le costanti negli script vengono aggiornate con i tuoi valori (passi 4.4–4.8). È atteso: immagine Magisk e compilatore sono diversi da quelli degli autori. |
| Versione del compilatore | Gli autori usavano `aarch64-linux-gnu-gcc` 16.1.0. Con versioni diverse i binari cambiano, ma i controlli strutturali degli script (ELF statico, chiamate di sistema consentite) restano attivi. |
| Hash ufficiale dell'OTA | Nubia non ne pubblica uno. Il controllo al passo 1.5 verifica la coerenza interna del pacchetto. |
| Esecuzione end-to-end | Questo manuale non è stato eseguito su un telefono reale. Comandi e percorsi sono ricavati dagli script del repository; le funzioni `pin`/`pinlen`, la modifica dell'overlay con `dtc`/`fdtoverlay`, `dtbo-replace.py`, la personalizzazione dei tar di persistenza e l'esecuzione di `build-v90.py` sono stati verificati su copie e campioni di prova. |
| EDL | Considerala non disponibile. |
