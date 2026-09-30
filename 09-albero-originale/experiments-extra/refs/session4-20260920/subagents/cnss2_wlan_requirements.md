# cnss2 / QCA6490 (WCN6855) — cosa serve per far salire il WLAN su userspace NON-Android

**Scope**: analisi read-only di
`/home/user/nx679j-stock/kernel-patch-test/stock-kernel-source/drivers/net/wireless/cnss2/`
(main.c, pci.c, qmi.c, bus.c, power.c, genl.c, debug.c + headers), incrociata con:
- i moduli reali del device `/home/user/nx679j-stock/stock-modules/*.ko` (e le copie identiche in `port-work/...`),
- il DTB/FDT del device `kernel-patch-test/vendor_dtb.dts` e `port-work/vboot-dtb-swap/stock_dump/running_fdt.dts`,
- il core MHI/QRTR della stessa tree (`drivers/bus/mhi/core/`, `net/qrtr/`),
- i blob firmware locali `port-work/vboot-dtb-swap/stock_dump/firmware/all_firmware/fw_image/`.

Nessun file è stato modificato. Nessun accesso alla rete/device.

---

## 1. Versione della tree: NON corrisponde al kernel in uso

**FATTO**
- `kernel-patch-test/stock-kernel-source/Makefile:2-4` → `VERSION=5 PATCHLEVEL=4 SUBLEVEL=242`.
- Non è un Makefile "stantio": il *core* della tree è davvero pre-5.6/5.8.
  - `include/linux/mm_types.h` contiene `mmap_sem` e **zero** occorrenze di `mmap_lock` (rename avvenuto in v5.8) — l'unica occorrenza di `mmap_lock` in `include/linux` è in 2 file non core.
  - assenti: `net/mptcp/` (≥5.6), `kernel/watch_queue.c` (≥5.8), `include/linux/find.h`, `drivers/misc/uacce` (5.10), `fs/zonefs`, `kernel/entry/`; presente `fs/io_uring.c` (≥5.1).
- La tree è una **vendor Qualcomm lahinia/SM8350, era Android 11 (msm-5.4)**: `arch/arm64/boot/dts/vendor/qcom/lahaina.dtsi` + `lahaina-*.dts*`, `modules.list.msm.lahaina`, `build.config.msm.lahaina`; unici residui waipio: 3 file `arch/arm64/boot/dts/vendor/qcom/camera/waipio-camera*.dtsi` (stray). In `drivers/net/wireless/cnss2/Kconfig` c'è però il supporto `CNSS_QCA6490`/`CNSS_WCN7850` (backport).
- Le stesse API "nuove" non sono usate dal driver di questa tree: in `cnss2/*.c` **zero** occorrenze di `sysfs_emit`, `dev_err_probe`, `DEFINE_SHOW_ATTRIBUTE`, `proc_ops`, `pm_runtime_resume_and_get` (tutte ≥5.6/5.10) → il driver è coerente con un kernel 5.4.

**Conclusione onesta**: la tree **non è il sorgente del kernel 5.10.66** del device. Va usata per la *semantica* (flussi, nomi di proprietà, ordine di power-on), **non** come verità per nomi stringa/API: quelli vanno verificati sui binari (come richiesto).

**Controprova sui binari reali (già fatta)**
- `modinfo stock-modules/cnss2.ko` → `vermagic: 5.10.66-gki-g491fe99db339 SMP preempt mod_unload modversions aarch64`; tutte le copie locali (`stock-modules/`, `vendor_rd_build/lib/modules/`, `port-work/stock_vendor_ramdisk/...`, `port-work/nx679j-boot-assets/...`, `vboot-dtb-swap/stock_dump/vendor_modules/`) sono **identiche** (stesso md5 `1eba985e80c4…`).
- **Attenzione (rischio da verificare sul device)**: il kernel del device è dichiarato `5.10.66-android12-9-00005-gf6e6376090be-ab8060604`, mentre il `vermagic` dei moduli è `5.10.66-gki-g491fe99db339`. Un mismatch di `vermagic` = moduli non caricabili. Da confermare con `uname -r` + `modprobe cnss2` sul telefono (nessuno dei due simboli è verificabile da qui).
- Overlap sorgente↔binario (metrica): su 490 literal univoci (len≥8, senza `%`) di `cnss2/{main,pci,qmi,bus,power,genl,debug}.c`, **382 (78%)** sono presenti in `cnss2.ko` → stessa famiglia ma **revisione diversa**. Differenze puntuali (verificate con `strings`):
  - nel **sorgente** ma **assenti** nel binario: `wlan-pci-wake-gpio`, `default_gen_speed`, `chip_cfg`/`supported-ids`, `cnss-daemon-support`, `qcom,notify-modem-status`, `esoc-names`, `wlan_vregs`;
  - nel **binario** ma **assenti** nel sorgente: `qcom,vreg_ipa`, `qcom,vreg_ol_cpr`, `phy_ucode.elf`, `wlfw_cal_db.bin`;
  - presenti in entrambi (quindi usabili come contratto): `wlan-en-gpio`, `qcom,bt-en-gpio`, `qcom,sw-ctrl-gpio`, `qcom,xo-clk-gpio`, `qcom,wlan-rc-num`, `qcom,wlan-ramdump-dynamic`, `qcom,wlan-cbc-enabled`, `use-pm-domain`, `qcom,same-dt-multi-dev`, `qcom,converged-dt`, `qcom,icc-path-count`, `qcom,bus-bw-cfg*`, `qcom,iommu-*`, `qcom,cmd_db_name`, `qcom,tcs_offset_int_pow_amp_vreg`, `cnss-enable-self-recovery`, `qcom,drv-supported`, `qcom,set-wlaon-pwr-ctrl`, `qcom,enable-bootstrap-gpio`, `wlan-en-gpio`, `cnss-genl`, `cnss-genl-grp`.

---

## 2. Proprietà DT parsate dal driver (nomi esatti + funzione + file:riga)

Nodo **`qcom,cnss-qca6490@b0000000`** (`plat_dev->dev.of_node`; per DT non-converged anche `plat_priv->dev_node`, vedi `pci.c:5940-5983`):

| Proprietà | Funzione | file:riga | Obbl.? | Presente nel DT del device |
|---|---|---|---|---|
| `wlan-en-gpio` | `cnss_get_pinctrl` (solo `of_find_property`: abilita il lookup pinctrl, **non** è un gpiod) | power.c:54,754 | di fatto sì (senza, nessun WLAN_EN) | sì (running_fdt.dts:12976) |
| `pinctrl-names` = `"wlan_en_active"`,`"wlan_en_sleep"` / `pinctrl-0`,`pinctrl-1` | `cnss_get_pinctrl` → `pinctrl_lookup_state` | power.c:58-59, 755-773 | sì | sì (12979-12981) |
| `qcom,enable-bootstrap-gpio` | `cnss_get_pinctrl` (lookup stato `bootstrap_active`) | power.c:52,742-752 | no | no |
| `qcom,bt-en-gpio` | `cnss_get_pinctrl` → `of_get_named_gpio`; usato in `cnss_select_pinctrl_enable` (100 ms delay PMU) | power.c:55,777-780 / 938-955 | no | sì (12977) |
| `qcom,xo-clk-gpio` | `cnss_get_pinctrl` + `cnss_set_xo_clk_gpio_state` (pulse) | power.c:56,786-793 / 812-846 | no (ma QCA6490 lo usa) | sì (12991) |
| `qcom,sw-ctrl-gpio` | `cnss_get_pinctrl` | power.c:57,796-799 | no | sì (12978) |
| `wlan_vregs` | `cnss_get_vreg` (solo se converged-dt) | power.c:287-312 | no | no |
| `vdd-*-supply` (es. `vdd-wlan-aon/io/dig/rfa1/rfa2`, `wlan-ant-switch`) | `devm_regulator_get_optional(dev, name)` | power.c:87 | solo quelle presenti | sì (12992-13004) |
| `qcom,<nome-reg>-config` (5 u32: min_uv,max_uv,load_ua,delay_us,need_unvote) | `cnss_get_vreg_single` (`of_get_property`) | power.c:103-116 | no (default interni) | sì |
| `qcom,icc-path-count`, `qcom,bus-bw-cfg-count`, `qcom,bus-bw-cfg`, `interconnect-names` | `cnss_register_bus_scale` + `of_icc_get` | main.c:2586, 2593, 2609, 2627 | count=0 → skip | sì (13007-13010) |
| `qcom,wlan-ramdump-dynamic` | `cnss_ramdump_dynamic_register` / `_v2` | main.c:2248, 2331 | no | sì = 0x420000 (12984) |
| `use-pm-domain` | `cnss_get_pm_domain_info` | main.c:3067 | no | sì (12986) |
| `qcom,set-wlaon-pwr-ctrl` | `cnss_get_wlaon_pwr_ctrl_info` | main.c:3077 | no | no |
| `qcom,wlan-cbc-enabled` | `cnss_init_control_params` | main.c:3049 | no | sì (12985) |
| `cnss-daemon-support` | `cnss_init_control_params` (setta quirk per il *host driver*) | main.c:3044-3045 | no | no (assente anche nel binario) |
| `qcom,converged-dt` / `qcom,same-dt-multi-dev` | `cnss_is_converged_dt`, `cnss_use_fw_path_with_prefix` → **prefisso `qca6490/` sui firmware** | main.c:3083-3089, 3134, 3194 | no | `qcom,same-dt-multi-dev` **sì** (12988) → prefisso attivo |
| `use-nv-mac` | `cnss_use_nv_mac` | main.c:3128 | no | no |
| `qcom,notify-modem-status`, `esoc-names` | `cnss_register_esoc` | main.c:1049-1056 | no | no |
| `qcom,cmd_db_name` | `cnss_get_cpr_info` (via `cmd_db_read_addr`) | power.c:1172-1184 | no (serve al CPR/TCS) | no |
| `qcom,tcs_offset_int_pow_amp_vreg` | `cnss_enable_int_pow_amp_vreg` | power.c:1277 | no | no |
| `qcom,wlan-pci-wake-gpio` | `cnss_pci_wake_gpio_init` → `of_get_named_gpio` + `gpio_request` + irq falling | pci.c:5824-5843 | no | no (**e assente nel binario**) |
| `default_gen_speed` | `cnss_pci_enumerate` (QCA6490: forza Gen2) | pci.c:6198-6216 | no | no (**assente nel binario**) |
| `chip_cfg`/`supported-ids` (child node) | `cnss_pci_get_dev_cfg_node` (solo converged) | pci.c:5945-5982 | no | no (**assente nel binario**) |
| `qcom,wlan-rc-num` (array) | `cnss_pci_init` → `of_get_property` | pci.c:6249-6262 | **sì** (errore se assente) | sì = `<0>` (12983) |
| `memory-region` (+`qcom,iommu-group`) sul child `cnss_pci` | `of_reserved_mem_device_init` / `cnss_pci_init_smmu` | pci.c:6027 / 4306 | no | sì (16751-16759) |
| `qcom,iommu-dma`, `qcom,iommu-dma-addr-pool`, `qcom,iommu-geometry` (nodo iommu-group) | `cnss_pci_init_smmu` | pci.c:4317, 4326, 4370 | no | sì |
| `qcom,drv-supported` (nodo PCIe parent) | `cnss_pci_is_drv_supported` | pci.c:3072-3073 | **sì** (altrimenti non enumera) | sì |

Nodo **PCIe**: `pcie0_rp/cnss_pci` con `qcom,iommu-group`, `memory-region=<&cnss_wlan_mem>` (running_fdt.dts:16751-16759; `cnss_wlan_region` = `shared-dma-pool`, `reusable`, 32 MB, vendor_dtb.dts:364-372).

---

## 3. Firmware: quali file, per quale variante, da quale percorso

**FATTO (sorgente)**
- `m3.bin` — `pci.c:48`; richiesto in `cnss_pci_load_m3` con `firmware_request_nowarn` **con prefisso** (`pci.c:4207-4211`), copiato in `dma_alloc_coherent` (`pci.c:4217-4228`) e poi consegnato al FW via QMI (`qmi.c:728` `cnss_wlfw_m3_dnld_send_sync`).
- Immagine principale: **`amss20.bin`** (HW v2, `major_version == 2`) oppure **`amss.bin`** — `pci.c:49-50`, scelta in `cnss_pci_update_fw_name` `pci.c:5368-5387` (default: `amss.bin`); passata a MHI: `mhi_ctrl->fw_image = plat_priv->firmware_name` (`pci.c:5668`), fallback **senza prefisso** `mhi_ctrl->fw_image_fallback = plat_priv->fw_fallback_name` (`pci.c:5669`, `5365-5392`).
- BDF (board data) via QMI: nomi in `qmi.c:18-26`, costruiti in `cnss_get_bdf_file_name` (`qmi.c:515-589`):
  - ELF (default, `main.c:47`+`3054`): `bdwlan.elf` / `bdwlang.elf` (board_id 0xFF), `bdwlan.e%02x` / `bdwlang.e%02x` (board_id<0xFF), `bdwlan%02x.e%02x` (board_id multi-byte);
  - BIN: `bdwlan.bin` / `bdwlang.bin` / `bdwlan.b%02x` / `bdwlang.b%02x` / `bdwlan%02x.b%02x` (forzato a `CNSS_BDF_BIN` se il download ELF fallisce, `main.c:471-474`);
  - `regdb.bin` (`qmi.c:27`,573 → `request_firmware_direct`, `qmi.c:621-623`); HDS via `HDS_FILE_NAME` (`qmi.c:575`).
- QDSS cfg (solo `CONFIG_CNSS2_DEBUG`): `qmi.c:984-1011` (nome `_v2/v1`).
- Percorso: **prefisso `qca6490/`** se `use_fw_path_with_prefix` (`main.c:3194`, `pci.c:5296-5334`, `QCA6490_PATH_PREFIX "qca6490/"` `pci.c:45`). Sul device `qcom,same-dt-multi-dev` è presente ⇒ i file saranno richiesti come `qca6490/amss20.bin`, `qca6490/m3.bin`, `qca6490/bdwlan.eXX`, `qca6490/regdb.bin`. Se MHI segnala `MHI_CB_FW_FALLBACK_IMG`, il driver azzera il prefisso (`pci.c:5528-5529`) ⇒ servono **entrambe** le posizioni.
- Caricamento: `firmware_request_nowarn` per BDF ELF/BIN e M3 (qmi.c:625, pci.c:4210) → ricerca anche in userspace, timeout 60 s; `request_firmware_direct` per regdb/QDSS (wrapper `cnss_request_firmware_direct` `main.c:2555-2560`).
- **Non richiesti da cnss2**: `wpss*`, `mba.mbn`, `qwlan*` (solo per QCA6174, `main.c:54-57`), `phy_ucode.elf`/`wlfw_cal_db.bin` (presenti nel binario, non nel sorgente ⇒ altri moduli) — *IPOTESI*.

**Blob già presenti in locale** (`port-work/vboot-dtb-swap/stock_dump/firmware/all_firmware/fw_image/`):
`amss20.bin` (5.300.224 B), `amss.bin` (5.478.416 B), `m3.bin`, `regdb.bin`, `Data.msc`, e **192 file `bdwlan.*`** inclusi `bdwlan.e01 … bdwlan.e27+` (variante ELF per board id, coerente con `ELF_BDF_FILE_NAME_PREFIX "bdwlan.e"`); più `qca6490/Data20.msc`. Da verificare quale `bdwlan.eXX` corrisponda al board id di questo esemplare (il nome dipende da `board_info.board_id` letto dal FW via QMI).

---

## 4. Come il FW arriva nel chip (flusso esatto)

