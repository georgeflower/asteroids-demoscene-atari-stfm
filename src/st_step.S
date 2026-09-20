| The hot loops of game_step, in assembly. The C reference versions are game_update_rocks_ref,
| game_update_bullets_ref and game_find_bullet_hit_ref in game.c (the tests compare them with these).
| Field offsets come from the compiler (build/asm_offsets.inc).
#include "asm_offsets.inc"

	.text

| Bring a register into 0 .. max - 1 by adding or subtracting max, and set d2 to 1 if that took anything.
	.macro	WRAP reg, max
1:	tst.l	\reg
	bpl.s	2f
	add.l	#\max,\reg
	moveq	#1,%d2
	bra.s	1b
2:	cmp.l	#\max,\reg
	blt.s	3f
	sub.l	#\max,\reg
	moveq	#1,%d2
	bra.s	2b
3:
	.endm

| void st_update_rocks(GameState *state)
| Every active rock: move, spin, wrap round the world; a rock that wrapped gets nudged towards the ship in C.
	.globl	_st_update_rocks
_st_update_rocks:
	movem.l	%d2-%d7/%a2-%a3,-(%sp)
	move.l	36(%sp),%a2
	lea	GS_ASTEROIDS(%a2),%a3
	moveq	#GA_MAX-1,%d7
rockloop:
	tst.b	GA_ACTIVE(%a3)
	beq.s	nextrock
	move.l	GA_X(%a3),%d0
	add.l	GA_VX(%a3),%d0
	move.l	GA_Y(%a3),%d1
	add.l	GA_VY(%a3),%d1
	move.w	GA_SPIN(%a3),%d3
	add.w	%d3,GA_ANGLE(%a3)
	moveq	#0,%d2
	WRAP	%d0,WORLD_MAX_X
	WRAP	%d1,WORLD_MAX_Y
	move.l	%d0,GA_X(%a3)
	move.l	%d1,GA_Y(%a3)
	tst.l	%d2
	beq.s	nextrock
	move.l	%a3,-(%sp)
	move.l	%a2,-(%sp)
	jsr	_game_nudge_rock
	addq.l	#8,%sp
nextrock:
	lea	GA_SIZEOF(%a3),%a3
	dbra	%d7,rockloop
	movem.l	(%sp)+,%d2-%d7/%a2-%a3
	rts

| void st_update_bullets(GameState *state)
| Every active bullet: move, wrap, count its life down and retire it at zero.
	.globl	_st_update_bullets
_st_update_bullets:
	movem.l	%d2-%d3/%a2,-(%sp)
	move.l	16(%sp),%a2
	lea	GS_BULLETS(%a2),%a2
	moveq	#GB_MAX-1,%d3
bulletloop:
	tst.b	GB_ACTIVE(%a2)
	beq.s	nextbullet
	move.l	GB_X(%a2),%d0
	add.l	GB_VX(%a2),%d0
	move.l	GB_Y(%a2),%d1
	add.l	GB_VY(%a2),%d1
	WRAP	%d0,WORLD_MAX_X
	WRAP	%d1,WORLD_MAX_Y
	move.l	%d0,GB_X(%a2)
	move.l	%d1,GB_Y(%a2)
	tst.b	GB_LIFE(%a2)
	beq.s	1f
	subq.b	#1,GB_LIFE(%a2)
1:	tst.b	GB_LIFE(%a2)
	bne.s	nextbullet
	clr.b	GB_ACTIVE(%a2)
nextbullet:
	lea	GB_SIZEOF(%a2),%a2
	dbra	%d3,bulletloop
	movem.l	(%sp)+,%d2-%d3/%a2
	rts

| long st_find_bullet_hit(const GameState *state, long first_bullet, const unsigned short *reach, long *rock)
| The first bullet at or after first_bullet that touches a rock, and the first rock (in slot order) it touches;
| the same circle test as within_radius in game.c: dx, dy in 12.4 fixed point, no farther than the reach for
| the size of the rock along either axis, and dx * dx + dy * dy < reach * reach. Returns -1 when there is none.
	.globl	_st_find_bullet_hit
_st_find_bullet_hit:
	movem.l	%d2-%d7/%a2-%a4,-(%sp)		| 36 bytes; arguments from 40(%sp)
	move.l	40(%sp),%a2			| state
	move.l	44(%sp),%d4			| bullet index
	move.l	48(%sp),%a1			| reach table
	lea	GS_BULLETS(%a2),%a4
	move.l	%d4,%d0
	mulu.w	#GB_SIZEOF,%d0
	adda.l	%d0,%a4				| a4 = bullet
nextbulletfor:
	cmp.l	#GB_MAX,%d4
	bge	nohit
	tst.b	GB_ACTIVE(%a4)
	beq	skipbullet
	move.l	GB_X(%a4),%d2
	move.l	GB_Y(%a4),%d3
	lea	GS_ASTEROIDS(%a2),%a3
	moveq	#GA_MAX-1,%d5
hitrock:
	tst.b	GA_ACTIVE(%a3)
	beq.s	nextrockfor
	move.l	%d2,%d0
	sub.l	GA_X(%a3),%d0
	asr.l	#8,%d0
	asr.l	#4,%d0				| dx (12.4), low word
	move.l	%d3,%d1
	sub.l	GA_Y(%a3),%d1
	asr.l	#8,%d1
	asr.l	#4,%d1				| dy
	moveq	#0,%d7
	move.b	GA_SIZE(%a3),%d7
	add.w	%d7,%d7
	move.w	0(%a1,%d7.w),%d6		| r
	move.w	%d6,%d7
	neg.w	%d7				| -r
	cmp.w	%d6,%d0
	bgt.s	nextrockfor
	cmp.w	%d7,%d0
	blt.s	nextrockfor
	cmp.w	%d6,%d1
	bgt.s	nextrockfor
	cmp.w	%d7,%d1
	blt.s	nextrockfor
	muls.w	%d0,%d0
	muls.w	%d1,%d1
	add.l	%d1,%d0
	muls.w	%d6,%d6
	cmp.l	%d6,%d0
	blt.s	foundhit
nextrockfor:
	lea	GA_SIZEOF(%a3),%a3
	dbra	%d5,hitrock
skipbullet:
	addq.l	#1,%d4
	lea	GB_SIZEOF(%a4),%a4
	bra	nextbulletfor
foundhit:
	move.l	%a3,%d0
	lea	GS_ASTEROIDS(%a2),%a0
	sub.l	%a0,%d0
	divu.w	#GA_SIZEOF,%d0			| rock index
	and.l	#0xffff,%d0
	move.l	52(%sp),%a0
	move.l	%d0,(%a0)
	move.l	%d4,%d0
	bra.s	hitdone
nohit:
	moveq	#-1,%d0
hitdone:
	movem.l	(%sp)+,%d2-%d7/%a2-%a4
	rts
