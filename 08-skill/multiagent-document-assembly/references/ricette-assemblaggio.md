# Ricette: estrazione, assemblaggio, correzioni

Snippet pronti per la pipeline del documento multi-sezione.

## Estrarre una sezione dal file salvato da un agente (guscio JSON)

```python
import json, pathlib
raw = (pathlib.Path('/tmp/parts') / fn).read_text().strip()
if raw.startswith('```'): raw = raw.split('\n', 1)[1]
if raw.rstrip().endswith('```'): raw = raw.rstrip().rsplit('```', 1)[0]
obj = json.loads(raw.strip())
section = obj.get('section_markdown', '')
```

Se il parse fallisce, il file è markdown puro: usarlo così com'è. Mai incollare il guscio JSON nel documento.

## Inserire una parte con ancona unica

```python
t = doc.read_text(); anchor = "## Titolo sezione"
assert t.count(anchor) == 1, "ancora ambigua: l'indice usa link (titolo), l'heading no"
i = t.find(anchor)
doc.write_text(t[:i] + block + "\n\n---\n\n" + t[i:])
```

## Sostituzioni con conteggio (il pattern delle correzioni)

```python
fixes = [(old1, new1), (old2, new2)]
applied, skipped = [], []
for old, new in fixes:
    n = t.count(old)
    if n == 1:
        t = t.replace(old, new); applied.append(old[:50])
    else:
        skipped.append((n, old[:70]))   # 0 o >1: rivedere la stringa
```

Le saltate si riprovano con regex flessibile (spaziature multiple, virgolette tipografiche, prefissi di percorso). Sempre `cp documento documento.bak` prima della passata.

## Verifica esadecimale (contro il mascheramento degli output)

```python
seg = t[i-6:i].encode()
print(seg.hex())                       # i byte veri, non mascherati: 6e 78 36 37 39 6a = 'nx679j'
import re
print('residui segnaposto:', len(re.findall(r'redact|«\.\.\.»', t)))
```

Il conteggio interno a python e la codifica esadecimale non passano dal filtro dei transcript: sono l'unica prova affidabile che una sostituzione è avvenuta sul file.

## Struttura finale

```bash
grep -nE '^## ' DOCUMENTAZIONE.md    # sezioni presenti e ordine
wc -c -l DOCUMENTAZIONE.md
grep -n '^- \[' DOCUMENTAZIONE.md     # indice allineato alle sezioni
```

## Pulizia prima della consegna

- Virgolette tipografiche dentro i blocchi di comandi → sostituire con quelle dritte (`t.replace('\u201c','"').replace('\u201d','"')`); controllare anche gli apostrofi tipografici nei percorsi.
- Duplicati di heading creati dall'assemblaggio: ogni titolo di sezione deve comparire una volta come heading (nel solo indice come link).
- Snapshot: `cp documento docs-snapshots/$(date +%Y%m%d-%H%M%S)/` se il progetto ha la convenzione degli snapshot datati.
