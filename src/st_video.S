	.text
	.globl	_st_clear_buffer
_st_clear_buffer:
	movem.l	%d2-%d6/%a2,-(%sp)
	move.l	28(%sp),%a0
	lea	32000(%a0),%a0
	moveq	#0,%d0
	moveq	#0,%d1
	moveq	#0,%d2
	moveq	#0,%d3
	moveq	#0,%d4
	moveq	#0,%d5
	move.l	%d0,%a1
	move.l	%d0,%a2
	move.w	#999,%d6		| 1000 x 32 bytes
1:
	movem.l	%d0-%d5/%a1-%a2,-(%a0)
	dbra	%d6,1b
	movem.l	(%sp)+,%d2-%d6/%a2
	rts

| Clear a rectangle of 16-pixel groups in a low-resolution screen.
| void st_clear_rect(unsigned char *buf, long group0, long group1, long y0, long y1)   (inclusive)
| Rows of 1-4 groups use movem.l stores (d0-d5 are zero); wider rects use a loop.
	.globl	_st_clear_rect
_st_clear_rect:
	movem.l	%d2-%d7,-(%sp)
	move.l	28(%sp),%a0
	move.l	32(%sp),%d0
	move.l	36(%sp),%d1
	move.l	40(%sp),%d2
	move.l	44(%sp),%d7
	sub.l	%d0,%d1			| groups - 1
	sub.l	%d2,%d7			| rows - 1
	mulu.w	#160,%d2
	add.l	%d2,%a0
	lsl.l	#3,%d0
	add.l	%d0,%a0
	move.l	%d1,%d6			| d6 = groups - 1
	moveq	#0,%d0
	moveq	#0,%d1
	moveq	#0,%d2
	moveq	#0,%d3
	moveq	#0,%d4
	moveq	#0,%d5
	cmp.w	#3,%d6
	bhi.s	wide
	add.w	%d6,%d6
	move.w	rowtable(%pc,%d6.w),%d6
	jmp	rowtable(%pc,%d6.w)
rowtable:
	.word	row1-rowtable, row2-rowtable, row3-rowtable, row4-rowtable
row1:	movem.l	%d0-%d1,(%a0)
	lea	160(%a0),%a0
	dbra	%d7,row1
	bra.s	clearend
row2:	movem.l	%d0-%d3,(%a0)
	lea	160(%a0),%a0
	dbra	%d7,row2
	bra.s	clearend
row3:	movem.l	%d0-%d5,(%a0)
	lea	160(%a0),%a0
	dbra	%d7,row3
	bra.s	clearend
row4:	movem.l	%d0-%d5,(%a0)
	movem.l	%d0-%d1,24(%a0)
	lea	160(%a0),%a0
	dbra	%d7,row4
	bra.s	clearend
wide:	move.l	#160,%a1		| more than four groups: generic loop
	move.l	%d6,%d3
	addq.l	#1,%d3
	lsl.l	#3,%d3
	sub.l	%d3,%a1
1:	move.w	%d6,%d2
2:	move.l	%d0,(%a0)+
	move.l	%d0,(%a0)+
	dbra	%d2,2b
	add.l	%a1,%a0
	dbra	%d7,1b
clearend:
	movem.l	(%sp)+,%d2-%d7
	rts

| Closed polygon outline in one bitplane of a low-resolution screen.
| void st_draw_poly_plane(unsigned char *buf, const short *points, long count, long plane_offset)
| points are x,y word pairs, all inside 0..319 x 0..199. plane_offset is 0, 2, 4 or 6.
| Same Bresenham as st_draw_line_plane, but one call per outline and a tight
| inner loop for each of the four directions.
	.globl	_st_draw_poly_plane
_st_draw_poly_plane:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)
	move.l	48(%sp),%a2
	add.l	60(%sp),%a2		| a2 = buffer + plane offset
	move.l	52(%sp),%a3		| a3 = current point
	move.l	%a3,%a5			| a5 = first point (to close the outline)
	move.l	56(%sp),%d0
	lsl.l	#2,%d0
	lea	0(%a3,%d0.l),%a4	| a4 = end of the point list
edge:
	move.w	(%a3),%d5		| x0
	move.w	2(%a3),%d6		| y0
	lea	4(%a3),%a6		| next point, wrapping to the first
	cmp.l	%a4,%a6
	bcs.s	1f
	move.l	%a5,%a6
1:	move.w	(%a6),%d1
	move.w	2(%a6),%d2
	sub.w	%d5,%d1			| dx
	sub.w	%d6,%d2			| dy
	move.w	%d6,%d3
	mulu.w	#160,%d3
	lea	0(%a2,%d3.l),%a0
	move.w	%d5,%d3
	lsr.w	#4,%d3
	lsl.w	#3,%d3
	adda.w	%d3,%a0
	moveq	#15,%d3
	and.w	%d5,%d3
	move.w	#0x8000,%d0
	lsr.w	%d3,%d0			| pixel mask
	movea.w	#160,%a1
	tst.w	%d2
	bpl.s	2f
	neg.w	%d2
	movea.w	#-160,%a1
2:	moveq	#0,%d4
	tst.w	%d1
	bpl.s	3f
	neg.w	%d1
	moveq	#1,%d4
3:	cmp.w	%d2,%d1
	bcs.w	ymajor
	move.w	%d1,%d7
	move.w	%d1,%d3
	lsr.w	#1,%d3
	tst.w	%d4
	bne.s	xleft
xright:
	or.w	%d0,(%a0)
	sub.w	%d2,%d3
	bpl.s	4f
	add.w	%d1,%d3
	adda.w	%a1,%a0
4:	ror.w	#1,%d0
	bcc.s	5f
	addq.l	#8,%a0
5:	dbra	%d7,xright
	bra.w	edgedone
xleft:
	or.w	%d0,(%a0)
	sub.w	%d2,%d3
	bpl.s	6f
	add.w	%d1,%d3
	adda.w	%a1,%a0
6:	rol.w	#1,%d0
	bcc.s	7f
	subq.l	#8,%a0
7:	dbra	%d7,xleft
	bra.w	edgedone
ymajor:
	move.w	%d2,%d7
	move.w	%d2,%d3
	lsr.w	#1,%d3
	tst.w	%d4
	bne.s	yleft
yright:
	or.w	%d0,(%a0)
	sub.w	%d1,%d3
	bpl.s	8f
	add.w	%d2,%d3
	ror.w	#1,%d0
	bcc.s	8f
	addq.l	#8,%a0
8:	adda.w	%a1,%a0
	dbra	%d7,yright
	bra.s	edgedone
yleft:
	or.w	%d0,(%a0)
	sub.w	%d1,%d3
	bpl.s	9f
	add.w	%d2,%d3
	rol.w	#1,%d0
	bcc.s	9f
	subq.l	#8,%a0
9:	adda.w	%a1,%a0
	dbra	%d7,yleft
edgedone:
	addq.l	#4,%a3
	cmp.l	%a4,%a3
	bcs.w	edge
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
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

| Single-bitplane variant: colour is one of 1, 2, 4, 8, so one indexed OR per pixel.
| void st_draw_line_plane(unsigned char *buf, long x0, long y0, long x1, long y1, long plane_offset)
| plane_offset is the byte offset of the plane within a 16-pixel group: 0, 2, 4 or 6.
	.macro	PUTPLANE
	or.w	%d0,0(%a0,%d5.w)
	.endm

	.globl	_st_draw_line_plane
_st_draw_line_plane:
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
3:	PUTPLANE
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
8:	PUTPLANE
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
