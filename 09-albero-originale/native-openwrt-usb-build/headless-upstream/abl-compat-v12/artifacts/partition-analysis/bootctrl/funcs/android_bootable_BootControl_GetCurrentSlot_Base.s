000000000000f80c <android::bootable::BootControl::GetCurrentSlot()@@Base>:
    f80c:	d503245f 	bti	c
    f810:	b9402400 	ldr	w0, [x0, #36]
    f814:	d65f03c0 	ret
    f818:	d503233f 	paciasp
    f81c:	a9be7bfd 	stp	x29, x30, [sp, #-32]!
    f820:	f9000bf3 	str	x19, [sp, #16]
    f824:	910003fd 	mov	x29, sp
    f828:	aa0003f3 	mov	x19, x0
    f82c:	f0ffffa0 	adrp	x0, 6000 <android::gsi::kDsuUserdata@@Base-0x26d9>
    f830:	91204800 	add	x0, x0, #0x812
    f834:	aa1303e1 	mov	x1, x19
    f838:	94003ec2 	bl	1f340 <strcmp@plt>
    f83c:	340002c0 	cbz	w0, f894 <android::bootable::BootControl::GetCurrentSlot()@@Base+0x88>
    f840:	90ffffc0 	adrp	x0, 7000 <android::gsi::kDsuUserdata@@Base-0x16d9>
    f844:	910c6800 	add	x0, x0, #0x31a
    f848:	aa1303e1 	mov	x1, x19
    f84c:	94003ebd 	bl	1f340 <strcmp@plt>
    f850:	340001c0 	cbz	w0, f888 <android::bootable::BootControl::GetCurrentSlot()@@Base+0x7c>
    f854:	f0ffffa0 	adrp	x0, 6000 <android::gsi::kDsuUserdata@@Base-0x26d9>
    f858:	9132ac00 	add	x0, x0, #0xcab
    f85c:	aa1303e1 	mov	x1, x19
    f860:	94003eb8 	bl	1f340 <strcmp@plt>
    f864:	34000160 	cbz	w0, f890 <android::bootable::BootControl::GetCurrentSlot()@@Base+0x84>
    f868:	f0ffffa0 	adrp	x0, 6000 <android::gsi::kDsuUserdata@@Base-0x26d9>
    f86c:	9135b400 	add	x0, x0, #0xd6d
    f870:	aa1303e1 	mov	x1, x19
    f874:	94003eb3 	bl	1f340 <strcmp@plt>
    f878:	7100001f 	cmp	w0, #0x0
    f87c:	52800068 	mov	w8, #0x3                   	// #3
    f880:	5a9f0100 	csinv	w0, w8, wzr, eq	// eq = none
    f884:	14000004 	b	f894 <android::bootable::BootControl::GetCurrentSlot()@@Base+0x88>
    f888:	52800020 	mov	w0, #0x1                   	// #1
    f88c:	14000002 	b	f894 <android::bootable::BootControl::GetCurrentSlot()@@Base+0x88>
    f890:	52800040 	mov	w0, #0x2                   	// #2
    f894:	f9400bf3 	ldr	x19, [sp, #16]
    f898:	a8c27bfd 	ldp	x29, x30, [sp], #32
    f89c:	d50323bf 	autiasp
    f8a0:	d65f03c0 	ret

