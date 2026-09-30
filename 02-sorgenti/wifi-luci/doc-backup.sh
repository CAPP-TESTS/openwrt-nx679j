#!/bin/sh
# doc-backup — snapshot datato dei documenti del progetto.
# Nasce da un incidente reale (25/09/2026): una scrittura basata su una lettura
# parziale ha sovrascritto RIPRESA.md. Con lo snapshot quel danno si annulla in
# un comando. Lo chiama build-v90.py a OGNI build: non serve disciplina.
D="$(dirname "$0")/docs-snapshots/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$D" && cp "$(dirname "$0")"/*.md "$D"/ 2>/dev/null
echo "snapshot: $(ls -1 "$D" | wc -l) documenti in $D"
