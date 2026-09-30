---
name: re-toolchain-routing
description: "Use when RE/pentest work needs stages, gates, evidence."
version: 1.0.0
author: Hermes Agent (adapted from zhaoxuya520/reverse-skill)
license: MIT
metadata:
  hermes:
    tags: [reverse-engineering, routing, toolchain, workflow, evidence, apk, firmware, pcap]
---

# RE toolchain routing

## When to Use

- Devi scegliere con quale tool/percorso analizzare un artefatto (APK, `.so`, `.ko`, firmware, dump, PCAP, bytecode).
- L'analisi è bloccata e serve cambiare angolo o ripianificare (regole anti-loop).
- Arriva materiale di un tipo nuovo (app Android, blob vendor, traffico, firmware).
- Serve il gate di scope o la catena di evidenza per un lavoro RE, o il workflow a fasi con le sue ancore.

Routing per tipo di artefatto + workflow a fasi + catena di evidenza. Nato come adattamento del package open-source **reverse-skill** di zhaoxuya520 (MIT), ora **interamente adottato**: i 46 moduli sono registrati come skill esterne del profilo (libreria in `~/.hermes/profiles/kernel-re/reverse-skill`, caricabili per nome — `apk-reverse`, `pwn-chain`, …). Provenance e mappa completa: `references/upstream-package.md`.

## Regola 0 — gate di scope prima di ACT

Prima di toccare un artefatto o un dispositivo, dichiara: **cosa** (file/target, con hash), **perché** (interoperabilità / diagnosi / documentazione), **con che autorizzazione** (dispositivo proprio / sistema autorizzato), **con quale profilo di rete** (default: offline — nessun pacchetto verso il target durante l'analisi). Per il NX679J valgono le regole di `nx679j-openwrt` (niente moduli sperimentali sul kernel di lavoro, niente sudo d'iniziativa, backup prima di ogni scrittura). Se l'autorizzazione non è chiara → non si esegue nulla di attivo.

Campi e template: `references/evidence-and-scope.md`.

## Passo 1 — Routing per artefatto

| Artefatto | Catena primaria | Pivot / alternativa | Dettagli |
|---|---|---|---|
| APK / app Android | `adb pull` → `jadx` (Java) + `apktool` (smali/res) | Frida (dinamica, device autorizzato); `.so` → pivot nativo | `references/android-toolchain.md` |
| ELF / libreria `.so` | `rabin2`/`r2` recon → r2-Ghidra deep | qemu-user per esecuzione isolata | `references/routing-map.md` |
| Modulo kernel `.ko` | skill `binary-recon` (scheda fatti) → r2 + `driver-protocol-recovery` | gdb su QEMU; ftrace/kprobes su macchina viva | `references/routing-map.md` |
| Firmware / dump flash | skill `embedded-firmware-recon` (binwalk, `/tmp`, ro) | unblob / ubi_reader / jefferson se binwalk fallisce | skill esistente |
| PCAP / protocollo custom | tshark (offline) / mitmproxy (live) | pattern "dispatch table" per protocolli custom | `references/upstream-package.md` |
| Bytecode / VM custom | pattern opcode-switch (library nel clone) | `driver-protocol-recovery` per protocolli binari | clone |
| Raw MCU (no filesystem) | vector table → base address → disasm mirato | qemu-system se core supportato | `references/routing-map.md` |

Regola trasversale: **recon leggero prima di tutto** (`file`, `rabin2 -I/-z/-i`, `strings`); deep-dive solo dopo le ancore minime.

## Passo 2 — Workflow a fasi

1. **Triage** (minuti): hash, tipo/architettura, ancore obbligatorie per tipo (`.ko`/`.so`: import/export + modinfo; APK: manifest; firmware: magic+entropia). Ancora non leggibile (packing/cifratura) → **registra il fallimento come evidenza** e prosegui su un'altra via: mai fingere che sia fatta, mai macinare sul percorso bloccato.
2. **Static**: mira ai punti chiave via stringhe/import/export/xref; un cambio strumento alla volta; timebox ~15 min senza percorso chiave → dinamica.
3. **Dynamic** (solo isolato/authorized): QEMU, gdb, ftrace/kprobes, Frida su device autorizzato. Serve a verificare le ipotesi dello static; le due si alternano.
4. **Synthesis**: finding con evidenze + path (callflow) + zone d'ombra; report via `re-driver-report`.

**Anti-loop (regole utente già pagate sul campo):** 3 azioni senza evidenza nuova → ripianifica dichiarando il cambio; stesso comando+parametri non si ripete senza un dato nuovo; dopo 2–3 fallimenti → ricerca esterna PRIMA di insistere (`block-research-fanout`).

Dettaglio di gate, timebox, blindspot e migrazione simboli tra versioni: `references/workflow-gates.md`.

## Passo 3 — Evidenza

Ogni fatto = comando riproducibile + output (o file con hash). Un finding regge con ≥1 evidenza; **validato** richiede ≥2 evidenze indipendenti (idealmente 1 statica + 1 dinamica). Le ipotesi restano marcate come tali. Template E→F→Path: `references/evidence-and-scope.md`.

## Passo 4 — Chiusura

- Report: `re-driver-report` (tabella valori→fonte).
- Lezioni riutilizzabili → si scrivono nella skill del dominio, non solo nel report. Per il device: `STATO-ATTUALE.md` + `nx679j-openwrt`.

## Strumenti su questo host (verificato 2026-09-27)

Presenti: r2/rabin2, binwalk 3.1, binutils, gdb, qemu-system, adb, java, mitmproxy, nmap, simg2img, pipx, paru. Mancanti (proposte di install in `references/routing-map.md`): jadx, apktool, frida, ghidra, rizin, smali, dex2jar, strace/ltrace/perf, tshark. `binary-recon/scripts/doctor.sh` ripete l'inventario. Le installazioni si propongono, non si eseguono di slancio.

## Skill collegate

`binary-recon` (triage) · `embedded-firmware-recon` (firmware) · `driver-protocol-recovery` (registri/ABI) · `linux-driver-authoring` (driver) · `kernel-debug-validate` (verifica) · `re-driver-report` (report) · `block-research-fanout` (quando bloccati) · `android-boot-chain-recovery` (boot chain) · `nx679j-openwrt` (device).
