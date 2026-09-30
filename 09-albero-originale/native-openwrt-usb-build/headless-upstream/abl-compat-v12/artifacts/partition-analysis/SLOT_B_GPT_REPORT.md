# GPT slot B success/retry metadata
Source: UFS GPT dumps (4K LBA), attributes field bits 48-55 (CAF ab_byte).
## Bit layout (CAF gpt-utils style)
- bits [1:0] priority
- bit [2] ACTIVE
- bits [5:3] retry/tries field
- bit [6] BOOT_SUCCESSFUL
- bit [7] UNBOOTABLE
## boot_b
- attrs=0x003a000000000000
- ab_byte=0x3a (00111010)
- ACTIVE=False
- SUCCESSFUL=False
- UNBOOTABLE=False
- priority=2
- retry_field=7
- compare boot_a ab=0x7f active=True succ=True retry=7
- compare typical *_b peer vendor_boot_b ab=0x7b
