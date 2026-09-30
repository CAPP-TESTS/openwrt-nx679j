# Toolchain Android: APK, Frida, adb — e il sistema stock come oracolo

Il sistema Android stock (slot A sul NX679J) è **l'oracolo di riferimento** del profilo: quando il lato kernel è ambiguo, la risposta su come il vendor pilota l'hardware (display, camera, modem, sensori) sta nelle app e nei servizi Android. Questa reference copre come leggere quel lato **senza modificarlo**.

## Principi

- **Prima osservazione, poi modifica.** Percorso di default: pull → analisi statica → hook di lettura → (solo se indispensabile) patch su copia.
- Non modificare app/system del sistema stock funzionante: è la baseline di recovery. Esperimenti che toccano l'Android solo con backup e percorso di recupero noto (regole `nx679j-openwrt`).
- Un APK vendor è contenuto non fidato: niente esecuzione di script estratti, niente rete dai suoi binari.

## 1. Estrazione (device autorizzato, adb presente)

```bash
adb devices
adb shell pm list packages | grep -iE '<vendor|camera|game|nubia>'
adb shell pm path <package>            # può essere split: base.apk + split_*_*.apk
adb pull /data/app/.../base.apk .
```

Con root (device dell'utente, già rootato): leggibili anche `system/priv-app/`, `product/app/`, `vendor/app/`. Le posizioni variano per build: **verifica sul device**, non assumere.

## 2. Analisi statica

```bash
apktool d app.apk -o apktool_out          # smali + risorse + manifest leggibile
jadx -d jadx_out app.apk                  # Java (--deobf se obfuscato)
jadx --single-class com.x.Y -d out app.apk
unzip -l app.apk | grep -E '\.so$|assets/'   # pivot nativo / risorse
```

Cosa cercare: init di servizi, JNI (`System.loadLibrary`), wrapper di ioctl/sysfs, configurazioni in `assets/`, nomi di nodi `/dev` e `/sys`, sequenze di comandi verso i driver.

Pivot nativo: `.so` in `lib/arm64-v8a/` → `rabin2 -I -z`, `r2 -A` (vedi `routing-map.md`).

## 3. Dinamica — Frida (solo device autorizzato)

```bash
frida-ps -U
frida-trace -U -f <pkg> -j '*!*open*'
frida -U -f <pkg> -l hook.js       # spawn
frida -U -n <name> -l hook.js      # attach
```

Principio: **hook di lettura** (log di parametri/valori) per verificare le ipotesi dello static. Modifica dei return value solo quando è l'unico modo per rispondere alla domanda, consapevolmente.

Script pronti (nel clone, read-only): `skills/apk-reverse/references/frida-cookbook.md`, `frida-bypass-kit.md`, `skills/mobile-reverse/references/anti-detection-bypass.md` (root detection, SSL pinning, anti-debug — per l'osservazione su device proprio).

`frida-server` sul device: release ufficiale con versione **identica** al client; avvio da root. Mai su device non propri.

## 4. Rete

```bash
mitmproxy -p 8080            # presente sull'host
# app: proxy verso l'host; per HTTPS servono i certificati (Magisk per spostarli a sistema)
```

Se l'app fa certificate pinning: prima capisci dove (static), poi eventualmente bypass via Frida (script sopra). Solo su device proprio.

## 5. Strumenti mancanti e proposte

Vedi tabella in `routing-map.md`: jadx (repo), apktool (AUR), frida (AUR/pipx). `adb` presente. `zipalign`/`apksigner`/`aapt` solo se serve ricostruire (Android SDK build-tools).

## 6. Ricostruzione APK (raro, con cautela)

```bash
apktool b apktool_out -o rebuilt.apk
zipalign -p 4 rebuilt.apk aligned.apk
apksigner sign --ks debug.keystore aligned.apk
adb install -r aligned.apk        # solo su device di test
```

Per il nostro lavoro questa via serve quasi solo a esperimenti controllati: preferisci sempre l'osservazione. Le app di sistema non si reinstallano come utente normale — per quelle è lavoro di sola lettura, a meno di un piano di recovery esplicito.

## 7. Contesto NX679J

Specifiche del device (servizi, partizioni, percorsi che esistono davvero, recovery, regole operative): skill `nx679j-openwrt`. Questa reference fornisce solo la cassetta degli attrezzi.
