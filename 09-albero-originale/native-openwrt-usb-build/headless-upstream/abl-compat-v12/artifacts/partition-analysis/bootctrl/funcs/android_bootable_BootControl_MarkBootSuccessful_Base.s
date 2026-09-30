00000000000100e4 <android::bootable::BootControl::MarkBootSuccessful()@@Base>:
   100e4:	d503233f 	paciasp
   100e8:	d10183ff 	sub	sp, sp, #0x60
   100ec:	a9037bfd 	stp	x29, x30, [sp, #48]
   100f0:	f90023f5 	str	x21, [sp, #64]
   100f4:	a9054ff4 	stp	x20, x19, [sp, #80]
   100f8:	9100c3fd 	add	x29, sp, #0x30
   100fc:	d53bd055 	mrs	x21, tpidr_el0
   10100:	f94016a8 	ldr	x8, [x21, #40]
   10104:	91002013 	add	x19, x0, #0x8
   10108:	aa0003f4 	mov	x20, x0
   1010c:	6f00e400 	movi	v0.2d, #0x0
   10110:	910003e1 	mov	x1, sp
   10114:	aa1303e0 	mov	x0, x19
   10118:	f81f83a8 	stur	x8, [x29, #-8]
   1011c:	ad0003e0 	stp	q0, q0, [sp]
   10120:	94003c90 	bl	1f360 <android::bootable::LoadBootloaderControl(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char> > const&, bootloader_control*)@plt>
   10124:	360001e0 	tbz	w0, #0, 10160 <android::bootable::BootControl::MarkBootSuccessful()@@Base+0x7c>
   10128:	b9402688 	ldr	w8, [x20, #36]
   1012c:	7100111f 	cmp	w8, #0x4
   10130:	54000302 	b.cs	10190 <android::bootable::BootControl::MarkBootSuccessful()@@Base+0xac>  // b.hs, b.nlast
   10134:	910003e9 	mov	x9, sp
   10138:	8b080528 	add	x8, x9, x8, lsl #1
   1013c:	79401909 	ldrh	w9, [x8, #12]
   10140:	5280120a 	mov	w10, #0x90                  	// #144
   10144:	910003e1 	mov	x1, sp
   10148:	aa1303e0 	mov	x0, x19
   1014c:	12187129 	and	w9, w9, #0xffffff1f
   10150:	2a0a0129 	orr	w9, w9, w10
   10154:	79001909 	strh	w9, [x8, #12]
   10158:	94003c8e 	bl	1f390 <android::bootable::UpdateAndSaveBootloaderControl(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char> > const&, bootloader_control*)@plt>
   1015c:	14000002 	b	10164 <android::bootable::BootControl::MarkBootSuccessful()@@Base+0x80>
   10160:	2a1f03e0 	mov	w0, wzr
   10164:	f94016a8 	ldr	x8, [x21, #40]
   10168:	f85f83a9 	ldur	x9, [x29, #-8]
   1016c:	eb09011f 	cmp	x8, x9
   10170:	54000141 	b.ne	10198 <android::bootable::BootControl::MarkBootSuccessful()@@Base+0xb4>  // b.any
   10174:	a9454ff4 	ldp	x20, x19, [sp, #80]
   10178:	f94023f5 	ldr	x21, [sp, #64]
   1017c:	a9437bfd 	ldp	x29, x30, [sp, #48]
   10180:	12000000 	and	w0, w0, #0x1
   10184:	910183ff 	add	sp, sp, #0x60
   10188:	d50323bf 	autiasp
   1018c:	d65f03c0 	ret
   10190:	52800240 	mov	w0, #0x12                  	// #18
   10194:	94003b6b 	bl	1ef40 <abort@plt>
   10198:	94003b76 	bl	1ef70 <__stack_chk_fail@plt>

