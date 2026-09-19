	.text
	.globl	_st_clear_buffer
_st_clear_buffer:
	movem.l	%d0-%d1/%a0,-(%sp)
	move.l	16(%sp),%a0
	moveq	#0,%d0
	move.w	#7999,%d1
1:
	move.l	%d0,(%a0)+
	dbra	%d1,1b
	movem.l	(%sp)+,%d0-%d1/%a0
	rts

| Low-resolution 320x200, 4-plane line drawer.
| void st_draw_line_low(unsigned char *buf, long x0, long y0, long x1, long y1, long color)
	.macro	PUTPIXEL
	btst	#0,%d5
	beq.s	.Lp1\@
	or.w	%d0,(%a0)
.Lp1\@:	btst	#1,%d5
	beq.s	.Lp2\@
	or.w	%d0,2(%a0)
.Lp2\@:	btst	#2,%d5
	beq.s	.Lp3\@
	or.w	%d0,4(%a0)
.Lp3\@:	btst	#3,%d5
	beq.s	.Lp4\@
	or.w	%d0,6(%a0)
.Lp4\@:
	.endm

	.globl	_st_draw_line_low
_st_draw_line_low:
	movem.l	%d2-%d7/%a2,-(%sp)
	move.l	32(%sp),%a0
	move.l	36(%sp),%d6
	move.l	40(%sp),%d7
	move.l	44(%sp),%d1
	move.l	48(%sp),%d2
	move.l	52(%sp),%d5
	sub.l	%d6,%d1
	sub.l	%d7,%d2
	mulu.w	#160,%d7
	add.l	%d7,%a0
	move.l	%d6,%d3
	lsr.l	#4,%d3
	lsl.l	#3,%d3
	add.l	%d3,%a0
	moveq	#15,%d3
	and.l	%d6,%d3
	move.w	#0x8000,%d0
	lsr.w	%d3,%d0
	move.l	#160,%a1
	tst.l	%d2
	bpl.s	1f
	neg.l	%d2
	move.l	#-160,%a1
1:	moveq	#1,%d4
	tst.l	%d1
	bpl.s	2f
	neg.l	%d1
	moveq	#-1,%d4
2:	cmp.l	%d2,%d1
	blt.s	5f
	move.l	%d1,%d3
	lsr.l	#1,%d3
	move.l	%d1,%d7
3:	PUTPIXEL
	sub.l	%d2,%d3
	bpl.s	4f
	add.l	%d1,%d3
	add.l	%a1,%a0
4:	tst.l	%d4
	bmi.s	6f
	lsr.w	#1,%d0
	bne.s	7f
	move.w	#0x8000,%d0
	addq.l	#8,%a0
	bra.s	7f
6:	lsl.w	#1,%d0
	bne.s	7f
	move.w	#1,%d0
	subq.l	#8,%a0
7:	dbra	%d7,3b
	bra.s	10f
5:	move.l	%d2,%d3
	lsr.l	#1,%d3
	move.l	%d2,%d7
8:	PUTPIXEL
	sub.l	%d1,%d3
	bpl.s	9f
	add.l	%d2,%d3
	tst.l	%d4
	bmi.s	11f
	lsr.w	#1,%d0
	bne.s	9f
	move.w	#0x8000,%d0
	addq.l	#8,%a0
	bra.s	9f
11:	lsl.w	#1,%d0
	bne.s	9f
	move.w	#1,%d0
	subq.l	#8,%a0
9:	add.l	%a1,%a0
	dbra	%d7,8b
10:	movem.l	(%sp)+,%d2-%d7/%a2
	rts
