# Workflow a fasi, gate e regole anti-loop

Adattato da `reverse-skill` (`skills/reverse-engineering/references/re-agent-workflow.md`, `ops/analysis-decision-framework.md`, `ops/analysis-blindspot-cookbook.md`) al nostro contesto (Linux/kernel/Android). I gate sono regole; timebox e default si possono modificare con motivazione.

## 0. Prima di iniziare

- Scope dichiarato (vedi `evidence-and-scope.md`).
- Inventario strumenti: `binary-recon/scripts/doctor.sh`.
- Una domanda alla volta: scrivi l'ipotesi corrente *prima* di aprire altri tool. Se non riesci a formulare un'ipotesi, sei ancora in triage.

## 1. Triage (minuti, sempre)

- [ ] hash sha256 del campione (identità unica)
- [ ] tipo/architettura/endianness (`file -k`, `readelf`)
- [ ] ancore minime obbligatorie per tipo (tabella sotto)
- [ ] pickup veloce: `strings -n 8`, `rabin2 -z`
- [ ] ipotesi iniziale + esperimento che la verifica

| Tipo | Ancore minime | Comando |
|---|---|---|
| ELF .so/binario | import + export + sezioni | `rabin2 -i`, `rabin2 -E`, `readelf -SW` |
| Modulo `.ko` | modinfo + alias + simboli non definiti | `modinfo`, `readelf -p .modinfo`, `nm -u` |
| APK | manifest + componenti + presenza `.so` | `apktool d`, `unzip -l` |
| Firmware | magic + mappa entropia | `binwalk`, `binwalk -E` |
| PCAP | conversazioni + protocolli | `tshark -r x.pcap -q -z io,phs` |

**Gate**: senza ancore (o senza evidenza del perché non sono leggibili) il triage NON è completo. Ancora non leggibile (packing/cifratura/tool mancante) → registra `E-<passo>-fail` con l'output e prosegui su un'altra via. Vietato dichiarare fatto ciò che non lo è.

## 2. Static

- Mira: xref da stringhe/import verso le funzioni che contano; mai "disassembla tutto".
- Un cambio strumento alla volta, motivato da un'evidenza (non "per provare").
- **Timebox**: ~15 min di static senza percorso chiave → passa alla dinamica o riformula l'ipotesi.
- `.ko`/`.so`: parti dai punti d'ingresso (`init_module`, `probe`, export JNI, `file_operations`).

## 3. Dynamic (solo su copia / ambiente isolato; device solo se autorizzato)

- ELF: `gdb` (breakpoint relativi per PIE, watchpoint); QEMU per archi non host (`qemu-aarch64-static -L rootfs ./bin`).
- `.ko` su Linux vivo: `ftrace`/`kprobes`/`bpftrace` — osservare, non strumentare a caso. In QEMU: `-s -S` + gdb con vmlinux.
- Android: Frida su device autorizzato, **prima hook di sola lettura** (log); modifica solo se necessaria.
- La dinamica **verifica** le ipotesi dello static: dopo ogni run torna allo static con l'evidenza nuova (ciclo, non sequenza unica).
- Comportamento inatteso (anti-debug, self-integrity): registra l'evidenza, cambia angolo (livello più basso, patch su copia, emulazione).

## 4. Synthesis

- Finding: cosa è vero e perché conta, ancorato a evidenze.
- Path: catena di chiamate / flusso dati passo-passo con riferimenti `E-*`.
- Zone d'ombra: cosa non è stato determinato (con impatto).
- Se è un deliverable: `re-driver-report`.

## 5. Anti-loop e self-supervision

| Regola | Dettaglio |
|---|---|
| 3 azioni senza evidenza nuova | STOP → ripianifica (nuova ipotesi o nuovo stadio), **dichiarando il cambio** |
| Stesso comando + stessi parametri | non si ripete senza un dato nuovo |
| 2–3 fallimenti dello stesso approccio | ricerca esterna PRIMA di insistere (`block-research-fanout`) |
| Timebox | static ~15 min; step dinamico senza segnale → torna allo static |
| Bias di stadio | se sei "innamorato" di un tool, dichiaralo e testa l'alternativa |
| Claim non ancorati | vietato: ogni affermazione mappa su un'evidenza, o è marcata ipotesi |

## 6. Blindspot frequenti (trigger → azione → evidenza)

| Trigger | Azione | Evidenza |
|---|---|---|
| Mangling `_ZN`, `core::`, `panic` | è Rust: symbol recovery; non fidarti del decompile C-like | E-rust |
| `runtime.`, `go.string`, pclntab | è Go: usa stringhe/pclntab, diffida del decompile | E-go |
| CFG a stella `while+switch` | OLLVM control-flow flattening: identifica il tier, poi dinamica | E-cff |
| Branche sempre vere/false | opaque predicates: dinamica o deobfuscator | E-opaque-pred |
| Alto entropia nei dati, decodificatori assenti | crypto custom: trova la routine di decode, dump dopo decrypt | E-custom-crypto |
| Sezioni VMP-like / entropia estrema | dichiarazioni qualitative + dinamica; mai "devirtualizzato" senza prova | E-vmp |
| API/CFG "spiegati" senza offset né output | claim non ancorato: rimuovi o àncoralo a un comando | ungrounded |

Riferimenti profondi nel clone: `skills/reverse-engineering/anti-analysis.md`, `references/ollvm-deobfuscation.md`, `references/nonpe-format-cookbook.md`, `skills/ops/analysis-blindspot-cookbook.md`.

## 7. Feasibility gate sulle istruzioni dell'utente

Se l'utente chiede il passo X ma manca un prerequisito noto (es. chiedere la tabella import di un campione packed):

1. dillo in una riga (cosa blocca e perché);
2. proponi l'ordine alternativo (prerequisito prima, poi X);
3. se l'utente accetta → fai il prerequisito e POI X;
4. se l'utente **forza** X subito → esegui, marca la qualità (`quality=unreadable`) e non trarne conclusioni;
5. vietato sostituire X con un altro passo spacciandolo per fatto.

## Appendice A — Migrazione simboli tra versioni (dal modulo `binary-diff`)

Quando: hai un binario vecchio con simboli e uno nuovo senza (due build dello stesso modulo vendor, oppure modulo vendor ↔ derive kernel sorgente).

Metodo (LLM-assisted, 1 funzione per volta, costo minimo):

1. Apri entrambi in un disassembler (r2 va bene); trova **ancore affidabili**: export, stringhe, costanti magiche.
2. Esporta per le funzioni ancora: disasm + pseudo-codice (vecchio) e disasm (nuovo).
3. Prompt fisso: "ecco la funzione di riferimento (con simboli) e la funzione da re-innominare; elenca i riferimenti a {simboli} come YAML" con schema: `found_call`, `found_vcall`, `found_funcptr`, `found_gv`, `found_struct_offset` (campi: `insn_va`, `insn_disasm`, `offset`, `func_name`).
4. Applica i rename via script (r2: `afn nome @ addr`; IDA: script Python).
5. I risultati diventano nuove ancore → itera.

Regole: mai l'intero binario al LLM; verifica a campione (LLM non è accurato al 100%); cache dei risultati. Prompt completo: `~/.hermes/profiles/kernel-re/reverse-skill/skills/binary-diff/references/prompt-template.md`.

Diff strutturale rapido senza LLM: `radiff2 -C a b` (funzioni) / `radiff2 -s a b` (stringhe). BinDiff/Diaphora solo se disponibili (non su questo host).
