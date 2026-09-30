00000000000103ac <android::bootable::BootControl::SetSlotAsUnbootable(unsigned int)@@Base>:
   103ac:	d503233f 	paciasp
   103b0:	d10183ff 	sub	sp, sp, #0x60
   103b4:	a9037bfd 	stp	x29, x30, [sp, #48]
   103b8:	a90457f6 	stp	x22, x21, [sp, #64]
   103bc:	a9054ff4 	stp	x20, x19, [sp, #80]
   103c0:	9100c3fd 	add	x29, sp, #0x30
   103c4:	d53bd055 	mrs	x21, tpidr_el0
   103c8:	f94016a8 	ldr	x8, [x21, #40]
   103cc:	71000c3f 	cmp	w1, #0x3
   103d0:	f81f83a8 	stur	x8, [x29, #-8]
   103d4:	540002a8 	b.hi	10428 <android::bootable::BootControl::SetSlotAsUnbootable(unsigned int)@@Base+0x7c>  // b.pmore
   103d8:	b9402008 	ldr	w8, [x0, #32]
   103dc:	2a0103f3 	mov	w19, w1
   103e0:	6b01011f 	cmp	w8, w1
   103e4:	54000229 	b.ls	10428 <android::bootable::BootControl::SetSlotAsUnbootable(unsigned int)@@Base+0x7c>  // b.plast
   103e8:	91002014 	add	x20, x0, #0x8
   103ec:	6f00e400 	movi	v0.2d, #0x0
   103f0:	910003e1 	mov	x1, sp
   103f4:	aa1403e0 	mov	x0, x20
   103f8:	ad0003e0 	stp	q0, q0, [sp]
   103fc:	910003f6 	mov	x22, sp
   10400:	94003bd8 	bl	1f360 <android::bootable::LoadBootloaderControl(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char> > const&, bootloader_control*)@plt>
   10404:	36000120 	tbz	w0, #0, 10428 <android::bootable::BootControl::SetSlotAsUnbootable(unsigned int)@@Base+0x7c>
   10408:	8b3346c8 	add	x8, x22, w19, uxtw #1
   1040c:	79401909 	ldrh	w9, [x8, #12]
   10410:	910003e1 	mov	x1, sp
   10414:	aa1403e0 	mov	x0, x20
   10418:	12186d29 	and	w9, w9, #0xffffff0f
   1041c:	79001909 	strh	w9, [x8, #12]
   10420:	94003bdc 	bl	1f390 <android::bootable::UpdateAndSaveBootloaderControl(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char> > const&, bootloader_control*)@plt>
   10424:	14000002 	b	1042c <android::bootable::BootControl::SetSlotAsUnbootable(unsigned int)@@Base+0x80>
   10428:	2a1f03e0 	mov	w0, wzr
   1042c:	f94016a8 	ldr	x8, [x21, #40]
   10430:	f85f83a9 	ldur	x9, [x29, #-8]
   10434:	eb09011f 	cmp	x8, x9
   10438:	54000101 	b.ne	10458 <android::bootable::BootControl::SetSlotAsUnbootable(unsigned int)@@Base+0xac>  // b.any
   1043c:	a9454ff4 	ldp	x20, x19, [sp, #80]
   10440:	a94457f6 	ldp	x22, x21, [sp, #64]
   10444:	a9437bfd 	ldp	x29, x30, [sp, #48]
   10448:	12000000 	and	w0, w0, #0x1
   1044c:	910183ff 	add	sp, sp, #0x60
   10450:	d50323bf 	autiasp
   10454:	d65f03c0 	ret
   10458:	94003ac6 	bl	1ef70 <__stack_chk_fail@plt>