**Canale A — immagine base via PCIe + MHI (BHI/BHIE), NON via QMI**
1. `cnss_pci_register_mhi` (`pci.c:5642-5734`): alloca il controller MHI, imposta `regs = pci_priv->bar` e `len = pci_resource_len(BAR)` (`pci.c:5671-5675`), `dev_id = pci_priv->device_id` (`5663`), `fw_image`/`fw_image_fallback` (`5668-5669`), `sbl_size = SZ_512K`, `seg_len = SZ_512K`, `fbc_download = true`, `rddm_supported = true` (`5702-5709`), poi `of_register_mhi_controller()` (`5713`).
2. Il core MHI scarica: `request_firmware(fw_name)` (`mhi_boot.c:661`), copia i primi `sbl_size` byte in RAM DMA-coerente e li spinge via **BHI** (`mhi_boot.c:680-700`, `mhi_fw_load_bhi`); per `fbc_download` alloca la **BHIE vector table** e copia l'intera immagine (`mhi_boot.c:716-728`), attende l'EE `SBL` (`750-758`) e infine `mhi_fw_load_bhie()` (`762-764`): l'immagine passa **via PCIe dalla RAM host al chip** (trasferimento DMA dal lato device, non PIO). Nessun passaggio QMI.
3. Solo dopo, la sequenza QMI di boot-handshake (sotto).

**Canale B — QMI/WLFW (BDF, REGDB, M3, QDSS, WLAN cfg/mode)**
- Servizio **WLFW 0x45** (`wlan_firmware_service_v01.h:9-10`), bind in `cnss_qmi_init`: `qmi_handle_init` + `qmi_add_lookup(..., WLFW_SERVICE_ID_V01, vers 1, inst 1)` (`qmi.c:2939-2949`). Servizi ausiliari: **DMS 0x02** (`qmi.c:3084-3092`), **COEX 0x22** (`coexistence_service_v01.h:7`, `qmi.c:3286-3292`), **IMSPRIVATE 0x4D** (`ip_multimedia_subsystem_private_service_v01.h:9`, `qmi.c:3487-3493`).
- `respond_mem` per pubblicare i segmenti di memoria al FW (`qmi.c:300-340`); BDF a blocchi da `QMI_WLFW_MAX_DATA_SIZE_V01 = 6144` B (`wlan_firmware_service_v01.h:115`, loop `qmi.c:644-702`); QDSS (`qmi.c:1060-1115`).
- Dopo il BDF: se il FW non annuncia XPA, abilita il regolatore interno (`cnss_enable_int_pow_amp_vreg`, `qmi.c:706-710`).
- WLAN cfg/mode (quello che "accende" la radio) viene inviato **dal driver host** via `cnss_wlan_enable` → `cnss_wlfw_wlan_cfg_send_sync` + `cnss_wlfw_wlan_mode_send_sync` (`main.c:300-340`).

**Trasporto QMI (FATTO, verificato sui simboli)**: `cnss2.ko` **non** ha simboli `qrtr_*`; usa `qmi_handle_init/qmi_add_lookup/qmi_send_request` (modulo `qmi_helpers`), che parla **QRTR** (`drivers/soc/qcom/qmi_interface.c:8` `#include <linux/qrtr.h>`, `:134-157` gestione `qrtr_ctrl_pkt`). Il trasporto QRTR verso il WLAN è `qrtr-mhi` sul canale MHI **"IPCR"** (`net/qrtr/mhi.c:196-199`). Quindi: **QMI → qmi_helpers → QRTR → qrtr-mhi → MHI (IPCR) → FW**. Nessun uso di glink/smd nella catena WLAN (`qrtr-smd.ko` è per altri sottosistemi).

**Canale C — ramdump/RDDM (solo crash)**: `mhi_download_rddm_image`, `mhi_force_rddm_mode`, `mhi_scan_rddm_cookie`, `mhi_dump_sfr`, `mhi_debug_reg_dump` + `qcom_ramdump`/`memory_dump_v2`/`minidump` (`main.c:1196+` subsys ramdump, `main.c:2240-2360`). Non partecipa al bring-up normale.

---

## 5. Dipendenze: moduli e sottosistemi

**FATTO — `modinfo stock-modules/cnss2.ko`**
```
depends: pci-msm-drv,qmi_helpers,wlan_firmware_service,cmd-db,mhi,qcom_ipc_logging,
         cnss_plat_ipc_qmi_svc,memory_dump_v2,minidump,qcom_ramdump
```
(308 simboli non risolti in totale). Raggruppati:
- **PCIe**: `msm_pcie_enumerate`, `msm_pcie_pm_control`, `msm_pcie_register_event`, `msm_pcie_deregister_event`, `msm_pcie_prevent_l1`, `msm_pcie_allow_l1`, `msm_pcie_set_link_bandwidth`, `msm_pcie_set_target_link_speed` → modulo `pci-msm-drv.ko`;
- **MHI**: 25 simboli (`mhi_alloc_controller`, `mhi_register_controller`, `mhi_prepare_for_power_up`, `mhi_sync_power_up`, `mhi_power_down`, `mhi_download_rddm_image`, `mhi_get_exec_env`, `mhi_set_m2_timeout_ms`, `mhi_controller_set_bw_scale_cb`, `mhi_dump_sfr`, `mhi_force_rddm_mode`, `mhi_scan_rddm_cookie`, …) → `mhi.ko`;
- **QMI/WLFW**: `qmi_handle_init`, `qmi_add_lookup`, `qmi_send_request`, `qmi_txn_*` → `qmi_helpers.ko`; 57 simboli `wlfw_*_msg_v01_ei` → `wlan_firmware_service.ko`;
- **regolatori/pinctrl/gpio**: `regulator_enable/disable/set_voltage/set_load`, `pinctrl_lookup_state/select_state`, `gpio_request/free/direction_*/gpio_to_desc/gpiod_*` → sottosistemi kernel;
- **interconnect**: `icc_set_bw`, `icc_put`, `of_icc_get` → ICC core (`CONFIG_INTERCONNECT`);
- **CommandDB/TCS**: `cmd_db_ready`, `cmd_db_read_addr` → `cmd-db.ko`;
- **dump/log**: `dump_enabled` (memory_dump_v2), `ipc_log_context_*`/`ipc_log_string` (qcom_ipc_logging), `qcom_ramdump`/`minidump` via `depends`.
- **Assenti** (verificato): `qrtr_*`, `smem_*`, `ipa_*`, `rproc_*`, `subsys_*`, e **zero** `cfg80211`/`wiphy`/`ieee80211`.

