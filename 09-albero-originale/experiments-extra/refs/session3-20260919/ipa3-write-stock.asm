
/home/user/nx679j-stock/stock-modules/ipam.ko:     file format elf64-littleaarch64


Disassembly of section .text:

000000000002c94c <ipa3_write$41945febc32b991b469f38e9831eb5b7>:
   2c94c:	d503233f 	paciasp
   2c950:	d10143ff 	sub	sp, sp, #0x50
   2c954:	f800865e 	str	x30, [x18], #8
   2c958:	a9037bfd 	stp	x29, x30, [sp, #48]
   2c95c:	a9044ff4 	stp	x20, x19, [sp, #64]
   2c960:	9100c3fd 	add	x29, sp, #0x30
   2c964:	90000008 	adrp	x8, 0 <__stack_chk_guard>
			2c964: R_AARCH64_ADR_PREL_PG_HI21	__stack_chk_guard
   2c968:	f9400108 	ldr	x8, [x8]
			2c968: R_AARCH64_LDST64_ABS_LO12_NC	__stack_chk_guard
   2c96c:	f1007c5f 	cmp	x2, #0x1f
   2c970:	f81f83a8 	stur	x8, [x29, #-8]
   2c974:	a901ffff 	stp	xzr, xzr, [sp, #24]
   2c978:	a900ffff 	stp	xzr, xzr, [sp, #8]
   2c97c:	540001c9 	b.ls	2c9b4 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x68>  // b.plast
   2c980:	928001b3 	mov	x19, #0xfffffffffffffff2    	// #-14
   2c984:	90000009 	adrp	x9, 0 <__stack_chk_guard>
			2c984: R_AARCH64_ADR_PREL_PG_HI21	__stack_chk_guard
   2c988:	f85f83a8 	ldur	x8, [x29, #-8]
   2c98c:	f9400129 	ldr	x9, [x9]
			2c98c: R_AARCH64_LDST64_ABS_LO12_NC	__stack_chk_guard
   2c990:	eb08013f 	cmp	x9, x8
   2c994:	54001e61 	b.ne	2cd60 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x414>  // b.any
   2c998:	a9437bfd 	ldp	x29, x30, [sp, #48]
   2c99c:	aa1303e0 	mov	x0, x19
   2c9a0:	a9444ff4 	ldp	x20, x19, [sp, #64]
   2c9a4:	f85f8e5e 	ldr	x30, [x18, #-8]!
   2c9a8:	910143ff 	add	sp, sp, #0x50
   2c9ac:	d50323bf 	autiasp
   2c9b0:	d65f03c0 	ret
   2c9b4:	aa0203f3 	mov	x19, x2
   2c9b8:	aa0103f4 	mov	x20, x1
   2c9bc:	910023e0 	add	x0, sp, #0x8
   2c9c0:	aa0203e1 	mov	x1, x2
   2c9c4:	2a1f03e2 	mov	w2, wzr
   2c9c8:	94000000 	bl	0 <__check_object_size>
			2c9c8: R_AARCH64_CALL26	__check_object_size
   2c9cc:	910023e0 	add	x0, sp, #0x8
   2c9d0:	aa1403e1 	mov	x1, x20
   2c9d4:	aa1303e2 	mov	x2, x19
   2c9d8:	94000000 	bl	0 <__cfi_check>
			2c9d8: R_AARCH64_CALL26	.text+0x23978
   2c9dc:	90000014 	adrp	x20, 10 <__cfi_check+0x10>
			2c9dc: R_AARCH64_ADR_PREL_PG_HI21	ipa3_ctx
   2c9e0:	b5000ee0 	cbnz	x0, 2cbbc <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x270>
   2c9e4:	b4000073 	cbz	x19, 2c9f0 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0xa4>
   2c9e8:	910023e8 	add	x8, sp, #0x8
   2c9ec:	3833691f 	strb	wzr, [x8, x19]
   2c9f0:	f9400288 	ldr	x8, [x20]
			2c9f0: R_AARCH64_LDST64_ABS_LO12_NC	ipa3_ctx
   2c9f4:	b40002c8 	cbz	x8, 2ca4c <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x100>
   2c9f8:	52902809 	mov	w9, #0x8140                	// #33088
   2c9fc:	f8696900 	ldr	x0, [x8, x9]
   2ca00:	b4000120 	cbz	x0, 2ca24 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0xd8>
   2ca04:	90000001 	adrp	x1, 0 <__cfi_check>
			2ca04: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x623
   2ca08:	90000002 	adrp	x2, 0 <__cfi_check>
			2ca08: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x180d0
   2ca0c:	91000021 	add	x1, x1, #0x0
			2ca0c: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x623
   2ca10:	91000042 	add	x2, x2, #0x0
			2ca10: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x180d0
   2ca14:	910023e4 	add	x4, sp, #0x8
   2ca18:	5283f223 	mov	w3, #0x1f91                	// #8081
   2ca1c:	94000000 	bl	0 <ipc_log_string>
			2ca1c: R_AARCH64_CALL26	ipc_log_string
   2ca20:	f9400288 	ldr	x8, [x20]
			2ca20: R_AARCH64_LDST64_ABS_LO12_NC	ipa3_ctx
   2ca24:	52902909 	mov	w9, #0x8148                	// #33096
   2ca28:	f8696900 	ldr	x0, [x8, x9]
   2ca2c:	b4000100 	cbz	x0, 2ca4c <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x100>
   2ca30:	90000001 	adrp	x1, 0 <__cfi_check>
			2ca30: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x623
   2ca34:	90000002 	adrp	x2, 0 <__cfi_check>
			2ca34: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x180d0
   2ca38:	91000021 	add	x1, x1, #0x0
			2ca38: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x623
   2ca3c:	91000042 	add	x2, x2, #0x0
			2ca3c: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x180d0
   2ca40:	910023e4 	add	x4, sp, #0x8
   2ca44:	5283f223 	mov	w3, #0x1f91                	// #8081
   2ca48:	94000000 	bl	0 <ipc_log_string>
			2ca48: R_AARCH64_CALL26	ipc_log_string
   2ca4c:	aa1f03e8 	mov	x8, xzr
   2ca50:	b40001d3 	cbz	x19, 2ca88 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x13c>
   2ca54:	9000000a 	adrp	x10, 0 <_ctype>
			2ca54: R_AARCH64_ADR_PREL_PG_HI21	_ctype
   2ca58:	910023e9 	add	x9, sp, #0x8
   2ca5c:	9100014a 	add	x10, x10, #0x0
			2ca5c: R_AARCH64_ADD_ABS_LO12_NC	_ctype
   2ca60:	7100811f 	cmp	w8, #0x20
   2ca64:	540017c0 	b.eq	2cd5c <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x410>  // b.none
   2ca68:	3868692b 	ldrb	w11, [x9, x8]
   2ca6c:	386b694b 	ldrb	w11, [x10, x11]
   2ca70:	362800cb 	tbz	w11, #5, 2ca88 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x13c>
   2ca74:	91000508 	add	x8, x8, #0x1
   2ca78:	93407d0b 	sxtw	x11, w8
   2ca7c:	eb13017f 	cmp	x11, x19
   2ca80:	54ffff03 	b.cc	2ca60 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x114>  // b.lo, b.ul, b.last
   2ca84:	aa0b03e8 	mov	x8, x11
   2ca88:	f9400289 	ldr	x9, [x20]
			2ca88: R_AARCH64_LDST64_ABS_LO12_NC	ipa3_ctx
   2ca8c:	528f760a 	mov	w10, #0x7bb0                	// #31664
   2ca90:	eb13011f 	cmp	x8, x19
   2ca94:	8b0a0128 	add	x8, x9, x10
   2ca98:	540002a1 	b.ne	2caec <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x1a0>  // b.any
   2ca9c:	b4fff749 	cbz	x9, 2c984 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x38>
   2caa0:	f942c900 	ldr	x0, [x8, #1424]
   2caa4:	b4000100 	cbz	x0, 2cac4 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x178>
   2caa8:	90000001 	adrp	x1, 0 <__cfi_check>
			2caa8: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0xea41
   2caac:	90000002 	adrp	x2, 0 <__cfi_check>
			2caac: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x180d0
   2cab0:	91000021 	add	x1, x1, #0x0
			2cab0: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0xea41
   2cab4:	91000042 	add	x2, x2, #0x0
			2cab4: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x180d0
   2cab8:	5283f343 	mov	w3, #0x1f9a                	// #8090
   2cabc:	94000000 	bl	0 <ipc_log_string>
			2cabc: R_AARCH64_CALL26	ipc_log_string
   2cac0:	f9400289 	ldr	x9, [x20]
			2cac0: R_AARCH64_LDST64_ABS_LO12_NC	ipa3_ctx
   2cac4:	52902908 	mov	w8, #0x8148                	// #33096
   2cac8:	f8686920 	ldr	x0, [x9, x8]
   2cacc:	b4fff5c0 	cbz	x0, 2c984 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x38>
   2cad0:	90000001 	adrp	x1, 0 <__cfi_check>
			2cad0: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0xea41
   2cad4:	90000002 	adrp	x2, 0 <__cfi_check>
			2cad4: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x180d0
   2cad8:	91000021 	add	x1, x1, #0x0
			2cad8: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0xea41
   2cadc:	91000042 	add	x2, x2, #0x0
			2cadc: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x180d0
   2cae0:	5283f343 	mov	w3, #0x1f9a                	// #8090
   2cae4:	94000000 	bl	0 <ipc_log_string>
			2cae4: R_AARCH64_CALL26	ipc_log_string
   2cae8:	17ffffa7 	b	2c984 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x38>
   2caec:	b9400108 	ldr	w8, [x8]
   2caf0:	340000c8 	cbz	w8, 2cb08 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x1bc>
   2caf4:	94000000 	bl	748b0 <ipa3_is_ready>
			2caf4: R_AARCH64_CALL26	ipa3_is_ready
   2caf8:	3707f460 	tbnz	w0, #0, 2c984 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x38>
   2cafc:	2a1f03e0 	mov	w0, wzr
   2cb00:	94000000 	bl	0 <__cfi_check>
			2cb00: R_AARCH64_CALL26	.text+0x1dff8
   2cb04:	17ffffa0 	b	2c984 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x38>
   2cb08:	910023e0 	add	x0, sp, #0x8
   2cb0c:	94000000 	bl	0 <strlen>
			2cb0c: R_AARCH64_CALL26	strlen
   2cb10:	90000001 	adrp	x1, 0 <__cfi_check>
			2cb10: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x32f7a
   2cb14:	aa0003e2 	mov	x2, x0
   2cb18:	91000021 	add	x1, x1, #0x0
			2cb18: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x32f7a
   2cb1c:	910023e0 	add	x0, sp, #0x8
   2cb20:	94000000 	bl	0 <strnstr>
			2cb20: R_AARCH64_CALL26	strnstr
   2cb24:	b4000840 	cbz	x0, 2cc2c <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x2e0>
   2cb28:	910023e0 	add	x0, sp, #0x8
   2cb2c:	94000000 	bl	0 <strlen>
			2cb2c: R_AARCH64_CALL26	strlen
   2cb30:	90000001 	adrp	x1, 0 <__cfi_check>
			2cb30: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x643
   2cb34:	aa0003e2 	mov	x2, x0
   2cb38:	91000021 	add	x1, x1, #0x0
			2cb38: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x643
   2cb3c:	910023e0 	add	x0, sp, #0x8
   2cb40:	94000000 	bl	0 <strnstr>
			2cb40: R_AARCH64_CALL26	strnstr
   2cb44:	b40000a0 	cbz	x0, 2cb58 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x20c>
   2cb48:	f9400288 	ldr	x8, [x20]
			2cb48: R_AARCH64_LDST64_ABS_LO12_NC	ipa3_ctx
   2cb4c:	52956c09 	mov	w9, #0xab60                	// #43872
   2cb50:	5280002a 	mov	w10, #0x1                   	// #1
   2cb54:	3829690a 	strb	w10, [x8, x9]
   2cb58:	910023e0 	add	x0, sp, #0x8
   2cb5c:	94000000 	bl	0 <strlen>
			2cb5c: R_AARCH64_CALL26	strlen
   2cb60:	90000001 	adrp	x1, 0 <__cfi_check>
			2cb60: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x15beb
   2cb64:	aa0003e2 	mov	x2, x0
   2cb68:	91000021 	add	x1, x1, #0x0
			2cb68: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x15beb
   2cb6c:	910023e0 	add	x0, sp, #0x8
   2cb70:	94000000 	bl	0 <strnstr>
			2cb70: R_AARCH64_CALL26	strnstr
   2cb74:	b40000a0 	cbz	x0, 2cb88 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x23c>
   2cb78:	f9400288 	ldr	x8, [x20]
			2cb78: R_AARCH64_LDST64_ABS_LO12_NC	ipa3_ctx
   2cb7c:	52956c29 	mov	w9, #0xab61                	// #43873
   2cb80:	5280002a 	mov	w10, #0x1                   	// #1
   2cb84:	3829690a 	strb	w10, [x8, x9]
   2cb88:	910023e0 	add	x0, sp, #0x8
   2cb8c:	94000000 	bl	0 <strlen>
			2cb8c: R_AARCH64_CALL26	strlen
   2cb90:	90000001 	adrp	x1, 0 <__cfi_check>
			2cb90: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x41861
   2cb94:	aa0003e2 	mov	x2, x0
   2cb98:	91000021 	add	x1, x1, #0x0
			2cb98: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x41861
   2cb9c:	910023e0 	add	x0, sp, #0x8
   2cba0:	94000000 	bl	0 <strnstr>
			2cba0: R_AARCH64_CALL26	strnstr
   2cba4:	b4ffef00 	cbz	x0, 2c984 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x38>
   2cba8:	f9400288 	ldr	x8, [x20]
			2cba8: R_AARCH64_LDST64_ABS_LO12_NC	ipa3_ctx
   2cbac:	52956c49 	mov	w9, #0xab62                	// #43874
   2cbb0:	5280002a 	mov	w10, #0x1                   	// #1
   2cbb4:	3829690a 	strb	w10, [x8, x9]
   2cbb8:	17ffff73 	b	2c984 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x38>
   2cbbc:	90000000 	adrp	x0, 0 <__cfi_check>
			2cbbc: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x3c7b9
   2cbc0:	90000001 	adrp	x1, 0 <__cfi_check>
			2cbc0: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x180d0
   2cbc4:	91000000 	add	x0, x0, #0x0
			2cbc4: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x3c7b9
   2cbc8:	91000021 	add	x1, x1, #0x0
			2cbc8: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x180d0
   2cbcc:	5283f142 	mov	w2, #0x1f8a                	// #8074
   2cbd0:	94000000 	bl	0 <printk>
			2cbd0: R_AARCH64_CALL26	printk
   2cbd4:	f9400288 	ldr	x8, [x20]
			2cbd4: R_AARCH64_LDST64_ABS_LO12_NC	ipa3_ctx
   2cbd8:	b4ffed48 	cbz	x8, 2c980 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x34>
   2cbdc:	52902809 	mov	w9, #0x8140                	// #33088
   2cbe0:	f8696900 	ldr	x0, [x8, x9]
   2cbe4:	b4000100 	cbz	x0, 2cc04 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x2b8>
   2cbe8:	90000001 	adrp	x1, 0 <__cfi_check>
			2cbe8: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x3ef18
   2cbec:	90000002 	adrp	x2, 0 <__cfi_check>
			2cbec: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x180d0
   2cbf0:	91000021 	add	x1, x1, #0x0
			2cbf0: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x3ef18
   2cbf4:	91000042 	add	x2, x2, #0x0
			2cbf4: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x180d0
   2cbf8:	5283f143 	mov	w3, #0x1f8a                	// #8074
   2cbfc:	94000000 	bl	0 <ipc_log_string>
			2cbfc: R_AARCH64_CALL26	ipc_log_string
   2cc00:	f9400288 	ldr	x8, [x20]
			2cc00: R_AARCH64_LDST64_ABS_LO12_NC	ipa3_ctx
   2cc04:	52902909 	mov	w9, #0x8148                	// #33096
   2cc08:	f8696900 	ldr	x0, [x8, x9]
   2cc0c:	b4ffeba0 	cbz	x0, 2c980 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x34>
   2cc10:	90000001 	adrp	x1, 0 <__cfi_check>
			2cc10: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x3ef18
   2cc14:	90000002 	adrp	x2, 0 <__cfi_check>
			2cc14: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x180d0
   2cc18:	91000021 	add	x1, x1, #0x0
			2cc18: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x3ef18
   2cc1c:	91000042 	add	x2, x2, #0x0
			2cc1c: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x180d0
   2cc20:	5283f143 	mov	w3, #0x1f8a                	// #8074
   2cc24:	94000000 	bl	0 <ipc_log_string>
			2cc24: R_AARCH64_CALL26	ipc_log_string
   2cc28:	17ffff56 	b	2c980 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x34>
   2cc2c:	b4000133 	cbz	x19, 2cc50 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x304>
   2cc30:	d1000668 	sub	x8, x19, #0x1
   2cc34:	f100811f 	cmp	x8, #0x20
   2cc38:	54000928 	b.hi	2cd5c <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x410>  // b.pmore
   2cc3c:	910023e9 	add	x9, sp, #0x8
   2cc40:	3868692a 	ldrb	w10, [x9, x8]
   2cc44:	7100295f 	cmp	w10, #0xa
   2cc48:	54000041 	b.ne	2cc50 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x304>  // b.any
   2cc4c:	3828693f 	strb	wzr, [x9, x8]
   2cc50:	90000001 	adrp	x1, 0 <__cfi_check>
			2cc50: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x132a8
   2cc54:	91000021 	add	x1, x1, #0x0
			2cc54: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x132a8
   2cc58:	910023e0 	add	x0, sp, #0x8
   2cc5c:	94000000 	bl	0 <strcasecmp>
			2cc5c: R_AARCH64_CALL26	strcasecmp
   2cc60:	35000080 	cbnz	w0, 2cc70 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x324>
   2cc64:	f9400288 	ldr	x8, [x20]
			2cc64: R_AARCH64_LDST64_ABS_LO12_NC	ipa3_ctx
   2cc68:	528f7689 	mov	w9, #0x7bb4                	// #31668
   2cc6c:	14000008 	b	2cc8c <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x340>
   2cc70:	b9400be8 	ldr	w8, [sp, #8]
   2cc74:	52884889 	mov	w9, #0x4244                	// #16964
   2cc78:	72a00a69 	movk	w9, #0x53, lsl #16
   2cc7c:	6b09011f 	cmp	w8, w9
   2cc80:	54000261 	b.ne	2cccc <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x380>  // b.any
   2cc84:	f9400288 	ldr	x8, [x20]
			2cc84: R_AARCH64_LDST64_ABS_LO12_NC	ipa3_ctx
   2cc88:	5297e109 	mov	w9, #0xbf08                	// #48904
   2cc8c:	5280002a 	mov	w10, #0x1                   	// #1
   2cc90:	3829690a 	strb	w10, [x8, x9]
   2cc94:	528f7689 	mov	w9, #0x7bb4                	// #31668
   2cc98:	38696908 	ldrb	w8, [x8, x9]
   2cc9c:	90000009 	adrp	x9, 0 <__cfi_check>
			2cc9c: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x3ef41
   2cca0:	9000000a 	adrp	x10, 0 <__cfi_check>
			2cca0: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x23122
   2cca4:	91000129 	add	x9, x9, #0x0
			2cca4: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x3ef41
   2cca8:	9100014a 	add	x10, x10, #0x0
			2cca8: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x23122
   2ccac:	7100011f 	cmp	w8, #0x0
   2ccb0:	90000000 	adrp	x0, 0 <__cfi_check>
			2ccb0: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x20cad
   2ccb4:	9a890141 	csel	x1, x10, x9, eq	// eq = none
   2ccb8:	91000000 	add	x0, x0, #0x0
			2ccb8: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x20cad
   2ccbc:	94000000 	bl	0 <printk>
			2ccbc: R_AARCH64_CALL26	printk
   2ccc0:	94000000 	bl	748b0 <ipa3_is_ready>
			2ccc0: R_AARCH64_CALL26	ipa3_is_ready
   2ccc4:	3707e600 	tbnz	w0, #0, 2c984 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x38>
   2ccc8:	17ffff8d 	b	2cafc <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x1b0>
   2cccc:	794013e8 	ldrh	w8, [sp, #8]
   2ccd0:	7100c51f 	cmp	w8, #0x31
   2ccd4:	54000061 	b.ne	2cce0 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x394>  // b.any
   2ccd8:	f9400288 	ldr	x8, [x20]
			2ccd8: R_AARCH64_LDST64_ABS_LO12_NC	ipa3_ctx
   2ccdc:	17ffffee 	b	2cc94 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x348>
   2cce0:	90000000 	adrp	x0, 0 <__cfi_check>
			2cce0: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0xea62
   2cce4:	90000001 	adrp	x1, 0 <__cfi_check>
			2cce4: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x180d0
   2cce8:	91000000 	add	x0, x0, #0x0
			2cce8: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0xea62
   2ccec:	91000021 	add	x1, x1, #0x0
			2ccec: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x180d0
   2ccf0:	910023e3 	add	x3, sp, #0x8
   2ccf4:	5283f9c2 	mov	w2, #0x1fce                	// #8142
   2ccf8:	94000000 	bl	0 <printk>
			2ccf8: R_AARCH64_CALL26	printk
   2ccfc:	f9400288 	ldr	x8, [x20]
			2ccfc: R_AARCH64_LDST64_ABS_LO12_NC	ipa3_ctx
   2cd00:	b4ffe428 	cbz	x8, 2c984 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x38>
   2cd04:	52902809 	mov	w9, #0x8140                	// #33088
   2cd08:	f8696900 	ldr	x0, [x8, x9]
   2cd0c:	b4000120 	cbz	x0, 2cd30 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x3e4>
   2cd10:	90000001 	adrp	x1, 0 <__cfi_check>
			2cd10: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x553c
   2cd14:	90000002 	adrp	x2, 0 <__cfi_check>
			2cd14: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x180d0
   2cd18:	91000021 	add	x1, x1, #0x0
			2cd18: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x553c
   2cd1c:	91000042 	add	x2, x2, #0x0
			2cd1c: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x180d0
   2cd20:	910023e4 	add	x4, sp, #0x8
   2cd24:	5283f9c3 	mov	w3, #0x1fce                	// #8142
   2cd28:	94000000 	bl	0 <ipc_log_string>
			2cd28: R_AARCH64_CALL26	ipc_log_string
   2cd2c:	f9400288 	ldr	x8, [x20]
			2cd2c: R_AARCH64_LDST64_ABS_LO12_NC	ipa3_ctx
   2cd30:	52902909 	mov	w9, #0x8148                	// #33096
   2cd34:	f8696900 	ldr	x0, [x8, x9]
   2cd38:	b4ffe260 	cbz	x0, 2c984 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x38>
   2cd3c:	90000001 	adrp	x1, 0 <__cfi_check>
			2cd3c: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x553c
   2cd40:	90000002 	adrp	x2, 0 <__cfi_check>
			2cd40: R_AARCH64_ADR_PREL_PG_HI21	.rodata+0x180d0
   2cd44:	91000021 	add	x1, x1, #0x0
			2cd44: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x553c
   2cd48:	91000042 	add	x2, x2, #0x0
			2cd48: R_AARCH64_ADD_ABS_LO12_NC	.rodata+0x180d0
   2cd4c:	910023e4 	add	x4, sp, #0x8
   2cd50:	5283f9c3 	mov	w3, #0x1fce                	// #8142
   2cd54:	94000000 	bl	0 <ipc_log_string>
			2cd54: R_AARCH64_CALL26	ipc_log_string
   2cd58:	17ffff0b 	b	2c984 <ipa3_write$41945febc32b991b469f38e9831eb5b7+0x38>
   2cd5c:	d4200020 	brk	#0x1
   2cd60:	94000000 	bl	0 <__stack_chk_fail>
			2cd60: R_AARCH64_CALL26	__stack_chk_fail
