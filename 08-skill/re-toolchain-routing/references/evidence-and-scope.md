# Gate di scope e catena di evidenza

Adattato da `reverse-skill` (`ops/scope-contract.md`, `ops/evidence-finding-path.md`, `ops/analysis-decision-framework.md`) alle regole di questo profilo. Obiettivo: (1) nessuna azione su un target senza autorizzazione dichiarata; (2) ogni conclusione tracciabile a un comando riproducibile.

## 1. Gate di scope (prima di ACT)

Per ogni lavoro dichiara in apertura (basta un blocco nel log o nel report):

- **target**: file/target con hash, oppure device/asset preciso;
- **motivazione**: interoperabilità / diagnosi / documentazione (il perimetro del profilo);
- **autorizzazione**: `own_system` (dispositivo proprio dell'utente — caso normale) | `lab_only` | `authorized_target` (con riferimento a chi ha autorizzato);
- **profilo di rete**: `offline` (default per l'analisi: nessun pacchetto verso il target) | `lab_only` | `authorized_target_only`;
- **limiti espliciti**: cosa NON si fa (es. nessuna scrittura su partizioni, nessun modulo sperimentale sul kernel di lavoro, niente sudo d'iniziativa).

Per il NX679J valgono inoltre le regole di `nx679j-openwrt`: flash solo col protocollo completo, backup prima di ogni scrittura, device dell'utente.

**Hard gate**: se l'autorizzazione non è chiara → nessuna azione attiva. L'analisi di file locali è sempre ok; le azioni verso un sistema richiedono le condizioni sopra.

## 2. Evidence → Finding → Path

### Evidence (osservazione, immutabile)

```markdown
### E-<nnn> <titolo breve>
- observed_at: <ISO>
- source_type: command | file | log | memory | network | manual
- source_ref: <comando esatto o path>
- hash: <sha256 se è un file; altrimenti n/a>
- repro_command: <comando rieseguibile da chiunque>
- raw_excerpt: <estratto, anche di fallimento>
- supersedes: E-<nnn> | none
```

Un **fallimento è un'evidenza**: output vuoto, tool che erra, ancora non leggibile → si registra (`E-<passo>-fail`), non si cancella. Mai salti silenziosi.

### Finding (conclusione)

```markdown
### F-<nnn> <titolo>
- category: reverse_algo | design | bug | capability | other
- status: candidate | validated | false_positive
- evidence_ids: [E-..., ...]        # MAI vuoto
- location: <file:offset | addr | classe.metodo | riga>
- confidence: high | medium | low
- repro_steps: [...]
- note: <cosa manca per validarlo>
```

### Path (flusso)

```markdown
### P-<nnn> <titolo>
- path_type: callflow | dataflow | boot-chain | ioctl-sequence
- start: ...  goal: ...
- steps:
  1. <azione> — evidence: E-... — finding: F-... | none
- residual_risks: ...
```

## 3. Soglia di validazione

| status | evidenza richiesta |
|---|---|
| candidate | ≥1 |
| **validated** | **≥2 indipendenti** (ideale: 1 statica + 1 dinamica). Una sola evidenza NON basta: resta candidate (con residuo dichiarato) o serve conferma umana |
| promozione bloccata | registra `E-insufficient-evidence` e dì cosa servirebbe |

Corollari (decision framework del package, adattati):

- Claim non ancorate = vietate (o marcate `ipotesi`).
- Fine di ogni fase: dichiara l'ipotesi e scegli **continua / cambia / stop** con l'evidenza che lo giustifica.
- Evidenza negativa: un ramo verificato assente si registra (`E-negative-evidence`) — "non c'è" è un risultato.
- Confidenza bassa dal decompile → programma la dinamica prima di dichiarare validato.
- Deadlock (3 azioni senza evidenza nuova) → ripianifica, non reiterare.

## 4. Checklist di chiusura

- [ ] scelta tool/percorso documentata (con il perché dell'alternativa scartata)
- [ ] ancore minime per tipo raccolte, o fallimento registrato
- [ ] ogni claim del deliverable mappato su evidenze (o marcato ipotesi)
- [ ] stato di verifica dichiarato (verificato / osservato / non testato / noto non funzionante)
- [ ] lezioni riutilizzabili scritte nella skill del dominio (non solo nel report)
- [ ] per il device: `STATO-ATTUALE.md` aggiornato

## 5. Mappatura sulle pratiche esistenti

- Fatti vs ipotesi: già regola del profilo; qui ha i campi formali.
- Anti-loop di progetto (NX679J): `STATO-ATTUALE.md` = il nostro evidence/timeline log in versione device.
- Report finale: `re-driver-report` (la tabella valori→fonte è la versione driver-friendly di questa catena).