**Ordine di caricamento** (`stock-modules/modules.load`): `mhi_cntrl_qcom(6) mhi(7) … pci-msm-drv(17) … cnss2(101) cnss_utils(102) cnss_plat_ipc_qmi_svc(104) cnss_nl(105) cnss_prealloc(106) … qrtr-smd(228) qrtr-mhi(229) qrtr-gunyah(230)`.
**softdep** (`stock-modules/modules.softdep`): `softdep qmi_helpers pre: qrtr` ⇒ `qrtr.ko` prima di `qmi_helpers`.
**Chi usa cnss2** (`modules.dep`): `/vendor/lib/modules/qca_cld3_qca6490.ko: … cnss2.ko …` ⇒ il driver host (qcacld) dipende da cnss2 (e registra la wiphy: cnss2 **non** lo fa).

**Nota MHI/DT**: il core MHI di *questa tree* esigerebbe `mhi,max-channels` + child `mhi_channels` (`mhi_init.c:1338-1347`) e leggerebbe `mhi,timeout`/`mhi,ee`/… dal nodo passato in `mhi_ctrl->of_node` (`pci.c:5662` → nodo WLAN). **Ma** né il FDT del device né `vendor_dtb.dts` contengono `mhi_channels`/`mhi,max-channels`/`mhi,devices`, e il `mhi.ko` reale **non contiene** quelle stringhe ⇒ il MHI 5.10 reale usa i default interni (`mhi,timeout`→`MHI_TIMEOUT_MS`, `mhi,buffer-len`→`MHI_MAX_MTU`, `mhi_init.c:1523-1532`). Conferma ulteriore che la tree 5.4 non è la verità per MHI.

---

## 6. Interazione con userspace: cosa è obbligatorio e cosa no

**FATTO**
- **genl (in cnss2)**: famiglia `"cnss-genl"`, gruppo multicast `"cnss-genl-grp"`, versione 1 (`genl.c:15-17`). Un solo comando, `CNSS_GENL_CMD_MSG`, il cui handler è un **no-op** che ritorna 0 (`genl.c:55-58`), e la famiglia è registrata in fase di probe (`main.c:3268`). I messaggi sono **solo in uscita** (`genlmsg_multicast`, `genl.c:145`) per scaricare i log QDSS (`main.c:1762`, `qmi.c:957`). ⇒ **nessun comando inbound, nessun "wait for daemon"**.
- **cnss_nl.ko**: famiglia genetlink **`cld80211`** (usata dal WLAN HAL/wificond Android), `depends:` vuoto → **opzionale** per il bring-up.
- **sysfs/debugfs**: link `/sys/…/cnss` e `shutdown_wlan` (`main.c:2898, 2906`); attributi WO `fs_ready`, `shutdown`, `recovery`, `enable_hds`, `qdss_trace_start/stop`, `qdss_conf_download`, `hw_trace_override` (`main.c:2868-2875`); debugfs `/sys/kernel/debug/cnss/{dev_boot,reg_read,reg_write,runtime_pm,control_params}` (`debug.c:839-847`) → superficie Android opzionale.
- **Il bring-up NON è netlink-triggered**: è innescato **dal driver host** via API esportate (`nm`): `cnss_wlan_register_driver`, `cnss_wlan_enable`/`_disable`, `cnss_power_up`/`_down`, `cnss_pci_dev_powerup`, `cnss_bus_dev_powerup`, `cnss_get_plat_priv`, `cnss_get_platform_cap`, `cnss_qmi_send(_get/_put)`. `cnss_power_up` posta l'evento `CNSS_DRIVER_EVENT_POWER_UP` e attende `power_up_complete` con timeout (`main.c:796-835`, `673-723`).
- `cnss_probe` (platform, `main.c:3244-3258`) fa **power-on + bus init** già al probe del nodo DT; dopo il probe PCI il driver **rispegne** (`cnss_suspend_pci_link` + `cnss_power_off_device`, `pci.c:6090-6097`) e la vera accensione avviene quando il host driver chiama `cnss_power_up()`.
- Quirk `ENABLE_DAEMON_SUPPORT` esiste solo se il DT ha `cnss-daemon-support` (`main.c:3044-3045`) ed è **consumato dal driver host** via `ctrl_params`, non da cnss2. Sul device la prop non c'è.

**Risposta secca**: probe + power-on + FW download **non richiedono** netlink né alcun daemon. Serve però il **driver host** (`qca_cld3`) per: inviare `wlan_enable` (cfg/mode QMI), registrare la wiphy e creare l'interfaccia.

---

