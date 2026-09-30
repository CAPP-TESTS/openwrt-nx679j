# Provenienza delle immagini bootloader — Nubia NX679J (SM8450 / Waipio)

Data: 2026-09-17 · Oggetto: `xbl_a.img` (3.670.016 B) e `abl_a.img` (1.048.576 B) in
`experiments/20260916-122926-native-baseline/diag-gpt-bootchain-20260917-034017/`

Regola applicata: **nessun nome di file e nessuna cartella è considerata una prova.** Solo hash,
confronti byte-a-byte e verifiche crittografiche. Ogni riga è marcata **[FATTO]** (misurato, con
comando e output) oppure **[INFERENZA]** (deduzione).
## 1. Cosa sono realmente i due dump

**[FATTO]** I dump sono un'immagine con contenuto utile + padding di zeri fino alla dimensione di
partizione. Il prefisso noto non basta: il contenuto utile è identico **su tutta** la sua lunghezza,
non solo in prefisso.

```console
$ B=experiments/20260916-122926-native-baseline/diag-gpt-bootchain-20260917-034017
$ cmp -n 1081344 $B/xbl_a.img verified-v311/partitions/xbl.img && echo OK   -> OK
$ cmp -n 172032  $B/abl_a.img verified-v311/partitions/abl.img && echo OK   -> OK
$ cmp -n 1081344 $B/xbl_a.img /tmp/ota311/xbl.img && echo OK                -> OK
$ cmp -n 172032  $B/abl_a.img /tmp/ota311/abl.img && echo OK                -> OK
$ tail -c +1081345 $B/xbl_a.img | tr -d '\0' | wc -c                        -> 0
$ tail -c +172033   $B/abl_a.img | tr -d '\0' | wc -c                       -> 0
```

**[FATTO]** SHA-256 (il padding di zeri spiega la differenza di hash con i file locali):

| artefatto | dimensione | SHA-256 |
|---|---|---|
| dump `xbl_a.img` | 3.670.016 (0x380000) | `685f0a75fbff851bb6ef55ba158b7f86ea14a44c527c5388605ece94f47d637a` |
| `verified-v311/partitions/xbl.img` | 1.081.344 (0x108000) | `4b537421c9a29c4f447c83ecf79ca51c4d1892ff1ca005db2fb5ee9fcd6b5efe` |
| dump `abl_a.img` | 1.048.576 (0x100000) | `443d1956e836aea3fe30ac13113dae787d88223c1e24f875c6831f69d86acea3` |
| `verified-v311/partitions/abl.img` | 172.032 (0x2a000) | `10bcdcaa4a92d03dfa3172c3731f39a3ee84fc9e5e9ccd0eb34f8ac9022b9d48` |
## 2. Ancoraggio indipendente: pacchetto OTA ufficiale Nubia

**[FATTO]** I due ZIP locali provengono dall'host ufficiale Nubia; URL e dimensione finale sono nel
log `wget` locale:

```console
$ grep -o 'https://[^ ]*' V311-download.log | sort -u
https://rom.download.nubia.com/Europe%26Asia/NX679J/V311/NX679J-update.zip
https://rom.download.nubia.com/Europe/NX679J/V411/NX679J-update.zip
$ ls -l NX679J-V311-update.zip     -> 3916284140   (log: "saved [3916284140/3916284140]")
```

**[FATTO]** `payload.bin` coincide con l'hash dichiarato dal pacchetto:
```console
$ sha256sum payload.bin
5994e80017ad2de818c386ba5abaea51f25d122ab4872ca3d548dd35c34a1e6f
$ unzip -p NX679J-V311-update.zip payload_properties.txt | grep FILE_HASH
FILE_HASH=WZToABetLegYw4a6WrrqUfJdEiq0hyyj1UjdNcNKHm8=   # base64 = stesso SHA-256
$ python3 -c "import hashlib,base64;print(base64.b64encode(hashlib.sha256(open('payload.bin','rb').read(218271)).digest()).decode())"
LD3bJH9Tv4DHLlCmU6+c4/m/P85lvSyvAJ+DWIuPXJU=             # = METADATA_HASH del pacchetto
```

