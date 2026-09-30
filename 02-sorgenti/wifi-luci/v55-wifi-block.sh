    # v55: WLAN QCA6490 bring-up in BACKGROUND (v54 + fix da boot reale 2026-09-20:
    # logd attivo, attesa driver completa, retry di wifi up, mkdir resolv).
    # Il boot NON dipende dal Wi-Fi: un fallimento qui non tocca usb0/SSH/modulo.
    ( $BB sleep 25
      KV=$(cat /proc/sys/kernel/osrelease 2>/dev/null)
      [ -n "$KV" ] || KV=5.10.66-android12-9-00005-gf6e6376090be-ab8060604
      say "v55 wifi: start (KV=$KV)"
      [ -x /owrt/etc/init.d/log ] && $BB chroot /owrt /etc/init.d/log start 2>>$J
      $BB chroot /owrt ubus list >/dev/null 2>&1 || ( $BB chroot /owrt /sbin/ubusd 2>>$J & )
      $BB sleep 2
      for m in cnss_prealloc cnss_utils cnss_nl wlan_firmware_service cnss_plat_ipc_qmi_svc qcom_ramdump qrtr-mhi cnss2; do
          if [ -f /owrt/lib/modules/$KV/$m.ko ]; then
              $BB chroot /owrt /sbin/insmod /lib/modules/$KV/$m.ko 2>>$J && say "v55 wifi: $m OK" || say "v55 wifi: $m FAIL"
          else
              say "v55 wifi: $m MANCANTE"
          fi
      done
      $BB sleep 2
      echo 1 > /sys/devices/platform/soc/b0000000.qcom,cnss-qca6490/fs_ready 2>>$J && say "v55 wifi: fs_ready=1"
      $BB sleep 2
      if [ -f /owrt/lib/modules/$KV/qca_cld3_qca6490.ko ]; then
          $BB chroot /owrt /sbin/insmod /lib/modules/$KV/qca_cld3_qca6490.ko 2>>$J && say "v55 wifi: qca_cld3 OK" || say "v55 wifi: qca_cld3 FAIL"
      else
          say "v55 wifi: qca_cld3 MANCANTE"
      fi
      i=0
      while [ $i -lt 100 ]; do
          if [ -e /sys/class/ieee80211/phy0 ] && [ -e /sys/class/net/wlan0 ]; then break; fi
          $BB sleep 2; i=$((i+1))
      done
      say "v55 wifi: phy0/wlan0 dopo $((i*2))s: $(ls /sys/class/ieee80211/ 2>/dev/null | tr '\n' ' ') $(ls /sys/class/net 2>/dev/null | grep -E '^wlan' | tr '\n' ' ')"
      $BB sleep 10
      $BB chroot /owrt mkdir -p /var/run/hostapd /var/run/wpa_supplicant /tmp/resolv.conf.d 2>>$J
      $BB chroot /owrt rm -f /var/run/hostapd/global 2>>$J
      [ -x /owrt/etc/init.d/wpad ] && $BB chroot /owrt /etc/init.d/wpad start 2>>$J
      i=0
      while [ $i -lt 15 ]; do
          $BB chroot /owrt ubus list 2>/dev/null | grep -q '^hostapd$' && break
          $BB sleep 2; i=$((i+1))
      done
      say "v55 wifi: hostapd ubus dopo $((i*2))s"
      [ -x /owrt/etc/init.d/rpcd ] && $BB chroot /owrt /etc/init.d/rpcd start 2>>$J
      [ -x /owrt/etc/init.d/uhttpd ] && $BB chroot /owrt /etc/init.d/uhttpd start 2>>$J
      [ -x /owrt/etc/init.d/dnsmasq ] && $BB chroot /owrt /etc/init.d/dnsmasq start 2>>$J
      [ -x /owrt/etc/init.d/network ] && $BB chroot /owrt /etc/init.d/network start 2>>$J
      $BB sleep 8
      i=0
      while [ $i -lt 6 ]; do
          $BB chroot /owrt /sbin/wifi up 2>>$J
          $BB sleep 10
          [ -e /sys/class/net/phy0-ap0 ] && break
          i=$((i+1))
          say "v55 wifi: retry wifi up $i"
      done
      $BB sleep 5
      say "v55 wifi: iface: $(ls /sys/class/net 2>/dev/null | grep -E 'ap0|^wlan' | tr '\n' ' ')"
      if [ -e /sys/class/net/phy0-ap0 ]; then
          $BB chroot /owrt ip addr add 192.168.77.1/24 dev phy0-ap0 2>>$J
      fi
      say "v55 wifi: fine"
    ) &
