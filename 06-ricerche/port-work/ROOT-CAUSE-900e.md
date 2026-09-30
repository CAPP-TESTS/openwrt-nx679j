# NX679J UEFI 900e — Root Cause Analysis (no more blind flashing)

Date: 2026-07-10  
Phone state when written: still **05c6:900e** after last gzip test — restore phase0 via keys→fastboot.

## 1. What we know is true (from device + docs, not guesswork)

### 1.1 Stock ABL always loads our `boot_b`
Every custom image we flashed was accepted (fastboot flash OK, reboot left fastboot). Failure is **after** ABL starts the kernel payload, not partition/AVB rejection (device unlocked/orange).

### 1.2 Failure mode matrix (same device, same slot B)
| Payload to ABL | Header | FD base | USB |
|----------------|--------|---------|-----|
| stock kernel Image (raw, ~49MB) + ramdisk | v4 | n/a | Android OK |
| BootShim+FD **gzip** | v3 | FFC or A7 | **900e** |
| BootShim+FD **raw** | v3 or v4 | FFC or A7 | **fastboot** (no dump) |
| BootShim fix text_offset=0 + gzip | v3 | A7 | **900e** still |

**Conclusion:** packaging header v3 vs v4 is **not** the root cause.  
**gzip path → hard crash / download mode.**  
**raw path → soft return to bootloader.**  
Neither path reached UEFI menu / USB gadget yet.

### 1.3 Stock kernel ARM64 Image header (ground truth)
From phase0 `boot_b` kernel:

| Field | Stock NX679J |
|-------|----------------|
| text_offset | **0x0** |
| image_size | 0x2cc0000 (~46MB) |
| flags | **0xa** |
| magic | ARMd |
| PE/MZ stub | present (EFI stub) |

### 1.4 Stock firmware UEFI map (from `full_extracted_v311/uefi.img`)
```
0xA7000000, 0x00400000, "UEFI FD"
0xA7400000, 0x00200000, "UEFI FD Reserved"
0xA8000000, 0x10000000, "Kernel"
DefaultBDSBootApp = "LinuxLoader"
```
There is **no** `0xFFC00000` in stock. Upstream mu_aloha SM8450 default FFC was **wrong for this phone**.

### 1.5 Qualcomm ABL behavior (external docs)
ARM64 booting protocol: bootloader may **gzip-decompress** Image before entry.  
Pixel/QCOM ABL is known to validate `text_offset` strictly:

> `KernelDecompress failed: Invalid Parameter Kernel TextOffset does not match`  
> (Dmitry Baryshkov / Qualcomm, arm64 TEXT_OFFSET discussion)

That matches our **gzip → 900e** pattern: ABL decompresses, validates header, fails/crashes into Sahara.