**[FATTO]** La firma del payload OTA è valida e appartiene al certificato di firma Nubia: RSA-2048
PKCS#1 v1.5 / SHA-256 all'offset 218277 di `payload.bin`, verificata su `payload[0:218271]` con la
chiave pubblica di `META-INF/com/android/otacert`.

```console
$ openssl x509 -in /tmp/otacert_v311.pem -noout -subject -serial -fingerprint -sha256
subject=C=CN, ST=GuangDong, L=Shenzhen, O=nubia, OU=nubia, CN=nubia, emailAddress=nubia@nubia.com.cn
serial=F9184DEF2791ACE0
sha256 Fingerprint=xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:DD:73
```
Lo **stesso** otacert (stesso serial, stessa impronta) è presente anche nel pacchetto V411.

**[FATTO]** Metadati OTA (`META-INF/com/android/metadata`): `pre-device=NX679J-UN`,
`post-build=nubia/NX679J-UN/NX679J-UN:12/SKQ1.211113.001/eng.nubia.20220308.193302:user/release-keys`,
`NUBIA_SUBVERSION=nx679j_un` → coerenti col `product=NX679J-UN` del device live.

**[FATTO]** Estrazione diretta dal solo `payload.bin` (nessuna cartella intermedia):
```console
$ ./payload-dumper-go -p xbl,abl -o /tmp/ota311 payload.bin
$ sha256sum /tmp/ota311/xbl.img /tmp/ota311/abl.img
4b537421c9a29c4f447c83ecf79ca51c4d1892ff1ca005db2fb5ee9fcd6b5efe  xbl.img
10bcdcaa4a92d03dfa3172c3731f39a3ee84fc9e5e9ccd0eb34f8ac9022b9d48  abl.img
```
→ identici a `verified-v311/partitions/**.img` (la cartella è fedele all'OTA, a prescindere dal
nome) e identici al contenuto dei dump del telefono.

**[FATTO]** V411 è **diverso** (stessa PKI, chiavi di firma diverse):
`af30c12138ebadfaf478ce79bdfb130e0a778247b0a8a9cf57aa6bd17ab36e17` xbl,
`bc917a2c80af4b071dfd37ffdfcaa24fea2015f9578d0af9689712d815274ca1` abl.
## 3. Prove statiche dentro le immagini (catena di firma Qualcomm MBNv7)

**[FATTO]** Struttura del hash-segment di `abl_a.img` (phdr[2] `NULL/HASH`: offset 0x29000, filesz 0xD58):

| campo MBN (header v7 @0x29000) | valore |
|---|---|
| common / QTI / OEM metadata | 24 / 0 / 224 byte |
| hash table | 144 B = 3 × SHA-384 @0x29120 |
| QTI signature / QTI cert chain | **0 / 0** (nessuna firma Qualcomm) |
| OEM signature / OEM cert chain | 104 B @0x291B0 / 2880 B (3 cert) @0x29218 |

