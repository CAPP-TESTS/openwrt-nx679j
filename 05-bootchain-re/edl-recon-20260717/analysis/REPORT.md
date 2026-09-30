# EDL recon + fixes (2026-07-17)

## 1) ABL A vs B
- Both ELF, same size 1 MiB
- ~150816 differing bytes, 568 ranges (mostly sparse mid-image)
- **No unique printable strings** A-only or B-only → same ABL build with different signature/hash blobs, not two different products
- XBL A vs B: only ~1965 bytes differ (similar story)

## 2) Boot header unpack (v4)
| | boot_a (pre-write stock) | boot_b (on flash) | magisk local |
|--|--|--|--|
| format | ANDROID! v4 | ANDROID! v4 | ANDROID! v4 |
| kernel | 49.1 MB stock 5.10.66 | 38.8 MB 6.12.0 dirty | same kernel as stock a |
| ramdisk | ~1.38 MB | ~4.93 MB (OpenWrt) | ~2.20 MB Magisk |
| signature_size | 4096 | **0** | 4096 |
| cmdline | empty in header | ttyMSM0 + debug + g_ncm | empty |
| OpenWrt | no | yes | no |
| Magisk | no | no | yes |

Note: boot_b has **no boot signature** (signature_size=0) while stock/Magisk have 4K sig. If ABL enforces signed boot images, that alone can force fail-to-fastboot on B.

## 3) Magisk restored to boot_a
- Wrote recovery-v311-magisk-20260711/boot_a-magisk-30700.img
- Readback SHA256 match: OK

## 4) misc BCB cleared
- Was: `bootonce-bootloader` at offset 0
- Cleared first 2 KiB only; preserved 0x8000 residual bytes
- Verify: bootonce gone
