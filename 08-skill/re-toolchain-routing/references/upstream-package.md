# Package di origine: zhaoxuya520/reverse-skill

## Provenance (verificato 2026-09-27)

- Repo: https://github.com/zhaoxuya520/reverse-skill — router di skill per RE/sicurezza ("Cybersecurity Skills Router")
- Libreria locale: `~/.hermes/profiles/kernel-re/reverse-skill` — HEAD `cab634bd855fc287f6e420c1f36fd1a6b9245960` (2026-09-22), ~14 MB, incluso il vendored `CTF-Sandbox-Orchestrator/` (136 file)
- Licenza: MIT (README del repo); `CTF-Sandbox-Orchestrator/` è GPLv3 e viaggia dentro il repo; Pentest Swarm solo riferimento esterno (AGPL, non incluso)
- Refresh: `git -C ~/.hermes/profiles/kernel-re/reverse-skill pull --ff-only` — le skill esterne si riscansionano da sole nella sessione successiva

## Fatti contati (non dal README)

- 44 route in `skills/config/routing.json` (id R0–R45; mancano R42/R43), 44 voci di priorità allineate.
- 46 `SKILL.md` scoperti dal walk di Hermes: il router alla radice (`reverse-skill-router`), 43 moduli di primo livello, 2 annidati (`reverse-engineering/dsl-vm-reverse/`, `pentest-tools/src-hunter/`).
- Il router bash (`skills/scripts/master-route.sh`) è puro Python+JSON e **funziona su Linux**. Verificato con hint reali: `apk jadx smali` → R1 `apk-reverse` (high); `firmware binwalk squashfs` → R8 `firmware-pentest` (high); «modulo kernel .ko» → **fallback R0 low** (`.ko` non è keyword: per i moduli kernel questo router non aiuta — usiamo `binary-recon`).
- Piccole incoerenze nel README (numeri tra 43/44/45 regole e 173/175 casi di test); i conteggi sopra sono quelli reali.
- Regole interne del package che condividiamo: mai installare skill/tool alla cieca (supply chain), niente path inventati, evidenza o non è fatto. Applicate anche a questo package: si consulta read-only; i suoi script di bootstrap **non si eseguono** (alcuni script si auto-installano dipendenze).

## Cosa contiene (49 directory sotto `skills/`)

**Adottati / in perimetro** (distillati nelle reference di questa skill):

| Modulo | Cosa ne abbiamo preso |
|---|---|
| `reverse-engineering/` | workflow a fasi, pattern library, OLLVM, anti-analysis → `workflow-gates.md` |
| `radare2/` | workflow r2/rabin2 → `routing-map.md` |
| `apk-reverse/`, `mobile-reverse/` | catena APK/Frida → `android-toolchain.md` |
| `firmware-pentest/` | catena FSTM/extrazione → verso `embedded-firmware-recon` |
| `binary-diff/` | migrazione simboli LLM-assisted → `workflow-gates.md` appendice A |
| `protocol-reverse/`, `hardware-security/`, `go-rust-reverse/` | pattern; consultazione diretta dal clone |
| `case-review/`, `docs-generator/`, `diagram-generator/` | riferimenti per report/diagrammi; la via standard resta `re-driver-report` |
| `ops/`, `field-journal/` | contratti scope/evidenza → `evidence-and-scope.md`; diari di casi reali |
| `ida-reverse/`, `ghidra-reverse/` | workflow disassembler (IDA non presente su questo host; Ghidra installabile) |

**Tutti adottati** (2026-09-27, richiesta utente: il profilo serve tutti i progetti RE/pentest): l'intera libreria è registrata via `skills.external_dirs` → i 46 moduli sono skill del profilo caricabili per nome (`apk-reverse`, `mobile-reverse`, `pwn-chain`, `attack-chain`, `malware-analysis`, `edr-bypass-re`, `windows-ad`, `js-reverse`, `ctf-sandbox`, `binary-ninja-reverse`, …). Restano soggetti al **gate di autorizzazione** (sistemi propri o esplicitamente autorizzati — contratto di scope del package, ripreso in `evidence-and-scope.md`). Il `CTF-Sandbox-Orchestrator` (42 scenari) resta dentro la libreria e si raggiunge dal modulo `ctf-sandbox`.

## File profondi che valgono una lettura diretta

(percorsi relativi alla libreria `~/.hermes/profiles/kernel-re/reverse-skill/`)

- `skills/reverse-engineering/kernel-driver-reverse.md` — LKM, rootkit, pattern C/C++ (la parte Windows è fuori perimetro)
- `skills/reverse-engineering/references/ollvm-deobfuscation.md` — deobfuscation OLLVM a stadi
- `skills/reverse-engineering/references/nonpe-format-cookbook.md` — ricette per formati non-PE
- `skills/reverse-engineering/anti-analysis.md`, `tools-dynamic.md`, `tools-advanced.md`, `patterns*.md`, `languages*.md`
- `skills/apk-reverse/references/frida-cookbook.md` + `scripts/*.sh` (decode / frida-run / rebuild — revisione prima dell'uso)
- `skills/firmware-pentest/references/extraction-methodology.md`, `emulation-and-fuzz.md`
- `skills/binary-diff/references/prompt-template.md`
- `skills/ops/analysis-decision-framework.md`, `analysis-blindspot-cookbook.md`
- `skills/field-journal/_index.md` — esempi di diario, utili come formato per i writeback

## Come si usa (e come non si usa)

- **Si caricano**: i 46 moduli sono skill del profilo (external dir) → `skill_view('apk-reverse')` ecc. I file di supporto (references/scripts del package) si leggono dalla libreria.
- **Scritti per il client originale**: contengono script `.ps1`, path Windows e un bootstrap che si auto-installa → su questo host usa i contenuti metodologici e i comandi portabili (spesso esiste l'equivalente `.sh`), **non eseguire il bootstrap**; per l'inventario strumenti affidabile valgono `doctor.sh` + `routing-map.md` (`skills/tool-index.md` della libreria è l'adattatore host-specifico scritto da noi).
- **Gate di autorizzazione**: pentest/attack/pwn/EDR si usano solo su sistemi propri o autorizzati (regola del package + `evidence-and-scope.md`).
- A runtime il routing è quello di Hermes (descrizioni delle skill); `routing.json` del package resta come riferimento.
