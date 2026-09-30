00*************c <android::bootable::BootControl::IsSlotBootable(unsigned int)@@Base>:
   1045c:	d503233f 	paciasp
   10460:	d10183ff 	sub	sp, sp, #0x60
   10464:	a9037bfd 	stp	x29, x30, [sp, #48]
   10468:	f90023f5 	str	x21, [sp, #64]
   1046c:	a9054ff4 	stp	x20, x19, [sp, #80]
   10470:	9100c3fd 	add	x29, sp, #0x30
   10474:	d53bd054 	mrs	x20, tpidr_el0
   10478:	f9401688 	ldr	x8, [x20, #40]
   1047c:	71000c3f 	cmp	w1, #0x3
   10480:	f81f83a8 	stur	x8, [x29, #-8]
   10484:	54000228 	b.hi	104c8 <android::bootable::BootControl::IsSlotBootable(unsigned int)@@Base+0x6c>  // b.pmore
   10488:	b9402008 	ldr	w8, [x0, #32]
   1048c:	2a0103f3 	mov	w19, w1
   10490:	6b01011f 	cmp	w8, w1
   10494:	540001a9 	b.ls	104c8 <android::bootable::BootControl::IsSlotBootable(unsigned int)@@Base+0x6c>  // b.plast
   10498:	6f00e400 	movi	v0.2d, #0x0
   1049c:	91002000 	add	x0, x0, #0x8
   104a0:	910003e1 	mov	x1, sp
   104a4:	ad0003e0 	stp	q0, q0, [sp]
   104a8:	910003f5 	mov	x21, sp
   104ac:	94003bad 	bl	1f360 <android::bootable::LoadBootloaderControl(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char> > const&, bootloader_control*)@plt>
   104b0:	360000c0 	tbz	w0, #0, 104c8 <android::bootable::BootControl::IsSlotBootable(unsigned int)@@Base+0x6c>
   104b4:	8b3346a8 	add	x8, x21, w19, uxtw #1
   104b8:	79401908 	ldrh	w8, [x8, #12]
   104bc:	721c091f 	tst	w8, #0x70
   104c0:	1a9f07e0 	cset	w0, ne	// ne = any
   104c4:	14000002 	b	104cc <android::bootable::BootControl::IsSlotBootable(unsigned int)@@Base+0x70>
   104c8:	2a1f03e0 	mov	w0, wzr
   104cc:	f9401688 	ldr	x8, [x20, #40]
   104d0:	f85f83a9 	ldur	x9, [x29, #-8]
   104d4:	eb09011f 	cmp	x8, x9
   104d8:	540000e1 	b.ne	104f4 <android::bootable::BootControl::IsSlotBootable(unsigned int)@@Base+0x98>  // b.any
   104dc:	a9454ff4 	ldp	x20, x19, [sp, #80]
   104e0:	f94023f5 	ldr	x21, [sp, #64]
   104e4:	a9437bfd 	ldp	x29, x30, [sp, #48]
   104e8:	910183ff 	add	sp, sp, #0x60
   104ec:	d50323bf 	autiasp
   104f0:	d65f03c0 	ret
   104f4:	94003a9f 	bl	1ef70 <__stack_chk_fail@plt>

