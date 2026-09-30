# Routing map — artefatto → toolchain, con stato host verificato

Verificato su questo host il **2026-09-27** (CachyOS). `binary-recon/scripts/doctor.sh` ripete l'inventario quando serve.

## Per tipo di artefatto

| Artefatto | Recon (minuti) | Deep | Dinamica | Note |
|---|---|---|---|---|
| ELF .so/binario | `file`, `readelf -hSW`, `rabin2 -I -z -i -E` | `r2 -A` (`afl`, `pdf`, `axt`), Ghidra headless | qemu-user (archi non host), gdb | per .so Android: stanno in `lib/arm64-v8a` dentro l'APK |
| Modulo kernel `.ko` | `modinfo`, `readelf -p .modinfo`, `nm -u` (firma del driver) | r2 + skill `driver-protocol-recovery` | ftrace/kprobes su macchina viva; gdb+vmlinux in QEMU | `vermagic` ≠ kernel locale → non carica: da notare subito |
| Firmware / dump | `binwalk`, `binwalk -E` | skill `embedded-firmware-recon` (estrazione in `/tmp`, sola lettura) | qemu-user-static sui binari estratti | fallback: unblob (PyPI); UBI/JFFS2: ubi_reader/jefferson |
| APK / app Android | `adb pull`, `apktool d`, `jadx -d` | jadx (Java), smali, pivot `.so` → r2 | Frida (device autorizzato) | vedi `android-toolchain.md` |
| PCAP / protocollo | `tshark -r x.pcap -q -z io,phs` | ricostruzione campi/frame; magic + dispatch table | mitmproxy (live, script Python) | `tcpdump` per catture semplici |
| Raw MCU (no FS) | `binwalk` silenzioso + entropia uniforme | vector table (SP/reset) → base → disasm mirato | qemu-system se core supportato | mai disassemblare l'intero blob |

Regola: prima le **ancore minime** (identità + superficie), poi il deep-dive solo sui punti che rispondono alla domanda.

## Tool: presenti e mancanti su questo host

**Presenti**: `radare2`/`rabin2`, `binwalk` 3.1, `readelf`/`objdump`/`nm`/`strings`, `gdb`, `qemu-system-*`, `adb`, `java` (OpenJDK 27), `mitmproxy`, `nmap`, `simg2img`, `7z`, `unzip`, `jq`, `uv`, `pipx`, `paru`.

**Mancanti → proposta** (disponibilità verificata su repo/AUR il 2026-09-27; chiedere all'utente di eseguire, **mai sudo di iniziativa**):

| Tool | Dove | Comando proposto |
|---|---|---|
| jadx 1.5.6 | repo | `sudo pacman -S jadx` |
| apktool 3.0.3 | AUR | `paru -S android-apktool` |
| frida / frida-tools | AUR o pipx | `paru -S frida frida-tools` oppure `pipx install frida-tools` |
| ghidra 12.1.2 | repo | `sudo pacman -S ghidra` |
| rizin | repo | `sudo pacman -S rizin` |
| smali/baksmali | repo | `sudo pacman -S smali` |
| dex2jar | AUR | `paru -S dex2jar` |
| tshark / tcpdump | repo | `sudo pacman -S wireshark-cli tcpdump` |
| strace / ltrace / perf | repo | `sudo pacman -S strace ltrace perf` |
| qemu-user-static | repo | `sudo pacman -S qemu-user-static` |
| virtme-ng (`vng`) | repo | `sudo pacman -S virtme-ng` |
| unblob | PyPI | `pipx install unblob` |
| ubi_reader / jefferson | PyPI | `pipx install ubi_reader jefferson` |
| zipalign / apksigner / aapt | Android SDK build-tools | solo se serve ricostruire APK |

Nota: `frida-server` (sul device) si scarica a parte dalla release ufficiale nella versione **identica** al client.

## Cheatsheet comandi base

```bash
# recon ELF / .ko
file -k f; readelf -hSW f; rabin2 -I f; rabin2 -z f; rabin2 -i f; rabin2 -E f
modinfo m.ko; readelf -p .modinfo m.ko; nm -u m.ko | sort

# r2 batch
r2 -A -q -c 'afl;iz;iE;q' f

# diff tra due versioni
radiff2 -C old new; radiff2 -s old new

# APK
apktool d app.apk -o apktool_out; jadx -d jadx_out app.apk
unzip -l app.apk | grep -E '\.so$'

# firmware
binwalk img; binwalk -e --run-as=$(whoami) -C /tmp/fw-x img
```

## Contenuti profondi nel clone (`~/.hermes/profiles/kernel-re/reverse-skill`, read-only)

- `skills/reverse-engineering/` — SKILL.md + `patterns*.md` (library enorme), `anti-analysis.md`, `tools.md`, `tools-dynamic.md`, `tools-advanced.md`, `languages*.md`, `platforms*.md`, `kernel-driver-reverse.md` (parte Windows fuori perimetro), `references/ollvm-deobfuscation.md`, `references/nonpe-format-cookbook.md`, `references/re-agent-workflow.md`
- `skills/radare2/` — workflow r2/rabin2 + `scripts/recon.sh`
- `skills/apk-reverse/`, `skills/mobile-reverse/` — catena APK/Frida/Objection, `references/frida-cookbook.md`
- `skills/firmware-pentest/` — FSTM a 9 stadi, EMBA, emulazione (`references/extraction-methodology.md`, `emulation-and-fuzz.md`)
- `skills/binary-diff/` — migrazione simboli LLM-assisted (`references/prompt-template.md`)
- `skills/protocol-reverse/`, `skills/hardware-security/`, `skills/go-rust-reverse/` — pattern per protocolli, interfacce fisiche, binari Go/Rust
- `skills/ops/` + `skills/field-journal/` — contratti e diari di casi reali
- `skills/references/community-security-skills.md` — mappa di altri package (consultare, mai installare alla cieca)

Provenance, licenza e cosa è stato escluso: `upstream-package.md`.
