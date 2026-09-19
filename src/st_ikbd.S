| IKBD (keyboard) interrupt handler.
|
| The TOS BIOS keyboard path (Bconin) only ever delivers key make codes and
| Bconstat/Bconin on device 4 is not a usable way to read the IKBD, so a game
| that wants held-key state has to own the ACIA interrupt itself.  This handler
| replaces the MFP channel B bit 6 vector, records make/break state for every
| scan code in st_key_state[] and skips the mouse/joystick/status packets.
|
| Must be installed and removed from supervisor mode.

	.equ	ikbd_vec, 0x118		| MFP channel B bit 6 - IKBD/MIDI ACIA
	.equ	ierb, 0xfffffa09
	.equ	imrb, 0xfffffa15
	.equ	isrb, 0xfffffa11
	.equ	kbd_ctrl, 0xfffffc00
	.equ	kbd_data, 0xfffffc02
	.equ	midi_ctrl, 0xfffffc04
	.equ	midi_data, 0xfffffc06

	.text
	.globl	_st_ikbd_install
	.globl	_st_ikbd_remove

_st_ikbd_install:
	move.w	%sr,-(%sp)
	move.w	#0x2700,%sr
	move.l	ikbd_vec.w,old_ikbd
	clr.w	pkt_left
	move.l	#ikbd_isr,ikbd_vec.w
	bset	#6,ierb.w
	bset	#6,imrb.w
	move.w	(%sp)+,%sr
	rts

_st_ikbd_remove:
	move.w	%sr,-(%sp)
	move.w	#0x2700,%sr
	move.l	old_ikbd,ikbd_vec.w
	btst	#0,kbd_ctrl.w		| drop any byte we have not read yet
	beq.s	1f
	tst.b	kbd_data.w
1:	move.w	(%sp)+,%sr
	rts

| Packet lengths (bytes after the header): $F6 status 6, $F7 absolute mouse 5,
| $F8-$FB relative mouse 2, $FC clock 6, $FD joysticks 2, $FE/$FF joystick 1.
| Anything below $F6 is a key make/break code.
ikbd_isr:
	movem.l	%d0-%d2/%a0,-(%sp)
1:	btst	#0,midi_ctrl.w		| MIDI shares this interrupt: discard
	beq.s	2f
	move.b	midi_data.w,%d0
2:	btst	#0,kbd_ctrl.w		| receive buffer full?
	beq.s	9f
	moveq	#0,%d0
	move.b	kbd_data.w,%d0
	move.w	pkt_left,%d1
	beq.s	3f
	subq.w	#1,%d1			| inside a packet we ignore
	move.w	%d1,pkt_left
	bra.s	1b

3:	cmpi.b	#0xf6,%d0
	bcs.s	key_byte
	moveq	#1,%d1
	cmpi.b	#0xfe,%d0
	bcc.s	4f			| $FE/$FF: 1
	moveq	#2,%d1
	cmpi.b	#0xfd,%d0
	beq.s	4f			| $FD: 2
	moveq	#6,%d1
	cmpi.b	#0xfc,%d0
	beq.s	4f			| $FC: 6
	moveq	#2,%d1
	cmpi.b	#0xf8,%d0
	bcc.s	4f			| $F8-$FB: 2
	moveq	#5,%d1
	cmpi.b	#0xf7,%d0
	beq.s	4f			| $F7: 5
	moveq	#6,%d1			| $F6: 6
4:	move.w	%d1,pkt_left
	bra.s	1b

key_byte:
	move.w	%d0,%d1
	andi.w	#0x7f,%d1
	lea	_st_key_state,%a0
	btst	#7,%d0			| bit 7 set = key released
	seq	%d2
	andi.b	#1,%d2
	move.b	%d2,0(%a0,%d1.w)
	bra.s	1b

9:	bclr	#6,isrb.w		| MFP is in software end-of-interrupt mode
	movem.l	(%sp)+,%d0-%d2/%a0
	rte

	.bss
	.even
old_ikbd:	.ds.l	1
pkt_left:	.ds.w	1
	.even
