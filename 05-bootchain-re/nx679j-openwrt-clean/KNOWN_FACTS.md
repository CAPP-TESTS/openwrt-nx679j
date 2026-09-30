# NX679J OpenWrt clean workspace

Created from direct Android/ADB root reads on the connected device. The previous project tree is not a source of truth and is not copied here.

## Verified identity

- `ro.product.model`: `NX679J`
- `ro.product.device`: `NX679J-UN`
- manufacturer: `nubia`
- SoC: `SM8450`
- runtime kernel: `5.10.66-android12-9-00005-gf6e6376090be-ab8060604`
- current slot: `_a`
- verified boot state: `orange`
- device state: `unlocked`
- runtime FDT size: `748187` bytes
- runtime DT identity: `Qualcomm Technologies, Inc. Waipio MTP with PM8010`, compatible `qcom,waipio-mtp`, `qcom,waipio`, `qcom,mtp`
- `ro.boot.dtb_idx=5`, `ro.boot.dtbo_idx=35`
- real UDC: `a600000.dwc3`, observed state `configured`
- display: DRM `/dev/dri/card0`, connector `card0-DSI-1`; no legacy `/dev/fb0` observed

## Direct live readback hashes

See `bootchain/manifest.json`. All 12 requested partitions matched expected byte lengths.

- ABL A/B: `443d1956e836aea3fe30ac13113dae787d88223c1e24f875c6831f69d86acea3`
- UEFI A/B: `d32139de305aeb2aba1dc8c12b4996c49b09a09a6032307bf100ecb1ecb2d312`
- boot A: `0cd94d5627501f3c65c94542093ad18b1ec6c1c215dc6a7ecab4f941057e8364`
- vendor_boot A/B: `6db6d4d76059a9ef3a6bcc904911aac53a20044224a675014a6396a8f5d9d168`
- dtbo A/B: `9ee95463abce517d140eae3c5c892c2b38fd1344d6fb0a6e2da2af76a86de6a1`
- vbmeta A/B: `cc0de687c935714a9b6de4bf3492391474e640d2e23f752f6c626f6413b07e22`

`boot_b` is intentionally recorded as a modified/experimental image and is not known-good.

## Live ABL/UEFI facts

- ABL A/B are byte-identical and are direct live reads.
- UEFI A/B are byte-identical and are direct live reads.
- ABL uses Qualcomm hash segment v7, SHA-384 hash table, ECDSA P-384 OEM signature and a root certificate named `Generated Ztemt Root CA`.
- Live UEFI contains `DefaultBDSBootApp = "LinuxLoader"`, `QC_IMAGE_VERSION_STRING=BOOT.MXF.2.0-00608-WAIPIO-1.465557.1`, and verified memory-map strings including UEFI FD `0xA7000000` size `0x00400000` and kernel `0xA8000000`.

## Not yet verified

- Any custom UEFI/ABL build boots on this phone.
- Which storage path and file format `LinuxLoader` expects for a boot target.
- Whether XBL would accept a locally test-signed replacement ABL.
- Whether the existing LinuxLoader can be configured through `uefivarstore` without changing protected firmware.
- Whether a kernel with DRM MSM and the real Waipio DT reaches a visible panel.

## Rules

- Use hashes and direct readbacks, not historical filenames.
- Do not flash ABL, UEFI, XBL, GPT or `uefivarstore` during investigation.
- Keep all new test images outside this clean evidence directory.
- Any physical test requires a new timestamped experiment directory, source hashes, target partition, readback, and recovery plan.
