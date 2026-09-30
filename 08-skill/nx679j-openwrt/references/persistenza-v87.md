# Persistenza NX679J — sistema v87

## Il problema

La rootfs OpenWrt è **volatile** (ram chroot su RAM). Anche `/rfs` e il rawdump sono volatili (rawdump PULITO al boot >15MB). La dir `/«redacted»` (switch.sh, relay, worker, journal) vive **NEL RAMDISK dell'immagine boot_b** — viene ri-estratta dall'immagine ad ogni boot. **La persistenza vera = il ramdisk stesso.**

## Il meccanismo (v87, verificato)

```
Host:  persist-tars/{etc,luci,tools}.tar
           ↓ build-v87.py (PERSIST-INSIDE: estrae i tar sopra OVL/owrt/, sovrascrive)
       boot_b-v87-final.img  (md5 0d23aa57b0a1365975c2d5ef57826c66)
           ↓ scp + dd sde41 + 2 letture fredde + reboot
Device: LUCI=1 TOOL=2 UCI=2 MODEM=1 ✓
```

## Aggiornare (3 passi, ~1 min, tutto da SSH)

1. **Sul device**: modifica → `cd /; tar cf /tmp/pw/<nome>.tar <percorsi>` → scp sul PC in `persist-tars/`
2. **Build**: `python3 build-v87.py` (~10s; fa anche gli assert: modemmanager.js, atom11, network config)
3. **Flash**: `scp` img → `rm -f /dev/sde41; mknod /dev/sde41 b 259 25; dd oflag=direct conv=fsync` → 2 letture fredde → reboot

## Contenuti attuali dei tar

| Tar | Contenuto |
|---|---|
| `etc.tar` | `/etc` completo — config UCI con `network.modem.device='qcom-soc'` + `force_connection='1'` |
| `luci.tar` | `/www/luci-static` + `/usr/share/luci` + `/usr/share/rpcd/acl.d` (LuCI 26.263 + luci-proto-modemmanager) |
| `tools.tar` | `/usr/lib/«redacted»` completo (42 file: atxxxx 8–11, guard v2, drmtest, paneltest, touchsim, ecc.) |

## Creare un tar (dettagli)

```sh
# /etc completo:
cd /; tar cf /tmp/pw/etc.tar etc
# LuCI:
cd /; tar cf /tmp/pw/luci.tar www/luci-static usr/share/luci usr/share/rpcd/acl.d
# Tool (attenzione ai nomi unicode — usare la variabile dalla glob):
D=$(ls -d /usr/lib/*/modem); DIRLIB=$(dirname "$D"); cd /; tar cf /tmp/pw/tools.tar "${DIRLIB#/}"
```

## Note

- Il build fa **assert** sui contenuti: se un tar manca o il contenuto chiave non c'è, il build fallisce (protezione).
- I tar vengono estratti con un copier custom che gestisce symlink e file esistenti (copytree standard falliva su symlink dangling).
- Il ramdisk ha un limite: l'immagine boot è 96MB fissi; il ramdisk attuale ~44MB. Se si aggiungono file GRANDI, ridurre i tar (es. solo i tool nuovi).
- **NON mettere i tar nel rawdump**: viene pulito al boot.

## Comando di verifica (post-boot)

```sh
ssh -i ~/.ssh/nx679j_key root@10.0.0.1 'L=$(ls /www/luci-static/resources/protocol/modemmanager.js >/dev/null 2>&1 && echo 1 || echo 0); T=$(ls /usr/lib/*/modem/ | grep -cE "atom11|guard"); U=$(uci show network.modem | grep -cE "qcom-soc|force"); M=$(ubus call network.interface.modem status | grep -c "\"up\": true"); echo "LUCI=$L TOOL=$T UCI=$U MODEM=$M"'
```
