| IKBD (keyboard) interrupt handler.
|
| The TOS BIOS keyboard path (Bconin) only ever delivers key make codes and
| Bconstat/Bconin on device 4 is not a usable way to read the IKBD, so a game
| that wants held-key state has to own the ACIA interrupt itself.  This handler
| replaces the MFP channel B bit 6 vector, records make/break state for every
| scan code in st_key_state[], queues the make codes (st_key_queue, for typing), keeps
| the last joystick state of each port in st_joystick[] and skips the other packets.
| st_joystick bits: 0 up, 1 down, 2 left, 3 right, 7 fire; index 0 is the mouse port, 1 the joystick port.
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
	.globl	_st_joystick
	.globl	_st_key_queue
	.globl	_st_key_head
	.globl	_st_key_tail

_st_ikbd_install:
	move.w	%sr,-(%sp)
	move.w	#0x2700,%sr
	move.l	ikbd_vec.w,old_ikbd
	clr.w	pkt_left
	clr.w	joy_slot
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
	beq.w	9f
	moveq	#0,%d0
	move.b	kbd_data.w,%d0
	move.w	pkt_left,%d1
	beq.s	3f
	subq.w	#1,%d1			| inside a packet
	move.w	%d1,pkt_left
	bne.w	1b
	move.w	joy_slot,%d1		| the last byte of a joystick packet is the joystick state
	beq.w	1b
	lea	_st_joystick,%a0
	move.b	%d0,-1(%a0,%d1.w)
	clr.w	joy_slot
	bra.w	1b

3:	cmpi.b	#0xf6,%d0
	bcs.s	key_byte
	moveq	#1,%d1
	cmpi.b	#0xfe,%d0
	bcs.s	5f
	move.w	%d0,%d2			| $FE/$FF: 1 byte, the state of joystick 0 / 1
	subi.w	#0xfd,%d2
	move.w	%d2,joy_slot
	bra.s	4f
5:
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
	bra.w	1b

key_byte:
	move.w	%d0,%d1
	andi.w	#0x7f,%d1
	lea	_st_key_state,%a0
	btst	#7,%d0			| bit 7 set = key released
	seq	%d2
	andi.b	#1,%d2
	move.b	%d2,0(%a0,%d1.w)
	beq.w	1b			| a release: nothing to queue
	lea	_st_key_queue,%a0
	moveq	#0,%d2
	move.b	_st_key_head,%d2
	move.b	%d1,0(%a0,%d2.w)
	addq.b	#1,%d2
	andi.b	#15,%d2
	cmp.b	_st_key_tail,%d2
	beq.w	1b			| queue full: drop the key
	move.b	%d2,_st_key_head
	bra.w	1b

9:	bclr	#6,isrb.w		| MFP is in software end-of-interrupt mode
	movem.l	(%sp)+,%d0-%d2/%a0
	rte

	.bss
	.even
old_ikbd:	.ds.l	1
pkt_left:	.ds.w	1
joy_slot:	.ds.w	1
_st_joystick:	.ds.b	2
_st_key_queue:	.ds.b	16
_st_key_head:	.ds.b	1
_st_key_tail:	.ds.b	1
	.even