### 1.6 Project Aloha “correct” path for real phones is **not** bare BootShim-as-kernel
Official dual-boot docs ([PatchKernel](https://aloha.firmware.icu/DualBoot/PatchKernel.html), DualBootKernelPatcher):

1. Keep **stock kernel** (and stock DTB in vendor_boot / boot as stock does).
2. Inject shellcode into kernel head; append **UEFI FD** after kernel.
3. Store FD base/size in header **reserved2/reserved3** (not in text_offset).
4. Repack with **stock ramdisk + cmdline + header version**.
5. `fastboot boot` / flash the hybrid image.

Configs on GitHub today: Sm7125/Sm8150/Sm8250/Sm8550 — **no Sm8450 cfg yet**.  
Sm8550 uses `StackBase=0xC7CC0000` (matches that platform’s UEFI_BASE).  
For NX679J the analogous config is:

```
StackBase=0xA7000000
StackSize=0x00400000
```

Porting guide early test (`fastboot boot device.img`) is for **bringing up** after binaries/map are correct; production dual-boot uses the hybrid kernel path.

## 2. What was wrong in our materials / assumptions

| Assumption | Reality |
|------------|---------|
| SM8450 UEFI_BASE = 0xFFC00000 (upstream Waipio) | Stock NX679J: **0xA7000000** |
| Put UEFI_BASE in Image `text_offset` (old BootShim) | Stock text_offset=**0**; DualBoot puts base in **reserved2** |
| Bare gzip BootShim+FD is same as “working Aloha boot.img” | Aloha production uses **stock kernel + shellcode + FD** |
| Header v3 vs v4 is the blocker | Ruled out by raw v3/v4 both → fastboot |
| Flashing many packaging variants will converge | Without fixing ABL-visible Image header / dualboot method, only 900e vs fastboot |

## 3. Code fixes already applied (keep; still not sufficient alone)

1. **FD base rebase** to `0xA7000000` (`sm8450.json`, `Waipio.fdf`, memory maps).  
2. **BootShim header layout** toward DualBoot-compatible:
   - text_offset = 0  
   - flags = 0xa  
   - reserved2 = UEFI_BASE  
   - reserved3 = UEFI_SIZE  
   - image_size = exact (shim + FD) via `(_Payload - _Head) + UEFI_SIZE`  

Gzip test with text_offset=0 still hit **900e** → remaining issues include at least:

- Bare BootShim is **not** a stock-like kernel (size ~4MB, no MZ/EFI stub like stock, no real Linux body).
- ABL may still require more of the Image to look like a valid kernel (size bounds, PE offset, checksum path, etc.).
- Or ABL accepts Image then BootShim/SEC crashes hard under gzip-loaded address layout.

Raw path returning to **fastboot** suggests ABL or payload **reboots to bootloader** without Sahara — different failure than gzip.

## 4. Correct next plan (evidence-driven, not flash roulette)

### Phase A — Restore & freeze flashing
Key combo → fastboot → phase0 restore. No more experimental `boot_b` until a hybrid image is built offline.

### Phase B — Build DualBoot hybrid (correct method)
1. Build DualBootKernelPatcher + shellcode (e.g. KernelWrapper or a generic Hotdog-like code if GPIO switch N/A; for pure UEFI force, need shellcode that always jumps UEFI).
2. Add `Config/DualBoot.Sm8450.cfg`:
   ```
   StackBase=0xA7000000
   StackSize=0x00400000
   ```
3. Inputs:
   - stock kernel from phase0 `boot_b` (raw Image)
   - `SM8450_EFI.fd` (A7-linked DEBUG)
4. Output hybrid kernel; **mkbootimg** using **stock** header version (4), **stock ramdisk**, **stock cmdline** empty or original, same os_version/patch as stock.
5. Prefer `fastboot boot hybrid.img` once (no permanent flash) after restore.

If pure UEFI (no Android dual): shellcode that always takes UEFI path (no key/GPIO check), still based on stock Image body so ABL validation passes.

### Phase C — Only if DualBoot hybrid still fails
1. `sudo edl peek` of PStore only on gzip paths (needs password).  
2. Diff stock ABL entry vs hybrid with serial/logs.  
3. Consider stock **uefi** partition analysis vs boot-payload path (different product surface).

### Phase D — Do not do
- More random v3/v4/gzip/raw matrix.  
- Flashing `abl_b` until DualBoot hybrid is proven impossible.  
- Treating HDK8450 defaults as NX679J truth without stock uefiplat.

## 5. Why 900e specifically
On Qualcomm, early kernel/ABL failure with dload enabled often presents as **Sahara 900e**. Stock cmdline includes `qcom-dload-mode.download_mode=1`.  
So 900e = **crash/assert in ABL decompress/prepare or very early payload**, not “wrong USB cable”.

Raw→fastboot is **healthier** for iteration (easy restore) but still means **no successful UEFI handoff**.

## 6. Summary one-liner
**Blocker:** bare BootShim-as-kernel violates stock ABL’s Image expectations (text_offset/layout + not a real kernel body); **gzip makes ABL decompress/validate and dump (900e)**; **correct Aloha approach is DualBoot hybrid of stock kernel + FD at 0xA7000000**.

## 7. Restore commands (when in fastboot)
```bash
cd /home/user/nx679j-stock/phase0-backups/2026-07-02-slotb-preproto
fastboot flash boot_b boot_b.img
fastboot flash vendor_boot_b vendor_boot_b.img
fastboot flash dtbo_b dtbo_b.img
fastboot flash vbmeta_b vbmeta_b.img
fastboot flash vbmeta_system_b vbmeta_system_b.img
fastboot set_active b
fastboot reboot
```
