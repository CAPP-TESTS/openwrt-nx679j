00*************c <android::bootable::BootControl::SetActiveBootSlot(unsigned int)@@Base>:
   1026c:	d503233f 	paciasp
   10270:	d101c3ff 	sub	sp, sp, #0x70
   10274:	a9037bfd 	stp	x29, x30, [sp, #48]
   10278:	f90023f7 	str	x23, [sp, #64]
   1027c:	a90557f6 	stp	x22, x21, [sp, #80]
   10280:	a9064ff4 	stp	x20, x19, [sp, #96]
   10284:	9100c3fd 	add	x29, sp, #0x30
   10288:	d53bd056 	mrs	x22, tpidr_el0
   1028c:	f94016c8 	ldr	x8, [x22, #40]
   10290:	71000c3f 	cmp	w1, #0x3
   10294:	f81f83a8 	stur	x8, [x29, #-8]
   10298:	54000468 	b.hi	10324 <android::bootable::BootControl::SetActiveBootSlot(unsigned int)@@Base+0xb8>  // b.pmore
   1029c:	b9402008 	ldr	w8, [x0, #32]
   102a0:	2a0103f5 	mov	w21, w1
   102a4:	aa0003f4 	mov	x20, x0
   102a8:	6b01011f 	cmp	w8, w1
   102ac:	540003c9 	b.ls	10324 <android::bootable::BootControl::SetActiveBootSlot(unsigned int)@@Base+0xb8>  // b.plast
   102b0:	91002293 	add	x19, x20, #0x8
   102b4:	6f00e400 	movi	v0.2d, #0x0
   102b8:	910003e1 	mov	x1, sp
   102bc:	aa1303e0 	mov	x0, x19
   102c0:	ad0003e0 	stp	q0, q0, [sp]
   102c4:	910003f7 	mov	x23, sp
   102c8:	94003c26 	bl	1f360 <android::bootable::LoadBootloaderControl(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char> > const&, bootloader_control*)@plt>
   102cc:	360002c0 	tbz	w0, #0, 10324 <android::bootable::BootControl::SetActiveBootSlot(unsigned int)@@Base+0xb8>
   102d0:	b940228a 	ldr	w10, [x20, #32]
   102d4:	aa1f03e9 	mov	x9, xzr
   102d8:	2a1503e8 	mov	w8, w21
   102dc:	14000003 	b	102e8 <android::bootable::BootControl::SetActiveBootSlot(unsigned int)@@Base+0x7c>
   102e0:	91000529 	add	x9, x9, #0x1
   102e4:	340005a9 	cbz	w9, 10398 <android::bootable::BootControl::SetActiveBootSlot(unsigned int)@@Base+0x12c>
   102e8:	eb09015f 	cmp	x10, x9
   102ec:	54000360 	b.eq	10358 <android::bootable::BootControl::SetActiveBootSlot(unsigned int)@@Base+0xec>  // b.none
   102f0:	eb09011f 	cmp	x8, x9
   102f4:	54ffff60 	b.eq	102e0 <android::bootable::BootControl::SetActiveBootSlot(unsigned int)@@Base+0x74>  // b.none
   102f8:	f100113f 	cmp	x9, #0x4
   102fc:	54000522 	b.cs	103a0 <android::bootable::BootControl::SetActiveBootSlot(unsigned int)@@Base+0x134>  // b.hs, b.nlast
   10300:	8b0906eb 	add	x11, x23, x9, lsl #1
   10304:	7940196c 	ldrh	w12, [x11, #12]
   10308:	2a2c03ed 	mvn	w13, w12
   1030c:	72000dbf 	tst	w13, #0xf
   10310:	54fffe81 	b.ne	102e0 <android::bootable::BootControl::SetActiveBootSlot(unsigned int)@@Base+0x74>  // b.any
   10314:	121c2d8c 	and	w12, w12, #0xfff0
   10318:	321f098c 	orr	w12, w12, #0xe
   1031c:	7900196c 	strh	w12, [x11, #12]
   10320:	17fffff0 	b	102e0 <android::bootable::BootControl::SetActiveBootSlot(unsigned int)@@Base+0x74>
   10324:	2a1f03e0 	mov	w0, wzr
   10328:	f94016c8 	ldr	x8, [x22, #40]
   1032c:	f85f83a9 	ldur	x9, [x29, #-8]
   10330:	eb09011f 	cmp	x8, x9
   10334:	540003a1 	b.ne	103a8 <android::bootable::BootControl::SetActiveBootSlot(unsigned int)@@Base+0x13c>  // b.any
   10338:	a9464ff4 	ldp	x20, x19, [sp, #96]
   1033c:	a94557f6 	ldp	x22, x21, [sp, #80]
   10340:	f94023f7 	ldr	x23, [sp, #64]
   10344:	a9437bfd 	ldp	x29, x30, [sp, #48]
   10348:	12000000 	and	w0, w0, #0x1
   1034c:	9101c3ff 	add	sp, sp, #0x70
   10350:	d50323bf 	autiasp
   10354:	d65f03c0 	ret
   10358:	910003e9 	mov	x9, sp
   1035c:	8b080529 	add	x9, x9, x8, lsl #1
   10360:	7840cd2a 	ldrh	w10, [x9, #12]!
   10364:	52800deb 	mov	w11, #0x6f                  	// #111
   10368:	3300196a 	bfxil	w10, w11, #0, #7
   1036c:	7900012a 	strh	w10, [x9]
   10370:	b940268b 	ldr	w11, [x20, #36]
   10374:	6b08017f 	cmp	w11, w8
   10378:	54000080 	b.eq	10388 <android::bootable::BootControl::SetActiveBootSlot(unsigned int)@@Base+0x11c>  // b.none
   1037c:	529fdde8 	mov	w8, #0xfeef                	// #65263
   10380:	0a080148 	and	w8, w10, w8
   10384:	79000128 	strh	w8, [x9]
   10388:	910003e1 	mov	x1, sp
   1038c:	aa1303e0 	mov	x0, x19
   10390:	94003c00 	bl	1f390 <android::bootable::UpdateAndSaveBootloaderControl(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char> > const&, bootloader_control*)@plt>
   10394:	17ffffe5 	b	10328 <android::bootable::BootControl::SetActiveBootSlot(unsigned int)@@Base+0xbc>
   10398:	2a1f03e0 	mov	w0, wzr
   1039c:	94003ae9 	bl	1ef40 <abort@plt>
   103a0:	52800240 	mov	w0, #0x12                  	// #18
   103a4:	94003ae7 	bl	1ef40 <abort@plt>
   103a8:	94003af2 	bl	1ef70 <__stack_chk_fail@plt>