**[FATTO]** La tabella hash corrisponde al codice reale (l'immagine non è stata alterata dopo la firma):
```
sha384(abl[0x0:0x94])       = d93d8fb4…bad6bc0a  -> trovato a 0x29120
sha384(abl[0x1000:0x29000]) = 7e89ab67…b8ad1bce  -> trovato a 0x29150
```

**[FATTO]** La firma OEM è **crittograficamente valida**: ECDSA P-384 / SHA-384, DER 103 B @0x291B0,
verificata sui byte `[0x29000, 0x291B0)` con la chiave pubblica del certificato @0x29218.
Script riproducibile: `verify_bootloader_signature.py`.
```console
$ python3 verify_bootloader_signature.py diag-gpt-bootchain-20260917-034017/abl_a.img 0x29000
   OEM SIGNATURE: VALIDA (ECDSA P-384/SHA-384, DER 104B) con la chiave del certificate @0x29218
        subject : L=San Diego,O=SecTools,ST=California,C=US
        issuer  : L=SanDiego,O=SecTools,CN=Generated Ztemt Attestation CA,ST=California,C=US
        serial  : 1   valid: 2022-03-08 12:55:26+00:00 .. 2042-03-03 12:55:26+00:00
        sha256  : 6ca946b4a9eb586c6a91e0cb30ce2b6dd4a336c15b05f1a164e94f194b3c7a0c
        catena: cert[0] verificato con la chiave di cert[1] -> OK
        catena: cert[1] verificato con la chiave di cert[2] -> OK
        catena: cert[2] (root) auto-firmato -> OK
```

**[FATTO]** Catena OEM completa (root self-signed; "Ztemt" = ZTE Mobile Tech). Nell'ABL non c'è
alcun certificato Qualcomm (`CN=CASS - SBL3`) perché QTI sig/chain = 0. I tre certificati:
1. leaf `L=San Diego, O=SecTools, ST=California, C=US` ← issuer `CN=Generated Ztemt Attestation CA`
2. `CN=Generated Ztemt Attestation CA, O=SecTools, L=SanDiego, ST=California, C=US`
3. root `CN=Generated Ztemt Root CA, OU=General Use Ztemt Key, OU=CDMA Technologies, O=SecTools, L=San Diego, ST=California, C=US`

**[FATTO]** Stringhe di build in `xbl_a.img`: `SEQ_FW_BUILD_TYPE_STRING=RELEASE`,
`SEQ_FW_RELEASE_BUILD_VERSION_STRING=r79`, `TME_FW_VERSION_STRING=ssg.tmefw.1.0.1-00189-release`,
`TME_FW_BUILD_TIME_STRING=December 03 2021 at 10:05:41`, `TME_FW_CHIPSET_STRING=Waipio`.

**[FATTO]** `xbl_a.img` contiene **tre** immagini annidate (ELF32 @0x0, ELF32 13-phdr @0x162F4,
ELF64/AArch64 @0x439F4). I due hash-segment firmati (0x412F4 e 0x1049F4) contengono **due** catene —
Qualcomm (`CN=CASS - SBL3` ← `SRoT MBNv7 Image Signing Root CA 6 SubCA 1` ← `SRoT MBNv7 Image
Signing Root CA 6`) **e** la stessa catena Ztemt dell'ABL. Entrambe le catene sono internamente
valide e le loro hash table corrispondono al codice reale.
## 4. Cross-check dispositivo ↔ set locale ↔ OTA

**[FATTO]** Catena di firma: **identica** (byte-identica) fra dump, `verified-v311` e OTA V311;
`xbl_a.img` porta la stessa catena Ztemt a 0x42374 e 0x105984.
**[FATTO]** Il certificato di firma ABL del V311 (leaf `6ca946b4…`, emesso 2022-03-08 12:55:26Z)
coincide con quello del dump; il V411 usa una leaf **diversa** (`5c2aaa9b…`, 2022-03-08 14:34:34Z).
**[FATTO]** Build id: il dump è il build **V311** (`eng.nubia.20220308.193302`, subversion
`nx679j_un`), non V411 (`NX679J-EEA`).
## 5. Sorgenti pubbliche consultate

- `https://rom.download.nubia.com/Europe%26Asia/NX679J/V311/NX679J-update.zip` (host ufficiale Nubia, URL dal log wget)
- `https://rom.download.nubia.com/Europe/NX679J/V411/NX679J-update.zip`
- needrom, `romprovider.com/firmware-nubia-red-magic-7-nx679j/`, `ztefirmware.com`, `liveonserver.com`
  (cartella NX679J), `device.report/nubia`, halabtech: **nessun hash pubblicato**, solo ZIP OTA da
  3,9–5 GB dietro registrazione.
- Ricerca `"Generated Ztemt Root CA"` / `"Ztemt Attestation CA"`: **nessun riscontro pubblico**
  (la root ZTE non compare in alcuno store pubblico consultabile).

Nessun download indipendente eseguito: i due pacchetti sono quelli già presenti, con URL ufficiali
documentati. **Non esiste un hash pubblico dell'OTA con cui confrontarsi**: è la lacuna principale.
## 6. Cosa NON è stato verificato

1. **Provenienza materiale dei due dump.** Dai soli byte non è dimostrabile che siano un readback
   reale del telefono: ho verificato che *il loro contenuto* è identico a quello dell'OTA ufficiale
   con coda di zeri; non ho potuto escludere che un file locale sia stato copiato e padato.
2. **Firma interna di XBL.** I byte di firma (DER ECDSA P-384, 104 B) e le due catene sono presenti
   e le hash table corrispondono al codice, ma **non ho riprodotto** la convenzione esatta del
   messaggio firmato per l'hash-segment doppiamente firmato QTI+OEM: la validità della firma XBL
   *non* è un mio risultato. Per XBL la prova poggia sull'identità byte-a-byte con l'OTA.
3. **Ancoraggio della root Ztemt.** Catena verificata solo per coerenza interna (leaf ← Attestation
   CA ← root self-signed); nessun riferimento pubblico la lega a ZTE/Nubia.
4. **6 byte non spiegati** fra fine metadati payload (218271) e inizio firma RSA (218277).
5. **Cross-check dimensione partizione**: `gpt-primary.bin`/`gpt-backup.bin` della stessa cartella
   non si parsano come GPT standard a settori da 512 B (header a 0x1000, 32 entry): il controllo
   della dimensione reale delle partizioni `xbl_a`/`abl_a` non è stato completato.
6. Metadati e certificati V411 provengono dallo ZIP locale (stessa provenienza wget del V311).
## 7. Verdetto

**[INFERENZA — confidenza ALTA]** Le immagini bootloader sul NX679J sono le immagini
**ufficiali/stock del vendor Nubia, firmware V311**. In ordine di forza:

1. I byte non-zero di `xbl_a.img`/`abl_a.img` sono **identici** — non solo come prefisso — a
   `xbl.img`/`abl.img` estratti *da me* da `payload.bin`, coperto dall'hash dichiarato del pacchetto
   scaricato dall'host ufficiale Nubia e con intestazione firmata dalla chiave OTA `CN=nubia`.
2. `abl_a.img` è firmato **validamente** (ECDSA P-384/SHA-384) con catena OEM ZTE/Ztemt completa, e
   la sua hash table corrisponde al codice: nessuna alterazione dopo la firma. La leaf è del
   **2022-03-08**, stesso giorno del build V311 (`eng.nubia.20220308`).
3. La corrispondenza è **specifica**: V411 (anch'esso ufficiale, stessa CA) ha xbl/abl con hash e
   certificato di firma diversi. Il device ha V311, non un'immagine Nubia qualunque.

**[INFERENZA — confidenza MEDIA]** Il device ha *eseguito* questi bootloader (`secure:yes`, slot `_a`
`slot-successful=yes`): il PBL verifica XBL contro l'hash della OEM PK fusa nel QFPROM, quindi
un'immagine contraffatta non si avvierebbe. Non ho letto i fuse né eseguito un test di boot.

**[NON DIMOSTRATO]** Readback genuino dei due file (6.1) e verificabilità crittografica della firma
interna di XBL (6.2). Nessuna delle due cambia il verdetto, perché la prova primaria è l'identità
byte-a-byte con il pacchetto ufficiale.

**Confidenza complessiva: ALTA** per "bootloader = stock Nubia V311";
**MEDIA** per "i dump provengono da un readback diretto del telefono".
