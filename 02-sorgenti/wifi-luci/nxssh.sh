#!/bin/sh
# SSH helper for NX679J (pubkey nx679j_key) - 2026-09-20
# Nota: la host key dropbear viene rigenerata ad ogni boot -> il known_hosts
# del PC diventa stale: si ignora con UserKnownHostsFile=/dev/null.
exec ssh -i /home/user/.ssh/nx679j_key -o IdentitiesOnly=yes -o ConnectTimeout=8 \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@10.0.0.1 "$@"
