# Moduli vendor: tasto, alimentazione, Type-C (scoperta 2026-09-25)

## Il fatto che spiega tre problemi in uno

Nella chroot **nessuno carica i moduli automaticamente**: l'autoload del kernel
richiede un helper userspace (modprobe/uevent) che il rootfs minimale non ha.
I moduli presenti li ha messi il boot vendor **prima** di noi; tutti gli altri non
compaiono mai. Risultato: tasto laterale, controllo dell'alimentazione e Type-C
sembrano *non supportati* — invece manca solo **chi carica i driver**.

Prova dell'oracolo (Android, stesso identico kernel `5.10.66-android12-9-...`):
là i dispositivi input sono 13 (noi ne avevamo 1) e `/sys/class/power_supply`
esiste. Stesso kernel, stessi `.ko` → la differenza era nel **caricamento**.

## La catena che serve (verificata, ordine importante)

```
qcom-pon          ← crea i figli del PMIC: senza questo, pwrkey non si lega MAI
pm8941-pwrkey     ← il tasto laterale (KEY_POWER, bit 116)
pmic-pon-log
qti_battery_charger / charger-ulog-glink / ucsi_glink   ← client di pmic_glink
```

- I `.ko` sono in `/usr/lib/nx679j/modem/kmod/` (7 file, ~250 KB) e lo script
  `nx679j-modules.sh` li carica dal launcher a ogni avvio.
- **I nomi sono trappole**: il modulo PON si chiama `qcom-pon.ko`, non
  `qpnp-power-on.ko` (il sorgente si chiama così, il modulo no). Cercare per
  sorgente porta a un falso "non esiste".
- `pm8941-pwrkey` registra il driver ma **non si lega** finché manca `qcom-pon`:
  il driver c'è, il dispositivo no. Il prerequisito è parte della sonda.

## Nodi /dev/input senza udev

La chroot non ha udev, quindi i nodi si creano a mano — e i numeri di device si
**leggono dal kernel** (`/sys/class/input/eventN/dev`), mai stimati:
l'ordine di registrazione cambia da avvio ad avvio (`pwrkey` può essere event0
o event2 a seconda di quando il modulo viene caricato).

La UI **scansiona** `/dev/input/event%d` e sceglie il device giusto: è questo che
l'ha salvata dal cambio di numerazione. Un percorso fisso avrebbe rotto il touch.

## Cosa NON fare (errore fatto e corretto lo stesso giorno)

Lo script ricaricava `pmic_glink` quando non trovava `/sys/class/power_supply`.
La condizione era **permanentemente vera**, quindi la ricarica scattava **a ogni
avvio** e abbatteva la sessione dati del modem (`rmnet_data0` senza indirizzo,
ping KO).

> **Regola**: un rimedio automatico va eseguito una volta sola, o protetto da una
> condizione che può essere vera **solo** quando il guasto c'è davvero. Se si
> riattiva quando non ottiene l'obiettivo, diventa un danno ricorrente e silenzioso.

Il caricabatterie **non espone** comunque `/sys/class/power_supply` su questo
setup: la carica funziona (autonoma nel PMIC) ma **non è controllabile** da qui.
Dire "non leggo i mA" è corretto; dire "non carica" sarebbe falso.