## 7. PCIe: chi accende il chip, `qcom,wlan-rc-num`, prerequisiti

**FATTO — sequenza di power-on** (`cnss_power_on_device`, `power.c:979-1061`)
1. se bus PCI: `cnss_bus_dsp_link_control(false)` (`990-994`);
2. **regolatori** `cnss_vreg_on_type(CNSS_VREG_PRIM)` (`996`) → per ognuno `devm_regulator_get_optional` (`87`) + `regulator_set_voltage/set_load/enable` con i valori di `qcom,<nome>-config` (`103-116`, `136-188`);
3. **clock** `cnss_clk_on` (`1002`);
4. se `reset`: stato pinctrl **sleep** e attesa 4-5 ms (`1015-1022`);
5. `cnss_select_pinctrl_enable` (`1025`): se `qcom,bt-en-gpio` è *high* salta WLAN_EN; se *low* attende **100 ms** per la sequenza PMU QCA6490/QCA6390 (`938-955`);
6. `cnss_select_pinctrl_state(true)` (`848-892`): (eventuale) `bootstrap_active` → **pulse `qcom,xo-clk-gpio`** (true poi false, `874`,`892`) → `wlan_en_active` → attesa **10-11 ms** prima del de-assert di PCIe reset;
7. `cnss_bus_init` → `cnss_pci_init` → `qcom,wlan-rc-num` → enumerate.
Power-off: `pinctrl sleep` → clock off → regolatori off (`1063-1079`).

**Regolatori**: nomi dalla lista statica (`power.c:19-35`) — `vdd-wlan`, `vdd-wlan-aon`, `vdd-wlan-dig`, `vdd-wlan-io`, `vdd-wlan-rfa1/2`, `wlan-ant-switch`, `vdd-wlan-xtal(-aon)`, `vdd-wlan-ctrl1/2`, `vdd-wlan-en`, `wlan-soc-swreg`, `vdd-wlan-core`: quelli non presenti nel DT tornano `-ENODEV` e vengono saltati (`90-91`), quindi il DT del device (5 supply) è sufficiente.

**`qcom,wlan-rc-num`** (FATTO): letto come **array** con `of_get_property` (`pci.c:6249`), obbligatorio (`6250-6253`); per ogni valore `cnss_pci_enumerate(rc)` (`6257`) → per QCA6490 imposta il link speed (default **Gen2**, `6193-6216`) → `_cnss_pci_enumerate` (`6221`) con retry sul link training (`6229-6231`) e `-EPROBE_DEFER` se la RC non è pronta (`6223-6225`). Il valore salvato è `plat_priv->rc_num` (`6237`). Sul device `qcom,wlan-rc-num = <0>` → **RC0 = `qcom,pcie@1c00000`**.

**Cosa deve essere pronto a livello PCIe**
- driver **`pci-msm-drv.ko`** (tutti i simboli `msm_pcie_*`), con il nodo RC0 che ha **`qcom,drv-supported`** (verificato da `cnss_pci_is_drv_supported`, `pci.c:3072-3073`; presente nel FDT);
- PHY/clock/reset della RC (dal DTB: `qcom,phy-sequence`, `max-clock-frequency-hz`, `resets`) — forniti da pci-msm-drv;
- **SMMU/IOMMU** con `qcom,iommu-group` + `fastmap` + pool `0xa0000000,0x10000000` (`pci.c:4306-4371`, running_fdt.dts:16751-16759);
- **riserva di memoria** `memory-region = <&cnss_wlan_mem>` (`cnss_wlan_region`, reusable 32 MB) → `of_reserved_mem_device_init` (`pci.c:6027`);
- **MSI** (`MHI`, `WAKE`) — `cnss_pci_get_mhi_msi` (`pci.c:5677`), `cnss_pci_enable_msi` (`6051`); BAR0 richiesto (`pci_request_region(..., "cnss")`, `pci.c:4784`);
- **pinctrl/GPIO** (tlmm) con gli stati `cnss_wlan_en_active/sleep` (pin gpio80 nel DTB), `qcom,xo-clk-gpio` (gpio 0xcc), `qcom,bt-en-gpio` (0x51), `qcom,sw-ctrl-gpio` (0x52), `wlan-en-gpio` (0x50);
- **interconnect/ICC** + `interconnect-names` (2 path nel DTB);
- **cmd-db/TCS** per CPR e `qcom,vreg_ipa` (s3e) — `power.c:1126-1200`, `1261-1294` (+ nel binario reale `qcom,vreg_ipa`/`qcom,vreg_ol_cpr`, assenti nel sorgente).

---

## 8. `soc:mhi_qrtr_cnss` — cos'è e chi lo binda

**FATTO**
- Nodo (`running_fdt.dts:13124-13129`; `vendor_dtb.dts:9602-9607`):
  `mhi_qrtr_cnss { compatible = "qcom,qrtr-mhi"; qcom,dev-id = <0x1103>; qcom,net-id = <0x00>; qcom,low-latency; };`
