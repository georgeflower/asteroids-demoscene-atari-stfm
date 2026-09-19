| Sampling profiler for the emulator: MFP timer A fires about 10 kHz and stores the interrupted PC.
	.text
	.globl	_prof_isr
	.globl	_prof_ptr
	.globl	_prof_end
_prof_isr:
	move.l	%a0,-(%sp)
	move.l	_prof_ptr,%a0
	cmpa.l	_prof_end,%a0
	bcc.s	1f
	move.l	6(%sp),(%a0)+		| stacked PC (SR is at 4(%sp))
	move.l	%a0,_prof_ptr
1:	move.b	#0xdf,0xfffa0f		| clear timer A in-service bit
	move.l	(%sp)+,%a0
	rte

	.data
_prof_ptr:
	.long	0
_prof_end:
	.long	0
