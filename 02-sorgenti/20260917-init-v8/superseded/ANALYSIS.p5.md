
PART 4 - MEASURED: WHICH MODULES CAN DO IT (not guessed - disassembled)

Tool: module-risk-analysis.py, run over the 327 .ko of the device's own vendor
ramdisk (port-work/stock_vendor_ramdisk/lib/modules). It builds the exact load
order v6's emit() produces, then walks each module's call graph from
init_module and from every probe/notify function using objdump -dr, and reports
the external calls reached. Output: module-risk.json, module-risk-sweep.log.

    modules in v6's chain                                 327
    have an unbounded wait reachable from init/probe       75
    reference a peer-subsystem rendezvous symbol           70
    of the 75, in modules.load (the 98 working set)        20
    of the 75, recovery-only (never loaded in PID 1)       55

Unbounded waits reached, by symbol:
    wait_for_completion            qcom_scm (qcom_scm_call path), gh_rm_drv
                                   (gh_rm_call), cnss2 (also _interruptible
                                   and _killable), cdsprm
    mutex_lock / down_write        61 modules, including ufs_qcom, arm_smmu,
                                   pmic_glink, ucsi_glink, altmode_glink,
                                   pdr_interface, qmi_helpers, atmel_mxt_ts
    cancel_work_sync/flush_workqueue/destroy_workqueue   cfg80211, mac80211,
                                   msm_drm, dwc3_msm, mem_buf, qmi_helpers,
                                   pmic_glink, altmode_glink, cnss2, icnss2,
                                   cdsprm, mhi_dev_net
    request_firmware               msm_drm

The modules named from the device read the day before, all recovery-only
(not in modules.load): cnss2 (wait_for_completion, mhi_register_controller),
icnss2 (qmi_txn_wait), cdsp-loader, cdsprm (wait_for_completion, rpmsg_send),
adsp_sleepmon, mhi_cntrl_qcom (mhi_arch_*), mhi_dev_drv, mhi_dev_net,
atmel_mxt_ts (mutex_lock), aw9620x, fsa4480_i2c, qcom_q6v5_pas (rproc_*).

Also worth knowing: 20 of the 98 modules.load members are in the 75. The 98
are safer by precedent (that is the set the working first stage loads on this
device), not because they are risk free. That is why v8 keeps a timeout even
for them.
