00000000000104f8 <android::bootable::BootControl::IsSlotMarkedSuccessful(unsigned int)@@Base>:
   104f8:	d503233f 	paciasp
   104fc:	d10183ff 	sub	sp, sp, #0x60
   10500:	a9037bfd 	stp	x29, x30, [sp, #48]
   10504:	f90023f5 	str	x21, [sp, #64]
   10508:	a9054ff4 	stp	x20, x19, [sp, #80]
   1050c:	9100c3fd 	add	x29, sp, #0x30
   10510:	d53bd054 	mrs	x20, tpidr_el0
   10514:	f9401688 	ldr	x8, [x20, #40]
   10518:	71000c3f 	cmp	w1, #0x3
   1051c:	f81f83a8 	stur	x8, [x29, #-8]
   10520:	540001e8 	b.hi	1055c <android::bootable::BootControl::IsSlotMarkedSuccessful(unsigned int)@@Base+0x64>  // b.pmore
   10524:	b9402008 	ldr	w8, [x0, #32]
   10528:	2a0103f3 	mov	w19, w1
   1052c:	6b01011f 	cmp	w8, w1
   10530:	54000169 	b.ls	1055c <android::bootable::BootControl::IsSlotMarkedSuccessful(unsigned int)@@Base+0x64>  // b.plast
   10534:	6f00e400 	movi	v0.2d, #0x0
   10538:	91002000 	add	x0, x0, #0x8
   1053c:	910003e1 	mov	x1, sp
   10540:	ad0003e0 	stp	q0, q0, [sp]
   10544:	910003f5 	mov	x21, sp
   10548:	94003b86 	bl	1f360 <android::bootable::LoadBootloaderControl(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char> > const&, bootloader_control*)@plt>
   1054c:	36000080 	tbz	w0, #0, 1055c <android::bootable::BootControl::IsSlotMarkedSuccessful(unsigned int)@@Base+0x64>
   10550:	8b3346a8 	add	x8, x21, w19, uxtw #1
   10554:	79401908 	ldrh	w8, [x8, #12]
   10558:	37380188 	tbnz	w8, #7, 10588 <android::bootable::BootControl::IsSlotMarkedSuccessful(unsigned int)@@Base+0x90>
   1055c:	2a1f03e0 	mov	w0, wzr
   10560:	f9401688 	ldr	x8, [x20, #40]
   10564:	f85f83a9 	ldur	x9, [x29, #-8]
   10568:	eb09011f 	cmp	x8, x9
   1056c:	54000141 	b.ne	10594 <android::bootable::BootControl::IsSlotMarkedSuccessful(unsigned int)@@Base+0x9c>  // b.any
   10570:	a9454ff4 	ldp	x20, x19, [sp, #80]
   10574:	f94023f5 	ldr	x21, [sp, #64]
   10578:	a9437bfd 	ldp	x29, x30, [sp, #48]
   1057c:	910183ff 	add	sp, sp, #0x60
   10580:	d50323bf 	autiasp
   10584:	d65f03c0 	ret
   10588:	721c091f 	tst	w8, #0x70
   1058c:	1a9f07e0 	cset	w0, ne	// ne = any
   10590:	17fffff4 	b	10560 <android::bootable::BootControl::IsSlotMarkedSuccessful(unsigned int)@@Base+0x68>
   10594:	94003a77 	bl	1ef70 <__stack_chk_fail@plt>

