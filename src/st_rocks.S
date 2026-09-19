| All the rocks in one loop: what game_render's draw_asteroid does for a rock that lies wholly inside the
| playing field, without going through C. Field offsets come from the compiler (build/asm_offsets.inc).
|
| void st_draw_rocks(GameState *state, unsigned char *buf, DirtyRect *rects, int *count, unsigned char *full,
|                    long max_rects)
|
| For every active rock: screen position from the world position (as screen_x/screen_y in game.c); bring the
| cached outline up to date (game_prepare_rock) and the prepared edge list (platform_prepare_rock) when the
| orientation changed; if the outline lies inside the field, add its dirty rectangle (grown by a pixel and
| clamped to the field, like platform_mark_dirty) and draw it with st_draw_rock; render_pending is cleared
| for those and set for the rest, which game_render draws through the general C route.
#include "asm_offsets.inc"

	.text
	.globl	_st_draw_rocks
_st_draw_rocks:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)		| 44 bytes; arguments start at 48(%sp)
	move.l	48(%sp),%a2			| a2 = state
	move.l	52(%sp),%a6			| a6 = screen buffer
	move.l	56(%sp),%a4			| a4 = dirty rectangle array
	move.l	60(%sp),%a5			| a5 = pointer to the rectangle count
	lea	GS_ASTEROIDS(%a2),%a3		| a3 = current rock
	moveq	#GA_MAX-1,%d7
rockloop:
	tst.b	GA_ACTIVE(%a3)
	beq	nextrock

	| screen position: field + ((world >> 11) * scale >> 13)
	move.l	GA_X(%a3),%d2
	asr.l	#8,%d2
	asr.l	#3,%d2
	muls.w	GS_X_SCALE(%a2),%d2
	asr.l	#8,%d2
	asr.l	#5,%d2
	add.w	GS_FIELD_X(%a2),%d2		| d2.w = centre x
	move.l	GA_Y(%a3),%d3
	asr.l	#8,%d3
	asr.l	#3,%d3
	muls.w	GS_Y_SCALE(%a2),%d3
	asr.l	#8,%d3
	asr.l	#5,%d3
	add.w	GS_FIELD_Y(%a2),%d3		| d3.w = centre y

	| cached outline for this orientation (angle >> 10)
	move.w	GA_ANGLE(%a3),%d4
	lsr.w	#8,%d4
	lsr.w	#2,%d4
	tst.b	GA_CACHE_VALID(%a3)
	beq.s	prepare
	cmp.b	GA_CACHE_INDEX(%a3),%d4
	beq.s	prepared
prepare:
	move.l	%a3,-(%sp)
	move.l	%a2,-(%sp)
	jsr	_game_prepare_rock
	addq.l	#8,%sp
prepared:
	tst.b	GA_DRAW_CACHE_VALID(%a3)
	bne.s	edgesready
	move.l	%a3,-(%sp)
	jsr	_platform_prepare_rock
	addq.l	#4,%sp
edgesready:

	| does the whole outline lie inside the field?
	move.b	GA_BOUND_X0(%a3),%d0
	ext.w	%d0
	add.w	%d2,%d0				| d0 = left
	cmp.w	GS_FIELD_X(%a2),%d0
	blt	notinside
	move.b	GA_BOUND_Y0(%a3),%d5
	ext.w	%d5
	add.w	%d3,%d5				| d5 = top
	cmp.w	GS_FIELD_Y(%a2),%d5
	blt	notinside
	move.b	GA_BOUND_X1(%a3),%d4
	ext.w	%d4
	add.w	%d2,%d4				| d4 = right
	move.w	GS_FIELD_X(%a2),%d1
	add.w	GS_FIELD_W(%a2),%d1
	cmp.w	%d1,%d4
	bge	notinside
	move.b	GA_BOUND_Y1(%a3),%d6
	ext.w	%d6
	add.w	%d3,%d6				| d6 = bottom
	move.w	GS_FIELD_Y(%a2),%d1
	add.w	GS_FIELD_H(%a2),%d1
	cmp.w	%d1,%d6
	bge	notinside

	| dirty rectangle: one pixel larger each way, clamped to the field
	move.l	(%a5),%d1			| count so far
	cmp.l	68(%sp),%d1
	bge.s	rectfull
	lsl.l	#3,%d1
	lea	0(%a4,%d1.l),%a0
	subq.w	#1,%d0
	cmp.w	GS_FIELD_X(%a2),%d0
	bge.s	1f
	move.w	GS_FIELD_X(%a2),%d0
1:	move.w	%d0,(%a0)+			| x0
	subq.w	#1,%d5
	cmp.w	GS_FIELD_Y(%a2),%d5
	bge.s	2f
	move.w	GS_FIELD_Y(%a2),%d5
2:	move.w	%d5,(%a0)+			| y0
	addq.w	#1,%d4
	move.w	GS_FIELD_X(%a2),%d1
	add.w	GS_FIELD_W(%a2),%d1
	subq.w	#1,%d1				| last column of the field
	cmp.w	%d1,%d4
	ble.s	3f
	move.w	%d1,%d4
3:	move.w	%d4,(%a0)+			| x1
	addq.w	#1,%d6
	move.w	GS_FIELD_Y(%a2),%d1
	add.w	GS_FIELD_H(%a2),%d1
	subq.w	#1,%d1				| last row of the field
	cmp.w	%d1,%d6
	ble.s	4f
	move.w	%d1,%d6
4:	move.w	%d6,(%a0)+			| y1
	addq.l	#1,(%a5)
	bra.s	drawrock
rectfull:
	move.l	64(%sp),%a0
	move.b	#1,(%a0)			| too many rectangles: the whole field gets erased instead
drawrock:
	moveq	#0,%d0
	move.b	GA_SIZE(%a3),%d0
	neg.w	%d0
	addq.w	#4,%d0
	add.w	%d0,%d0				| plane offset (4 - size) * 2
	move.l	%d0,-(%sp)
	pea	GA_DRAW_CACHE(%a3)
	move.w	%d3,%d0
	ext.l	%d0
	move.l	%d0,-(%sp)			| centre y
	move.w	%d2,%d0
	ext.l	%d0
	move.l	%d0,-(%sp)			| centre x
	move.l	%a6,-(%sp)			| buffer
	jsr	_st_draw_rock
	lea	20(%sp),%sp
	clr.b	GA_RENDER_PENDING(%a3)
	bra.s	nextrock
notinside:
	move.b	#1,GA_RENDER_PENDING(%a3)
nextrock:
	lea	GA_SIZEOF(%a3),%a3
	dbra	%d7,rockloop
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts
