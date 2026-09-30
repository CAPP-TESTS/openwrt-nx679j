    # v54: WLAN QCA6490 bring-up in BACKGROUND (verificato live 2026-09-20).
    # Il boot NON dipende dal Wi-Fi: un fallimento qui non tocca usb0/SSH/modulo.
    ( $BB sleep 25
      KV=$(cat /proc/sys/kernel/osrelease 2>/dev/null)
      [ -n "$KV" ] || KV=5.10.66-android12-9-00005-gf6e6376090be-ab8060604
      say "v54 wifi: start (KV=$KV)"
      $BB chroot /owrt ubus list >/dev/null 2>&1 || ( $BB chroot /owrt /sbin/ubusd 2>>$J & )
      $BB sleep 2
      for m in cnss_prealloc cnss_utils cnss_nl wlan_firmware_service cnss_plat_ipc_qmi_svc qcom_ramdump qrtr-mhi cnss2; do
          if [ -f /owrt/lib/modules/$KV/$m.ko ]; then
              $BB chroot /owrt /sbin/insmod /lib/modules/$KV/$m.ko 2>>$J && say "v54 wifi: $m OK" || say "v54 wifi: $m FAIL"
          else
              say "v54 wifi: $m MANCANTE"
          fi
      done
      $BB sleep 2
      echo 1 > /sys/devices/platform/soc/b0000000.qcom,cnss-qca6490/fs_ready 2>>$J && say "v54 wifi: fs_ready=1"
      $BB sleep 2
      if [ -f /owrt/lib/modules/$KV/qca_cld3_qca6490.ko ]; then
          $BB chroot /owrt /sbin/insmod /lib/modules/$KV/qca_cld3_qca6490.ko 2>>$J && say "v54 wifi: qca_cld3 OK" || say "v54 wifi: qca_cld3 FAIL"
      else
          say "v54 wifi: qca_cld3 MANCANTE"
      fi
      i=0
      while [ $i -lt 100 ]; do
          [ -d /sys/class/ieee80211/phy0 ] && break
          $BB sleep 2; i=$((i+1))
      done
      say "v54 wifi: phy0 dopo $((i*2))s: $(ls /sys/class/ieee80211/ 2>/dev/null | tr '\n' ' ')"
      $BB chroot /owrt /etc/init.d/wpad start 2>>$J
      $BB sleep 2
      $BB chroot /owrt /etc/init.d/rpcd start 2>>$J
      $BB chroot /owrt /etc/init.d/uhttpd start 2>>$J
      $BB chroot /owrt /etc/init.d/dnsmasq start 2>>$J
      $BB chroot /owrt /etc/init.d/network start 2>>$J
      $BB sleep 8
      $BB chroot /owrt /sbin/wifi up 2>>$J
      $BB sleep 5
      say "v54 wifi: iface: $(ls /sys/class/net 2>/dev/null | grep -E 'ap0|^wlan' | tr '\n' ' ')"
      say "v54 wifi: fine"
    ) &
