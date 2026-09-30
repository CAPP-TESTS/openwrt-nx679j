.section .text
.global _start

// Linux arm64 Image header fields retained from the authenticated V311 kernel.
_start:
  b     _AlwaysUefi
  .word 0x149abfff
  .quad 0x00**************
  .quad 0x0000000002cc0000
  .quad 0x00*************a
_UefiBase:
  .quad 0x00000000a7000000
_UefiSize:
  .quad 0x00**************
_ImageSize:
  .quad 0x0000000002ed5570
  .word 0x644d5241
  .word 0x00000040

_AlwaysUefi:
  b     _CopyUefi
  .word 0x17fffff0

_CopyUefi:
  adr   x4, _start
  ldr   x5, _ImageSize
  add   x4, x4, x5
  ldr   x5, _UefiBase
  ldr   x6, _UefiSize

_CopyLoop:
  ldp   x2, x3, [x4], #16
  stp   x2, x3, [x5], #16
  subs  x6, x6, #16
  b.ne  _CopyLoop

  // x5 now points to 0xA7400000, immediately after the copied 4 MiB FD.
  add   x0, x5, #0x205, lsl #12
  mov   w1, #0xB101
  str   w1, [x0]
  dsb   sy
  ic    iallu
  dsb   sy
  isb
  ldr   x5, _UefiBase
  br    x5