- Driver: **`net/qrtr/mhi.c`** = modulo `qrtr-mhi.ko`; è un **`mhi_driver`** con `id_table = { .chan = "IPCR" }` (`mhi.c:196-206`), registrato su bus MHI (`214-215`). In `probe` legge `qcom,net-id` e `qcom,low-latency` **dal `mhi_dev->dev.of_node`** (`169-174`) e registra l'endpoint QRTR (`179`, `qrtr_endpoint_register`). Stringhe confermate nel binario reale: `IPCR`, `qcom,net-id`, `qcom,low-latency`.
- Quindi **è il trasporto QRTR (QMI-over-MHI) del WLAN**: viene bindato dal bus **MHI** quando il core MHI crea i device di canale (`mhi_create_devices`, `mhi_main.c:821`; nome device `%04x_%02u.%02u.%02u` con `dev_id`, `mhi_init.c:1687`) dopo `of_register_mhi_controller` chiamato da `cnss_pci_register_mhi` (`pci.c:5713`). `qcom,dev-id = 0x1103` = PCI device ID del QCA6490/WCN6855 (`plat_priv->device_id`, `pci.c:5663`).
- Chi associa il nodo al device MHI: `mhi_assign_of_node` (`mhi_main.c:798-818`) cerca un child **`mhi_devices`** con prop `mhi,chan`; quel meccanismo **non** è presente nel DT del device (grep: 0 occorrenze di `mhi_devices`/`mhi,chan`), e **nessun** file di questa tree legge `qcom,dev-id`.
  ⇒ *IPOTESI*: nel kernel 5.10 reale l'aggancio avviene per `dev-id` (matching nodo↔controller MHI); da confermare con `strings mhi.ko`/`strings cnss2.ko` sul kernel vero (i nomi `qcom,dev-id` non compaiono in questa tree). Effetto pratico se l'aggancio fallisse: `qcom,net-id`/`qcom,low-latency` non verrebbero letti e varrebbero i default (`net_id = QRTR_EP_NET_ID_AUTO`, `low-latency = false`) — probabilmente non fatale per il QMI locale, ma da tenere presente.

---

## 9. Cosa può mancare senza Android (sintesi operativa)

