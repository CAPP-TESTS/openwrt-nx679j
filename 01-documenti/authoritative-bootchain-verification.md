# NX679J — authoritative boot-chain verification

Date: 2026-09-16

## Source

Values below come from direct ADB root reads while Android was running on the phone. The live ABL/UEFI readback is in `../authoritative-bootchain-20260916-235306/` and the live runtime verification is in `../authoritative-android-verification-20260916-234837/`.

## Live identity

- Model: `NX679J`
- Product/device: `NX679J-UN`
- Manufacturer: `nubia`
- SoC: `SM8450`
- Slot: `_a`
- Boot state: `orange`, device state `unlocked`
- Kernel: `5.10.66-android12-9-00005-gf6e6376090be-ab8060604`
- USB: `05c6:908c Qualcomm WAIPIO-MTP`

## Live hashes

| Partition | SHA-256 | Status |
|---|---|---|
| `abl_a` | `443d1956e836aea3fe30ac13113dae787d88223c1e24f875c6831f69d86acea3` | direct read, valid size 1 MiB |
| `abl_b` | `443d1956e836aea3fe30ac13113dae787d88223c1e24f875c6831f69d86acea3` | direct read, valid size 1 MiB |
| `uefi_a` | `d32139de305aeb2aba1dc8c12b4996c49b09a09a6032307bf100ecb1ecb2d312` | direct read, valid size 5 MiB |
| `uefi_b` | `d32139de305aeb2aba1dc8c12b4996c49b09a09a6032307bf100ecb1ecb2d312` | direct read, valid size 5 MiB |
| `boot_a` | `0cd94d5627501f3c65c94542093ad18b1ec6c1c215dc6a7ecab4f941057e8364` | Magisk image |
| `vendor_boot_a/b` | `6db6d4d76059a9ef3a6bcc904911aac53a20044224a675014a6396a8f5d9d168` | identical |
| `dtbo_a/b` | `9ee95463abce517d140eae3c5c892c2b38fd1344d6fb0a6e2da2af76a86de6a1` | identical |
| `vbmeta_a/b` | `cc0de687c935714a9b6de4bf3492391474e640d2e23f752f6c626f6413b07e22` | identical |

## Provenance correction

The current live ABL hash `443d1956...` matches these local files:

- `edl-recovery/recon-20260717-183330/abl_b.bin`
- `edl-recovery/restore-stock-a/abl_a_from_b.bin`
- `bootloader-re/abl_a.img`
- `bootloader-re/extractions/abl_a.img`

It does **not** match the historical files named `abl_a.bin` in the July recon or `native-boot-baseline`; those have hash `913e7242...`.

Therefore filenames in the old artifact set are not reliable slot labels. The hash and direct live read are authoritative. The device currently boots Android with `443d1956...` in both ABL slots.

## Live UEFI facts

`uefi_a` and `uefi_b` are identical. Strings in the live UEFI include:

- `QC_IMAGE_VERSION_STRING=BOOT.MXF.2.0-00608-WAIPIO-1.465557.1`
- `1.2.8.f-QC-UEFI`
- `DefaultBDSBootApp = "LinuxLoader"`
- `PlatConfigFileName = "uefiplatLA.cfg"`
- UEFI FD: `0xA7000000`, size `0x00400000`
- UEFI stack: `0xA760D000`, size `0x00040000`
- Kernel region: `0xA8000000`, size `0x10000000`
- Display thread and display image FV enabled
- Boot Device UFS support

The live UEFI contains the firmware's own `uefiplat.cfg` strings and the NX679J memory map. This is stronger evidence than the unverified July UEFI build artifact.

## Conclusions

1. NX679J/SM8450/Waipio is confirmed directly.
2. ABL A/B and UEFI A/B are currently symmetric and Android boots.
3. The old ABL labels were mixed; use hashes, not filenames.
4. A custom ABL must preserve Qualcomm's signing chain. The live ABL contains an OEM ECDSA P-384 chain with root `Generated Ztemt Root CA`; its private key is not present.
5. The live UEFI already has a `LinuxLoader` boot target and verified NX679J memory-map strings. The next port should modify/test the LinuxLoader/UEFI path while preserving ABL, not assume the old `nubia-nx679j.img` is valid.
6. No write was performed during this verification.
