#!/bin/sh
# Ripristino del device dopo lo stallo dell'interfaccia (25/09).
# Riavvia il sistema: la UI riparte pulita e NON rientra in standby (l'innesco
# e' disattivato nella nuova immagine; in questa vecchia il file di servizio
# viene rimosso prima).
rm -f /tmp/ui-standby
sync
(sleep 2; /sbin/reboot -f) >/dev/null 2>&1 &
echo "ripristino richiesto"