1. **Catena di moduli completa e con vermagic allineato** al kernel in esecuzione: `mhi.ko`, `qmi_helpers.ko`, `qrtr.ko` (+`qrtr-mhi.ko`), `wlan_firmware_service.ko`, `cmd-db.ko`, `pci-msm-drv.ko`, `cnss2.ko`, `cnss_utils.ko`, `cnss_plat_ipc_qmi_svc.ko`, `cnss_prealloc.ko`, `memory_dump_v2.ko`, `minidump.ko`, `qcom_ramdump.ko`, (`qcom_ipc_logging`).
2. **Blob firmware** in `/lib/firmware` (e/o `/lib/firmware/qca6490/`): `amss20.bin` (HW v2) o `amss.bin`, `m3.bin`, `bdwlan.eXX` (o `bdwlan.bin`/`bdwlan.elf`), `regdb.bin`. Disponibili in locale (vedi §3).
3. **Driver host `qca_cld3_qca6490.ko`**: senza di esso **non esiste wiphy né interfaccia**; è lui a chiamare `cnss_power_up`/`cnss_wlan_enable` e a inoltrare il `wlan_mode`. Dipende da cnss2 (`modules.dep`).
4. **DT/PCIe**: RC0 (`qcom,pcie@1c00000`) con `qcom,drv-supported`, driver `pci-msm-drv`, SMMU/`qcom,iommu-group`, reserved-mem `cnss_wlan_mem`, pinctrl `cnss_wlan_en_active/sleep`, GPIO (wlan-en/xo-clk/bt-en/sw-ctrl), regolatori rpmh, ICC, cmd-db/TCS + vreg IPA (`s3e`).
5. **Attenzione al `status` del nodo wlan**: nel FDT di esecuzione il nodo è **enabled** (nessuna prop `status`, `running_fdt.dts:12972`), mentre in `kernel-patch-test/vendor_dtb.dts:9807` è `status = "disabled"` → se il DTB attualmente flashato è quello, **il platform device non nasce e il driver non fa probe**.
6. **Niente da Android strettamente necessario**: nessun daemon, nessun comando netlink in ingresso; `cnss_nl.ko`/`cld80211`, sysfs WO e debugfs sono opzionali (usati dall'HAL Android).
7. Il QMI verso il FW passa da **QRTR-MHI (canale IPCR)**: `qrtr-mhi.ko` + nodo `mhi_qrtr_cnss` devono esserci, altrimenti la sequenza QMI (respond_mem/BDF/M3/mode) non può partire.

---

## FATTO (con file:riga)

- Tree ≠ kernel in uso: `stock-kernel-source/Makefile:2-4` = 5.4.242; marker core <5.6/5.8 (no `mmap_lock` in `include/linux/mm_types.h`, no `net/mptcp/`, no `kernel/watch_queue.c`, no `include/linux/find.h`, no `drivers/misc/uacce`); DTS `lahaina*` (SM8350). Driver 5.4-style: nessun `sysfs_emit`/`dev_err_probe`/`proc_ops` in `cnss2/*.c`.
- Binari reali: `vermagic 5.10.66-gki-g491fe99db339`; `depends: pci-msm-drv,qmi_helpers,wlan_firmware_service,cmd-db,mhi,qcom_ipc_logging,cnss_plat_ipc_qmi_svc,memory_dump_v2,minidump,qcom_ramdump`; copie identiche (md5 `1eba985e80c4…`).
- Etichette DT: match `qcom,cnss-qca6490` (`main.c:3101-3122`), platform id `qca6490` (`main.c:3095`); nodo device `vendor_dtb.dts:9806`, `running_fdt.dts:12972`.
- Proprietà DT obbligatorie di fatto: `qcom,wlan-rc-num` (`pci.c:6249-6253`), `qcom,drv-supported` sulla RC (`pci.c:3072-3073`), `wlan-en-gpio`+stati pinctrl `wlan_en_active/sleep` (`power.c:58-59, 754-773`).
- Firmware richiesti: `m3.bin` (`pci.c:48,4207-4211`), `amss20.bin`/`amss.bin` (`pci.c:49-50,5368-5387`, a MHI `pci.c:5668`), `bdwlan.elf|bdwlan.e%02x|bdwlan.bin|bdwlan.b%02x` (`qmi.c:18-26,515-589`), `regdb.bin` (`qmi.c:27,573,621-623`); prefisso `qca6490/` (`pci.c:45,5296-5334`; attivo per `qcom,same-dt-multi-dev`, `main.c:3083-3089`).
- FW immagine → chip via **PCIe/BHI+BHIE** (`pci.c:5668-5709,5713`; `mhi_boot.c:661-700,716-728,750-764`); BDF/M3/QDSS → **QMI WLFW 0x45** (`qmi.c:2939-2949`, chunk 6144 B `wlan_firmware_service_v01.h:115`).
- Trasporto QMI = QRTR via `qmi_helpers` (`qmi_interface.c:8,134-157`) + `qrtr-mhi` canale **IPCR** (`net/qrtr/mhi.c:196-199`); nessun simbolo `qrtr_*` in cnss2.ko.
- Nessuna wiphy in cnss2: 0 simboli `cfg80211/wiphy/ieee80211`; l'host driver `qca_cld3_qca6490.ko` dipende da `cnss2.ko` (`modules.dep`).
- genl di cnss2: solo invio, handler no-op (`genl.c:15-17,55-58,145`); `cnss_nl.ko` = famiglia `cld80211` (opzionale).
- Power-on: `power.c:979-1061` (vreg→clk→pinctrl sleep+4-5 ms→bt_en 100 ms→xo-clk pulse→wlan_en+10 ms); regolatori `devm_regulator_get_optional` (`power.c:87`) con config da `qcom,<nome>-config` (`power.c:103-116`).
- `mhi_qrtr_cnss` = `qcom,qrtr-mhi`, `dev-id 0x1103`, `net-id 0`, `low-latency` (`running_fdt.dts:13124-13129`); bindato dal bus MHI sul canale `IPCR`.
- Blob disponibili in locale: `.../fw_image/{amss20.bin, amss.bin, m3.bin, regdb.bin, 192×bdwlan.*}`.

## IPOTESI

- L'aggancio del nodo `mhi_qrtr_cnss` al controller MHI avviene per `qcom,dev-id` nel kernel 5.10 reale (in questa tree nessun codice legge `qcom,dev-id`; `mhi_assign_of_node` cerca `mhi_devices`/`mhi,chan` assenti nel DT) → da confermare con `strings mhi.ko`/`dmesg` sul device.
- Il kernel 5.10 reale usa i **default** per `mhi,timeout`/`mhi,buffer-len` e **non** richiede `mhi_channels`/`mhi,max-channels` (assenti nel DTB e assenti come stringhe in `mhi.ko`), a differenza del core di questa tree (`mhi_init.c:1338-1347`).
- `phy_ucode.elf` e `wlfw_cal_db.bin` (stringhe presenti in `cnss2.ko`, non nel sorgente) provengono da altri moduli e non sono necessari al bring-up QCA6490.
- `qcom,vreg_ipa`/`qcom,vreg_ol_cpr` (nel binario, non nel sorgente) servono per la configurazione IPA/S3E → il supporto IPA (`ipa_fmwk.ko`) è presente nei moduli stock; il ruolo esatto nel bring-up WLAN non è determinabile da qui.
- I 192 `bdwlan.eXX` disponibili coprono tutti i board id; il file effettivamente richiesto dipende dal `board_id` letto dal FW via QMI (`qmi.c:522-546`) → per questo device sarà presumibilmente uno tra `bdwlan.eXX` (o `qca6490/bdwlan.eXX`).

## NON RISOLTO

- **Mismatch di versione kernel**: kernel dichiarato `5.10.66-android12-9-00005-gf6e6376090be-ab8060604` vs `vermagic` dei moduli `5.10.66-gki-g491fe99db339`. Se il mismatch è reale, **nessun** modulo della catena si carica (da verificare con `uname -r` + `modprobe cnss2` sul device; non eseguibile da qui).
- **Quale DTB è effettivamente in uso**: `status` del nodo wlan diverso tra `vendor_dtb.dts:9807` (`disabled`) e `running_fdt.dts:12972` (enabled). Da chiarire quale file alimenta il boot attuale.
- Non ho potuto verificare sul device: `dmesg` del probe cnss2/MHI/QRTR, l'esito reale di `qmi_add_lookup` per WLFW 0x45, e se `mhi_qrtr_cnss` venga effettivamente bindato (`ls /sys/bus/mhi/devices`, `/sys/bus/mhi/drivers/qcom_mhi_qrtr/`).
- Revisione esatta del sorgente reale di cnss2 (solo 78% di overlap stringhe); i nomi/righe citati valgono per la tree 5.4, i nomi-stringa verificati sui binari sono elencati in §1.
- `amss20.bin` nel dump ha 5.300.224 B: se la partizione firmware del device sia la fonte corretta per `/lib/firmware` (piuttosto che `/vendor/firmware` di Android) non è determinabile da qui.
