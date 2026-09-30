00************** <android::bootable::BootControl::IsValidSlot(unsigned int)@@Base>:
   10598:	d503245f 	bti	c
   1059c:	71000c3f 	cmp	w1, #0x3
   105a0:	540000a8 	b.hi	105b4 <android::bootable::BootControl::IsValidSlot(unsigned int)@@Base+0x1c>  // b.pmore
   105a4:	b9402008 	ldr	w8, [x0, #32]
   105a8:	6b01011f 	cmp	w8, w1
   105ac:	1a9f97e0 	cset	w0, hi	// hi = pmore
   105b0:	d65f03c0 	ret
   105b4:	2a1f03e0 	mov	w0, wzr
   105b8:	d65f03c0 	ret

