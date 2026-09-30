# NX679J key and boot-verification analysis

## Verified

- Live `abl_a` and `abl_b` are byte-identical, SHA-256 `443d1956e836aea3fe30ac13113dae787d88223c1e24f875c6831f69d86acea3`.
- The live ABL uses Qualcomm hash-segment v7, SHA-384 hashes, ECDSA P-384 OEM signature, and a three-certificate chain.
- The live root certificate subject is `Generated Ztemt Root CA`; its full-DER root hash is `5ad5c780085682ddc9cbe4281ffe476781f32ea2da881dba7a2944fa55e50ea5`.
- No private-key material is present in the live ABL readback: no PEM/private-key markers; the image contains signatures, public certificates, and public-key material only.
- The local `qtestsign` project deliberately ships test keys for reproducible dummy signing. Its generated root is `qtestsign Root CA - NOT SECURE`, not the live ZTE root.
- `qtestsign` documents that dummy-signed images are not intended for devices with permanent firmware secure boot.
- The local LinuxLoader source chooses the boot path through `LoadImageAndAuth()`, AVB policy, and `BootLinux()`. The constant `SECBOOT_FUSE=0` is only a source-level bit index; the runtime security state is obtained through the Qualcomm SCM `TZ_INFO_GET_SECURE_STATE` call.

## Consequence

The ZTE private signing key cannot be reconstructed from the ABL certificate chain or its ECDSA signature. The public data supports verification, not signing. A vulnerability could theoretically bypass or alter verification or permit a temporary payload, but that would not recover the private key.

## Productive next paths

1. Analyze the existing LinuxLoader/UEFI boot path and produce a valid stock-kernel + UEFI hybrid payload, leaving ABL and UEFI partitions untouched.
2. Perform read-only research of public SM8450/Sahara/Firehose vulnerabilities for temporary execution or diagnostic access on this owned device. Do not target secret extraction.
3. Compare runtime security-state evidence and actual A/B boot behavior against the source's AVB branches before changing any image.

## Not established

- The exact Qualcomm fuse/security-state bitfield returned by the live device is not directly exposed through Android.
- A public vulnerability is not proof of applicability to this Nubia firmware revision.
- No custom ABL or UEFI image is authorized for flashing from this analysis.
