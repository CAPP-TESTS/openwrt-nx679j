#!/owrt/bin/busybox sh
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
# v15 hand-over.  Every step is appended to the journal so the relay can show
# it live, and PID 1 CANNOT die: a dead PID 1 is a kernel panic, which is the
# reboot loop we just observed on hardware.  Only a fully prepared root is
# handed over with exec; anything else leaves the shell alive forever.
BB=/owrt/bin/busybox
J=/nx679j-journal
say() { echo "switch.sh: $*" >> $J 2>/dev/null; }
say "start pid=$$"
$BB mkdir -p /owrt/proc /owrt/sys /owrt/dev /owrt/dev/pts /owrt/tmp /owrt/run
for m in "proc proc /owrt/proc" "sysfs sysfs /owrt/sys" "tmpfs tmpfs /owrt/dev" "tmpfs tmpfs /owrt/tmp" "tmpfs tmpfs /owrt/run" "devpts devpts /owrt/dev/pts"; do
    set -- $m
    if $BB mount -t $1 $2 $3 2>>$J; then say "mount $3 OK"; else say "mount $3 FAILED"; fi
done
$BB mknod -m 600 /owrt/dev/console c 5 1 2>>$J
$BB mknod -m 666 /owrt/dev/null    c 1 3 2>>$J
$BB mknod -m 666 /owrt/dev/zero    c 1 5 2>>$J
$BB mknod -m 666 /owrt/dev/tty     c 5 0 2>>$J
$BB mknod -m 666 /owrt/dev/random  c 1 8 2>>$J
$BB mknod -m 666 /owrt/dev/urandom c 1 9 2>>$J
$BB mknod -m 666 /owrt/dev/ptmx    c 5 2 2>>$J
say "mdev -s"
$BB mdev -s 2>>$J
say "mdev rc=$?; /sbin/init executable: $([ -x /owrt/sbin/init ] && echo YES || echo NO)"
$BB mount --bind /owrt /owrt 2>/dev/null
if [ -x /owrt/sbin/init ]; then
    # v23: i mount DEVONO essere fatti dall'INTERNO del chroot.  Misurato via SSH:
    # dentro /owrt `ps w` era vuoto, "mount: no /proc/mounts", /proc/<pid>/status
    # inesistente, /dev con soli kmsg/null/pts/urandom -> procd non puo' partire.
    CI="$BB chroot /owrt /bin/busybox"
    $CI mount -t proc proc /proc 2>>$J
    $CI mount -t sysfs sysfs /sys 2>>$J
    say "v23: mount interni fatti"
    # v60: PERSISTENZA DAL RAWDUMp (tar a offset fissi) - PRIMA di ubusd/rpcd/uhttpd
    # layout: etc@16MB(3MB), luci@18MB(4MB), tools@20MB(24MB)
    PRD=/dev/rd
    [ -e "$PRD" ] || PRD=/dev/block/by-name/rawdump
    if [ -e "$PRD" ]; then
        $BB dd if="$PRD" of=/tmp/p-etc.tar bs=1M skip=16 count=3 2>>$J
        $BB dd if="$PRD" of=/tmp/p-luci.tar bs=1M skip=18 count=4 2>>$J
        $BB dd if="$PRD" of=/tmp/p-tools.tar bs=1M skip=20 count=24 2>>$J
        for pt in /tmp/p-etc.tar /tmp/p-luci.tar /tmp/p-tools.tar; do
            if [ -s "$pt" ]; then
                $BB tar x -C /owrt -f "$pt" 2>>$J && say "v60: $(basename $pt) OK" || say "v60: $(basename $pt) FAIL"
            fi
        done
    else
        say "v60: rawdump non trovato!"
    fi
    for n in "666 /dev/null c 1 3" "600 /dev/console c 5 1" "666 /dev/zero c 1 5" "666 /dev/tty c 5 0" "666 /dev/urandom c 1 9" "666 /dev/ptmx c 5 2"; do
        set -- $n; $CI mknod -m $1 $2 $3 $4 $5 2>>$J
    done
    # v24: procd/rc.common esigono /var/lock - senza, OGNI init script muore con
    # "can't create /var/lock/procd_*.lock: nonexistent directory" (MISURATO via
    # SSH) e ubusd/netifd non partono mai.  /var e' symlink a /tmp (tmpfs vuoto).
    $CI mkdir -p /var/lock /var/run /var/log /var/state /var/tmp /tmp/lock /tmp/run /tmp/log
    # v25: PRE-AVVIO ubusd prima di procd.  Misurato: `/sbin/ubusd &` a mano funziona
    # e `ubus list` mostra l'albero standard, ma procd NON lo lancia -> ubusd/netifd
    # non partono mai.  Se ubus esiste gia', procd ha una ragione in meno per fermarsi.
    ( $CI /sbin/ubusd >>$J 2>&1 & ) ; $BB sleep 2
    say "v25: ubusd pre-avviato; ubus list: $( $CI ubus list 2>/dev/null | wc -l ) oggetti"
    # v29: il mount del firmware lo fa lo SCRIPT con busybox (provato a mano: riesce),
    # perche' la syscall raw dall'init non riesce.  DEVE precedere il probe forzato.
    # v30: vfat e le nls sono MODULI: al momento dello script il filesystem non era
    # ancora registrato (il mount falliva senza dire perche').  Li carico con il
    # kmodloader del root esterno (busybox insmod non funziona) e riprovo.
    for m in vfat nls_cp437 nls_iso8859-1; do
        [ -f /lib/modules/$m.ko ] && /sbin/insmod /lib/modules/$m.ko 2>>$J
    done
    for i in 1 2 3 4 5; do
        $BB mount -t vfat /dev/sde6 /vendor/firmware_mnt 2>>$J && break
        $BB sleep 1
    done
    say "v30: image/ -> $(ls /vendor/firmware_mnt/image 2>/dev/null | head -2 | tr '\n' ' ')"
    # v36: i file del modem devono stare in /lib/firmware (il path che il kernel cerca:
    # lo dice il dmesg di Android "Direct firmware load for modem.b11").  Il bind va
    # fatto con busybox: la syscall raw nell'init da' ENOENT (misurato, loggato in v35).
    $BB mount --bind /vendor/firmware_mnt/image /lib/firmware 2>>$J && say "v36: bind /lib/firmware OK" || say "v36: bind FALLITO"
    say "v36: /lib/firmware -> $(ls /lib/firmware 2>/dev/null | head -2 | tr '\n' ' ')"
    # v36: rmmod+insmod di q6v5_pas = NUOVA registrazione di driver = il kernel riprova
    # i device differiti (finche' il timeout non scade).  Col firmware ora al posto
    # giusto, se quello era il pezzo mancante il remoteproc si registra adesso.
    $BB rmmod qcom_q6v5_pas 2>>$J
    [ -x /owrt/sbin/kmodloader ] && /owrt/sbin/kmodloader /proc/1/root/lib/modules/qcom_q6v5_pas.ko 2>>$J
    $BB sleep 3
    say "v36: remoteproc dopo il reload: $(ls /sys/class/remoteproc/ 2>/dev/null | tr '\n' ' ')"
    # v46: gli altri remoteproc in BACKGROUND: il start e' sincrono e il modem
    # puo' metterci secondi: in primo piano bloccherebbe rete e SSH.
    # v47: il probe di q6v5_pas deve FINIRE prima che qualcuno scriva start:
    # scriverlo durante il probe fa fallire il probe e i rproc vengono rilasciati
    # (misurato: v46, "releasing" a 19.251s = esattamente quando partiva lo script).
    # v49: attesa piu' lunga + controllo di stato: avvio ogni rproc solo se esiste
    # ed e' offline (se il probe e' ancora in corso la scrittura lo farebbe fallire).
    ( $BB sleep 30
      for _r in 0 1 2 3 4; do
        [ -e /sys/class/remoteproc/remoteproc$_r/state ] || continue
        [ "$(cat /sys/class/remoteproc/remoteproc$_r/state 2>/dev/null)" = "offline" ] || continue
        echo start > /sys/class/remoteproc/remoteproc$_r/state 2>>$J
        say "v49: rproc$_r -> $(cat /sys/class/remoteproc/remoteproc$_r/state 2>/dev/null)"
      done ) &
    $BB mkdir -p /vendor/firmware_mnt
    if $BB mount -t vfat /dev/sde6 /vendor/firmware_mnt 2>>$J; then say "v29: firmware montato"; else say "v29: mount firmware FALLITO"; fi
    say "v29: contenuto image/: $(ls /vendor/firmware_mnt/image 2>/dev/null | head -3 | tr '\n' ' ')"
    # v28: risveglio i remoteproc differiti.  Il mount del firmware avviene DOPO la
    # catena moduli (vfat e' un modulo), quindi quando q6v5_pas ha fatto il probe il
    # firmware non c'era ancora: il device e' andato in deferred.  Forzare il probe
    # adesso che il firmware e' montato e' cio' che accende il core del modem.
    for d in 4080000.remoteproc-mss 3000000.remoteproc-adsp 32300000.remoteproc-cdsp 2400000.remoteproc-slpi; do
        echo "$d" > /sys/bus/platform/drivers_probe 2>>$J
    done
    say "v28: probe forzato; remoteproc: $(ls /sys/class/remoteproc/ 2>/dev/null | tr '\n' ' ')"
    say "starting procd as a CHILD; its output is redirected into this journal"
    # v22: dropbear DIRETTO, senza procd.  Con SSH dentro l'OpenWrt in esecuzione
    # si vede PERCHE' procd non parte (logread, ubus, ps, init.d status) invece di
    # guardarlo dal di fuori attraverso un rele' read-only.  Chiavi generate qui.
    ( $BB sleep 6
      $BB mkdir -p /owrt/etc/dropbear
      $BB chroot /owrt /usr/bin/dropbearkey -t rsa -f /etc/dropbear/dropbear_rsa_host_key >>$J 2>&1
      say "chiave host pronta rc=$?"
      $BB chroot /owrt /usr/sbin/dropbear -F -E -p 22 >>$J 2>&1
      say "dropbear terminato rc=$?" ) &
    # v21: SONDA DI MISURA.  12 s dopo il via a procd, tabella processi e porte in
    # ascolto finiscono in journal: cosi' si VEDE se dropbear e' partito e se
    # qualcuno ascolta sulla 22, invece di dedurlo dal rifiuto di connessione.
    ( $BB sleep 12; $BB ps >> $J 2>&1; $BB netstat -ltn >> $J 2>&1 ) &
    # v57: WLAN QCA6490 bring-up in BACKGROUND. Lezioni dai boot reali v54/v55:
    #  - i servizi vanno avviati DENTRO il chroot con PATH corretto (wrapper
    #    /etc/nx679j-wifi-services.sh), non con chiamate dirette dal contesto
    #    esterno (senza PATH gli init script falliscono);
    #  - ujail va disabilitato (EINVAL del kernel vendor sui jail procd);
    #  - la sequenza log->wpad->(ubus)->rpcd->uhttpd->dnsmasq->network->wifi up
    #    con retry e' quella validata live (AP+DHCP+LuCI OK, SSH sempre su usb0).
    # Il boot NON dipende dal Wi-Fi: un fallimento qui non tocca usb0/SSH/modulo.
    ( $BB sleep 25
      KV=$(cat /proc/sys/kernel/osrelease 2>/dev/null)
      [ -n "$KV" ] || KV=5.10.66-android12-9-00005-gf6e6376090be-ab8060604
      say "v57 wifi: start (KV=$KV)"
      $BB chroot /owrt ubus list >/dev/null 2>&1 || ( $BB chroot /owrt /sbin/ubusd 2>>$J & )
      $BB sleep 2
      for m in cnss_prealloc cnss_utils cnss_nl wlan_firmware_service cnss_plat_ipc_qmi_svc qcom_ramdump qrtr-mhi cnss2; do
          if [ -f /owrt/lib/modules/$KV/$m.ko ]; then
              $BB chroot /owrt /sbin/insmod /lib/modules/$KV/$m.ko 2>>$J && say "v57 wifi: $m OK" || say "v57 wifi: $m FAIL"
          else
              say "v57 wifi: $m MANCANTE"
          fi
      done
      $BB sleep 2
      echo 1 > /sys/devices/platform/soc/b0000000.qcom,cnss-qca6490/fs_ready 2>>$J && say "v57 wifi: fs_ready=1"
      $BB sleep 2
      if [ -f /owrt/lib/modules/$KV/qca_cld3_qca6490.ko ]; then
          $BB chroot /owrt /sbin/insmod /lib/modules/$KV/qca_cld3_qca6490.ko 2>>$J && say "v57 wifi: qca_cld3 OK" || say "v57 wifi: qca_cld3 FAIL"
      else
          say "v57 wifi: qca_cld3 MANCANTE"
      fi
      if [ -f /owrt/etc/nx679j-wifi-services.sh ]; then
          say "v57 wifi: lancio wrapper servizi+wifi (in chroot, PATH corretto)"
          $BB chroot /owrt /bin/sh /etc/nx679j-wifi-services.sh >>$J 2>&1
          say "v57 wifi: wrapper terminato"
          echo done > /owrt/tmp/wifi-wrapper.done 2>>$J
      else
          say "v57 wifi: wrapper MANCANTE"
          echo done > /owrt/tmp/wifi-wrapper.done 2>>$J
      fi
    ) &
    # avvia i servizi rc.d (l'immagine stock aveva inittab vuoto e niente rcS:
    # senza questo i servizi S* non partono mai). Aspetta che il procd sia su.
    ( $BB sleep 4; $BB chroot /owrt /bin/sh /etc/nx679j-boot-services.sh >>$J 2>&1; say "rcS S boot completato" ) &
    $BB chroot /owrt /sbin/init >> $J 2>&1
    say "procd returned rc=$? -- PID 1 stays alive (no panic, no reboot loop)"
fi
say "keeping PID 1 alive forever: the phone stays calm and readable"
while :; do $BB sleep 3600; done
