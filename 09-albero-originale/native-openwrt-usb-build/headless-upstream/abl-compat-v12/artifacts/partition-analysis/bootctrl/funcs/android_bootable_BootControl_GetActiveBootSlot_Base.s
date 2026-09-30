00*************c <android::bootable::BootControl::GetActiveBootSlot()@@Base>:
   1019c:	d503233f 	paciasp
   101a0:	d10143ff 	sub	sp, sp, #0x50
   101a4:	a9037bfd 	stp	x29, x30, [sp, #48]
   101a8:	a9044ff4 	stp	x20, x19, [sp, #64]
   101ac:	9100c3fd 	add	x29, sp, #0x30
   101b0:	d53bd054 	mrs	x20, tpidr_el0
   101b4:	f9401688 	ldr	x8, [x20, #40]
   101b8:	aa0003f3 	mov	x19, x0
   101bc:	6f00e400 	movi	v0.2d, #0x0
   101c0:	91002000 	add	x0, x0, #0x8
   101c4:	910003e1 	mov	x1, sp
   101c8:	f81f83a8 	stur	x8, [x29, #-8]
   101cc:	ad0003e0 	stp	q0, q0, [sp]
   101d0:	94003c64 	bl	1f360 <android::bootable::LoadBootloaderControl(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char> > const&, bootloader_control*)@plt>
   101d4:	36000320 	tbz	w0, #0, 10238 <android::bootable::BootControl::GetActiveBootSlot()@@Base+0x9c>
   101d8:	b9402660 	ldr	w0, [x19, #36]
   101dc:	7100101f 	cmp	w0, #0x4
   101e0:	54000402 	b.cs	10260 <android::bootable::BootControl::GetActiveBootSlot()@@Base+0xc4>  // b.hs, b.nlast
   101e4:	b940226c 	ldr	w12, [x19, #32]
   101e8:	340002ac 	cbz	w12, 1023c <android::bootable::BootControl::GetActiveBootSlot()@@Base+0xa0>
   101ec:	910003e9 	mov	x9, sp
   101f0:	b27e0529 	orr	x9, x9, #0xc
   101f4:	7860792b 	ldrh	w11, [x9, x0, lsl #1]
   101f8:	f100059f 	cmp	x12, #0x1
   101fc:	aa1f03e8 	mov	x8, xzr
   10200:	5100058a 	sub	w10, w12, #0x1
   10204:	12000d6b 	and	w11, w11, #0xf
   10208:	9a9f858c 	csinc	x12, x12, xzr, hi	// hi = pmore
   1020c:	71000d5f 	cmp	w10, #0x3
   10210:	54000288 	b.hi	10260 <android::bootable::BootControl::GetActiveBootSlot()@@Base+0xc4>  // b.pmore
   10214:	7868792d 	ldrh	w13, [x9, x8, lsl #1]
   10218:	12000dad 	and	w13, w13, #0xf
   1021c:	6b0d017f 	cmp	w11, w13
   10220:	1a803100 	csel	w0, w8, w0, cc	// cc = lo, ul, last
   10224:	91000508 	add	x8, x8, #0x1
   10228:	1a8b31ab 	csel	w11, w13, w11, cc	// cc = lo, ul, last
   1022c:	eb08019f 	cmp	x12, x8
   10230:	54fffee1 	b.ne	1020c <android::bootable::BootControl::GetActiveBootSlot()@@Base+0x70>  // b.any
   10234:	14000002 	b	1023c <android::bootable::BootControl::GetActiveBootSlot()@@Base+0xa0>
   10238:	2a1f03e0 	mov	w0, wzr
   1023c:	f9401688 	ldr	x8, [x20, #40]
   10240:	f85f83a9 	ldur	x9, [x29, #-8]
   10244:	eb09011f 	cmp	x8, x9
   10248:	54000101 	b.ne	10268 <android::bootable::BootControl::GetActiveBootSlot()@@Base+0xcc>  // b.any
   1024c:	a9444ff4 	ldp	x20, x19, [sp, #64]
   10250:	a9437bfd 	ldp	x29, x30, [sp, #48]
   10254:	910143ff 	add	sp, sp, #0x50
   10258:	d50323bf 	autiasp
   1025c:	d65f03c0 	ret
   10260:	52800240 	mov	w0, #0x12                  	// #18
   10264:	94003b37 	bl	1ef40 <abort@plt>
   10268:	94003b42 	bl	1ef70 <__stack_chk_fail@plt>

