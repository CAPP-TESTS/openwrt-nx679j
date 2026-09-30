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
      else
          say "v57 wifi: wrapper MANCANTE"
      fi
    ) &
