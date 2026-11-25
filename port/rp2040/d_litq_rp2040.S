/*
 * d_litq_rp2040.S -- QuakeOG (Agent AR): one lit span of the Fast and Turbo
 * drawer, litq_span_c of engine/d_lit_q.h in hand-scheduled Thumb for the
 * M0+, in RAM (.time_critical) so the XIP cache's misses on texels and
 * colour map rows never refetch it.
 *
 *   void litq_span_asm (litq_span_t *c);
 *
 * Per sub-span (Agent AH's): the length from how fast 1/z changes, the end
 * point by one divide on the SIO divider (whose 8 cycles the shifts of Ns
 * and Nt fill), the steps by a shift or two signed divides. Then the runs
 * in two passes. Pass 1 blends the light at each run's ends with every
 * coordinate shifted by the mip level (so one blend serves every level)
 * and writes each run's light start and step. Pass 2 sets interpolator 1
 * from them and draws the run: interpolator 0 gives the texel addresses
 * and carries S and T from run to run, interpolator 1 the colour map
 * rows. A pixel is two single-cycle SIO reads, two byte loads and a store
 * (8 cycles), and its z a shift, a store and an add (4 more). A run's
 * pixels are an unrolled sequence entered at slot 8 - k.
 *
 * The board checks this routine against litq_span_c once at start-up
 * (D_QogLitQCheck) and uses the C one if they differ.
 */
	.syntax unified
	.cpu cortex-m0plus
	.thumb

	@ litq_span_t (engine/d_lit_q.h)
	.equ	C_D, 0
	.equ	C_NS, 4
	.equ	C_NT, 8
	.equ	C_DU, 12
	.equ	C_SU, 16
	.equ	C_TU, 20
	.equ	C_M, 24
	.equ	C_COUNT, 28
	.equ	C_PD, 32
	.equ	C_PZ, 36
	.equ	C_IZI, 40
	.equ	C_IZS, 44
	.equ	C_SHARE, 48
	.equ	C_SURF, 52		@ the surface's litq_surf_t (each core's copy has its own)
	.equ	O_S, 56			@ the assembly's own: s and t of the sub-span,
	.equ	O_T, 60
	.equ	O_SN, 64		@   at its end,
	.equ	O_TN, 68
	.equ	O_SS, 72		@   their steps,
	.equ	O_TS, 76
	.equ	O_SC, 80		@   its pixels and log2 of its full length,
	.equ	O_SH, 84
	.equ	O_PAIRS, 88		@   each run's light step and start << 16 (4 runs)

	@ litq_surf_t
	.equ	F_LB, 0
	.equ	F_RB, 4
	.equ	F_MIP, 8
	.equ	F_TSH, 12
	.equ	F_RECIP, 16
	.equ	F_SSW, 20
	.equ	F_TSW, 24
	.equ	F_EXTS, 28
	.equ	F_EXTT, 32
	.equ	F_SOFF, 36
	.equ	F_TOFF, 40

	@ stack frame
	.equ	K_CTX, 0
	.equ	K_N, 4			@ pass 1: pixels left
	.equ	K_SHARE, 8
	.equ	K_TMP, 12		@ pass 1: k - 1, or the shared light end

	@ SIO
	.equ	SIO, 0xD0000000
	.equ	DIV_UDIVIDEND, 0x60
	.equ	DIV_UDIVISOR, 0x64
	.equ	DIV_SDIVIDEND, 0x68
	.equ	DIV_SDIVISOR, 0x6C
	.equ	DIV_QUOTIENT, 0x70
	.equ	SIO_INTERP, 0xD0000080	@ INTERP0, INTERP1 at + 0x40
	.equ	I0_ACC0, 0x00
	.equ	I0_ACC1, 0x04
	.equ	I0_BASE0, 0x08
	.equ	I0_BASE1, 0x0C
	.equ	I0_FULL, 0x1C
	.equ	I1_ACC0, 0x40
	.equ	I1_BASE0, 0x48
	.equ	I1_FULL, 0x5C

	/*
	 * BLEND: id's four-sample light at X (r0), Y (r3), both shifted by the
	 * mip level, into r0. r12 = the grid's row bytes, lr = its base.
	 * Uses r3 to r6, keeps r1, r2, r7 to r11.
	 */
	.macro	BLEND
	lsls	r6, r3, #12
	lsrs	r6, r6, #24		@ fy
	lsrs	r3, r3, #20		@ row
	mov	r4, r12
	muls	r3, r4
	mov	r4, lr
	adds	r4, r3
	lsrs	r3, r0, #20		@ column
	lsls	r3, r3, #2
	adds	r4, r3			@ the cell
	lsls	r5, r0, #12
	lsrs	r5, r5, #24		@ fx
	ldr	r0, [r4, #0]
	ldr	r3, [r4, #4]
	subs	r3, r0
	muls	r3, r5
	asrs	r3, r3, #8
	adds	r0, r3			@ top
	add	r4, r12
	ldr	r3, [r4, #4]
	ldr	r4, [r4, #0]
	subs	r3, r4
	muls	r3, r5
	asrs	r3, r3, #8
	adds	r4, r3			@ bottom
	subs	r4, r0
	muls	r4, r6
	asrs	r4, r4, #8
	adds	r0, r4
	.endm

	.macro	ZPIX i
	ldr	r4, [r0, #I0_FULL]	@ the texel's address, S and T stepped
	ldr	r5, [r0, #I1_FULL]	@ the colour map row, the light stepped
	ldrb	r4, [r4]
	ldrb	r4, [r5, r4]
	strb	r4, [r1, #\i]
	asrs	r5, r3, #19
	strb	r5, [r2, #\i]		@ z = izi >> 19
	add	r3, r12			@ izi += izistep
	.endm

	.macro	PIX i
	ldr	r4, [r0, #I0_FULL]
	ldr	r5, [r0, #I1_FULL]
	ldrb	r4, [r4]
	ldrb	r4, [r5, r4]
	strb	r4, [r1, #\i]
	.endm

	.macro	WAIT8
	b	1f
1:	b	1f
1:	b	1f
1:	b	1f
1:
	.endm

	.section .text.litq_span_asm, "ax"
	.align	2

/*
 * endpt: the divisor's shift kept from the last point (r7 = the span), the
 * reciprocal of D's top 17 bits, then s and t into r0 and r1, clamped to
 * [lo (r6), the extent] as id clamps them (LITQ_NORM, LITQ_COORD).
 * Uses r2 to r5.
 */
	.thumb_func
	.type	litq_endpt, %function
litq_endpt:
	ldr	r0, [r7, #C_D]
	ldr	r1, [r7, #C_M]
	movs	r2, r0
	lsrs	r2, r1			@ D >> m
	movs	r3, #1
	lsls	r3, #17
1:	cmp	r2, r3
	blo	2f
	adds	r1, #1			@ D >> m at 2^17 or more: m + 1
	lsrs	r2, #1
	b	1b
2:	lsrs	r3, #1
3:	cmp	r1, #0
	beq	4f
	cmp	r2, r3
	bhs	4f
	subs	r1, #1			@ below 2^16: m - 1
	movs	r2, r0
	lsrs	r2, r1
	b	3b
4:	str	r1, [r7, #C_M]
	cmp	r2, #0
	bne	5f
	movs	r2, #1
5:	ldr	r3, =SIO
	movs	r4, #0
	mvns	r4, r4
	str	r4, [r3, #DIV_UDIVIDEND]
	str	r2, [r3, #DIV_UDIVISOR]
	ldr	r4, [r7, #C_NS]
	asrs	r4, r1			@ Ns >> m
	ldr	r5, [r7, #C_NT]
	asrs	r5, r1			@ Nt >> m
	ldr	r1, [r7, #C_SURF]
	nop
	ldr	r3, [r3, #DIV_QUOTIENT]	@ (2^32 - 1) / Dm, 8 cycles after the divisor
	@ s
	cmp	r4, #0
	ble	6f
	cmp	r4, r2
	bhs	7f
	muls	r4, r3
	ldr	r0, [r1, #F_SSW]
	lsrs	r4, r0
	b	8f
6:	movs	r4, #0
	b	8f
7:	ldr	r4, [r1, #F_EXTS]
8:	ldr	r0, [r1, #F_EXTS]
	cmp	r4, r0
	ble	9f
	movs	r4, r0
	b	10f
9:	cmp	r4, r6
	bge	10f
	movs	r4, r6
10:	@ t
	cmp	r5, #0
	ble	11f
	cmp	r5, r2
	bhs	12f
	muls	r5, r3
	ldr	r0, [r1, #F_TSW]
	lsrs	r5, r0
	b	13f
11:	movs	r5, #0
	b	13f
12:	ldr	r5, [r1, #F_EXTT]
13:	ldr	r0, [r1, #F_EXTT]
	cmp	r5, r0
	ble	14f
	movs	r5, r0
	b	15f
14:	cmp	r5, r6
	bge	15f
	movs	r5, r6
15:	movs	r0, r4
	movs	r1, r5
	bx	lr
	.ltorg
	.size	litq_endpt, . - litq_endpt

	.global	litq_span_asm
	.thumb_func
	.type	litq_span_asm, %function
litq_span_asm:
	push	{r4, r5, r6, r7, lr}
	mov	r4, r8
	mov	r5, r9
	mov	r6, r10
	mov	r7, r11
	push	{r4, r5, r6, r7}
	sub	sp, #16
	str	r0, [sp, #K_CTX]
	mov	r7, r0
	movs	r1, #0
	str	r1, [r7, #O_SS]		@ steps 0 until a sub-span sets them
	str	r1, [r7, #O_TS]
	movs	r6, #0
	bl	litq_endpt
	str	r0, [r7, #O_S]
	str	r1, [r7, #O_T]

.Lsub:
	@ r7 = the span. The longest sub-span this 1/z allows
	ldr	r0, [r7, #C_DU]
	asrs	r1, r0, #31
	eors	r0, r1
	subs	r0, r1			@ |DU|
	movs	r2, #3
	movs	r3, #1
	lsls	r3, #22
	cmp	r0, r3
	bhs	1f
	ldr	r1, [r7, #C_D]
	movs	r3, #200
	muls	r3, r0
	cmp	r1, r3
	bls	1f			@ D > |DU| * 16 / 0.08: 16
	movs	r2, #4
	lsls	r3, #1
	cmp	r1, r3
	bls	1f			@ D > |DU| * 32 / 0.08: 32
	movs	r2, #5
1:	movs	r3, #1
	lsls	r3, r2
	ldr	r4, [r7, #C_COUNT]
	cmp	r4, r3
	bge	2f
	movs	r3, r4
2:	subs	r4, r3
	str	r4, [r7, #C_COUNT]	@ pixels after this sub-span
	str	r3, [r7, #O_SC]
	str	r2, [r7, #O_SH]
	ldr	r0, [r7, #O_S]
	str	r0, [r7, #O_SN]		@ a last sub-span of one pixel keeps s and t
	ldr	r0, [r7, #O_T]
	str	r0, [r7, #O_TN]
	cmp	r4, #0
	bne	.Lmid
	subs	r3, #1
	bne	.Llast
	b	.Lruns
.Llast:
	@ the last sub-span: D, Ns and Nt at its last pixel
	ldr	r0, [r7, #C_DU]
	muls	r0, r3
	ldr	r1, [r7, #C_D]
	adds	r1, r0
	str	r1, [r7, #C_D]
	ldr	r0, [r7, #C_SU]
	muls	r0, r3
	ldr	r1, [r7, #C_NS]
	adds	r1, r0
	str	r1, [r7, #C_NS]
	ldr	r0, [r7, #C_TU]
	muls	r0, r3
	ldr	r1, [r7, #C_NT]
	adds	r1, r0
	str	r1, [r7, #C_NT]
	b	.Lclamp
.Lmid:
	@ a full sub-span: 2^sh steps on
	ldr	r0, [r7, #C_DU]
	lsls	r0, r2
	ldr	r1, [r7, #C_D]
	adds	r1, r0
	str	r1, [r7, #C_D]
	ldr	r0, [r7, #C_SU]
	lsls	r0, r2
	ldr	r1, [r7, #C_NS]
	adds	r1, r0
	str	r1, [r7, #C_NS]
	ldr	r0, [r7, #C_TU]
	lsls	r0, r2
	ldr	r1, [r7, #C_NT]
	adds	r1, r0
	str	r1, [r7, #C_NT]
.Lclamp:
	ldr	r0, [r7, #C_D]
	cmp	r0, #1
	bge	3f
	movs	r0, #1
	str	r0, [r7, #C_D]
	b	4f
3:	movs	r1, #1
	lsls	r1, #29
	cmp	r0, r1
	blt	4f
	asrs	r0, r0, #4		@ nearer along the span: four bits less, the ratio stays
	str	r0, [r7, #C_D]
	ldr	r0, [r7, #C_NS]
	asrs	r0, r0, #4
	str	r0, [r7, #C_NS]
	ldr	r0, [r7, #C_NT]
	asrs	r0, r0, #4
	str	r0, [r7, #C_NT]
	ldr	r0, [r7, #C_DU]
	asrs	r0, r0, #4
	str	r0, [r7, #C_DU]
	ldr	r0, [r7, #C_SU]
	asrs	r0, r0, #4
	str	r0, [r7, #C_SU]
	ldr	r0, [r7, #C_TU]
	asrs	r0, r0, #4
	str	r0, [r7, #C_TU]
4:	movs	r6, #8
	bl	litq_endpt
	str	r0, [r7, #O_SN]
	str	r1, [r7, #O_TN]
	ldr	r2, [r7, #O_S]
	subs	r0, r2			@ snext - s
	ldr	r2, [r7, #O_T]
	subs	r1, r2			@ tnext - t
	ldr	r4, [r7, #C_COUNT]
	cmp	r4, #0
	beq	5f
	ldr	r2, [r7, #O_SH]
	asrs	r0, r2
	asrs	r1, r2
	b	6f
5:	ldr	r2, [r7, #O_SC]		@ the last: divided by its pixels less one
	subs	r2, #1
	ldr	r3, =SIO
	str	r0, [r3, #DIV_SDIVIDEND]
	str	r2, [r3, #DIV_SDIVISOR]
	WAIT8
	ldr	r0, [r3, #DIV_QUOTIENT]
	str	r1, [r3, #DIV_SDIVIDEND]
	str	r2, [r3, #DIV_SDIVISOR]
	WAIT8
	ldr	r1, [r3, #DIV_QUOTIENT]
6:	str	r0, [r7, #O_SS]
	str	r1, [r7, #O_TS]

.Lruns:
	@ interpolator 0: S, T << tshift and their steps for the whole sub-span
	ldr	r5, [r7, #C_SURF]
	ldr	r0, [r7, #O_S]
	ldr	r1, [r5, #F_SOFF]
	adds	r0, r1			@ S
	ldr	r2, [r7, #O_T]
	ldr	r1, [r5, #F_TOFF]
	adds	r2, r1			@ T
	ldr	r3, =SIO_INTERP
	str	r0, [r3, #I0_ACC0]
	ldr	r4, [r7, #O_SS]
	str	r4, [r3, #I0_BASE0]
	ldr	r1, [r5, #F_TSH]
	movs	r6, r2
	lsls	r6, r1
	str	r6, [r3, #I0_ACC1]
	ldr	r6, [r7, #O_TS]
	lsls	r6, r1
	str	r6, [r3, #I0_BASE1]
	@ pass 1: the coordinates and steps shifted by the mip level
	ldr	r1, [r5, #F_MIP]
	lsls	r0, r1			@ S'
	lsls	r2, r1			@ T'
	lsls	r4, r1			@ ss'
	mov	r8, r4
	ldr	r6, [r7, #O_TS]
	lsls	r6, r1			@ ts'
	mov	r10, r6
	lsls	r4, #3
	mov	r3, r8
	subs	r4, r3
	mov	r9, r4			@ 7 ss'
	lsls	r6, #3
	mov	r3, r10
	subs	r6, r3
	mov	r11, r6			@ 7 ts'
	ldr	r4, [r5, #F_RB]
	mov	r12, r4
	ldr	r4, [r5, #F_LB]
	mov	lr, r4
	ldr	r4, [r7, #C_SHARE]
	str	r4, [sp, #K_SHARE]
	ldr	r4, [r7, #O_SC]
	str	r4, [sp, #K_N]
	mov	r1, r0			@ r1 = S', r2 = T'
	adds	r7, #O_PAIRS		@ r7 = the next run's light
.Lp1:
	mov	r0, r1
	mov	r3, r2
	BLEND				@ the run's light start
.Lp1la:
	ldr	r4, [sp, #K_N]
	cmp	r4, #1
	beq	.Lp1one
	str	r0, [r7]
	cmp	r4, #8
	ble	.Lp1last
	ldr	r5, [sp, #K_SHARE]
	cmp	r5, #0
	bne	.Lp1sh
	mov	r0, r1
	add	r0, r9
	mov	r3, r2
	add	r3, r11
	BLEND				@ at the run's 8th pixel
	ldr	r5, =9362		@ 65536 / 7
	b	.Lp1st
.Lp1sh:
	mov	r0, r1
	add	r0, r9
	add	r0, r8
	mov	r3, r2
	add	r3, r11
	add	r3, r10
	BLEND				@ at the next run's first pixel
	str	r0, [sp, #K_TMP]
	movs	r5, #1
	lsls	r5, #13			@ 65536 / 8
	b	.Lp1st
.Lp1last:
	subs	r4, #1			@ k - 1
	str	r4, [sp, #K_TMP]
	mov	r0, r8
	muls	r0, r4
	adds	r0, r1
	mov	r3, r10
	muls	r3, r4
	adds	r3, r2
	BLEND				@ at the run's last pixel
	ldr	r4, [sp, #K_TMP]
	lsls	r4, #2
	ldr	r5, [sp, #K_CTX]
	ldr	r5, [r5, #C_SURF]
	ldr	r5, [r5, #F_RECIP]
	ldr	r5, [r5, r4]		@ 65536 / (k - 1)
	b	.Lp1st
.Lp1one:
	str	r0, [r7]
	movs	r5, #0
.Lp1st:
	ldr	r3, [r7]
	subs	r0, r3
	muls	r0, r5			@ the light step
	lsls	r3, #16
	stmia	r7!, {r0, r3}
	ldr	r4, [sp, #K_N]
	subs	r4, #8
	str	r4, [sp, #K_N]
	ble	.Lp2
	add	r1, r9
	add	r1, r8			@ S' + 8 ss'
	add	r2, r11
	add	r2, r10			@ T' + 8 ts'
	ldr	r5, [sp, #K_SHARE]
	cmp	r5, #0
	bne	1f
	b	.Lp1
1:	ldr	r0, [sp, #K_TMP]	@ shared: the last run's end
	b	.Lp1la

.Lp2:
	@ pass 2: r0 = SIO_INTERP, r1 = pd, r2 = pz, r3 = izi, r12 = izistep,
	@ r6 = the runs' light, r7 = pixels left
	ldr	r7, [sp, #K_CTX]
	ldr	r1, [r7, #C_PD]
	ldr	r4, [r7, #C_IZS]
	mov	r12, r4
	ldr	r3, [r7, #C_IZI]
	ldr	r2, [r7, #C_PZ]
	movs	r6, r7
	adds	r6, #O_PAIRS
	ldr	r7, [r7, #O_SC]
	ldr	r0, =SIO_INTERP
	cmp	r2, #0
	beq	.Lp2n
	ldr	r4, =litq_zruns		@ the z runs in RAM
	bx	r4
	.thumb_func
	.global	litq_zruns_done
litq_zruns_done:
	ldr	r7, [sp, #K_CTX]
	str	r1, [r7, #C_PD]
	str	r2, [r7, #C_PZ]
	str	r3, [r7, #C_IZI]
	b	.Lnext

.Lp2n:
	ldmia	r6!, {r4, r5}
	str	r5, [r0, #I1_ACC0]
	str	r4, [r0, #I1_BASE0]
	movs	r4, #8
	subs	r4, r7
	bgt	1f
	movs	r4, #0
1:	subs	r1, r4
	movs	r5, #10			@ 10 bytes a slot
	muls	r4, r5
	adr	r5, .Lns
	adds	r5, r4
	adds	r5, #1
	bx	r5
	.align	2
.Lns:
	PIX	0
	PIX	1
	PIX	2
	PIX	3
	PIX	4
	PIX	5
	PIX	6
	PIX	7
	adds	r1, #8
	subs	r7, #8
	bgt	.Lp2n
	ldr	r7, [sp, #K_CTX]
	str	r1, [r7, #C_PD]

.Lnext:
	@ r7 = the span: the next sub-span starts at this one's end
	ldr	r0, [r7, #O_SN]
	str	r0, [r7, #O_S]
	ldr	r0, [r7, #O_TN]
	str	r0, [r7, #O_T]
	ldr	r0, [r7, #C_COUNT]
	cmp	r0, #0
	ble	1f
	b	.Lsub
1:	add	sp, #16
	pop	{r4, r5, r6, r7}
	mov	r8, r4
	mov	r9, r5
	mov	r10, r6
	mov	r11, r7
	pop	{r4, r5, r6, r7, pc}
	.ltorg
	.size	litq_span_asm, . - litq_span_asm

/*
 * litq_zruns: pass 2 for spans whose z the pixel loop writes, the hottest
 * loop, in RAM (.time_critical), so the XIP cache's texel and colour map
 * misses never refetch it. Entered from litq_span_asm with r0 =
 * SIO_INTERP, r1 = pd, r2 = pz, r3 = izi, r12 = izistep, r6 = the runs'
 * light, r7 = pixels, returns to litq_zruns_done with r1 to r3 advanced.
 * The rest of the routine runs from flash.
 */
	.section .time_critical.litq_zruns, "ax"
	.align	2
	.global	litq_zruns
	.thumb_func
	.type	litq_zruns, %function
litq_zruns:
	ldmia	r6!, {r4, r5}
	str	r5, [r0, #I1_ACC0]
	str	r4, [r0, #I1_BASE0]
	movs	r4, #8
	subs	r4, r7			@ 8 - k for a last run of k
	bgt	1f
	movs	r4, #0
1:	subs	r1, r4
	subs	r2, r4
	lsls	r4, #4			@ 16 bytes a slot
	adr	r5, .Lzs
	adds	r5, r4
	adds	r5, #1
	bx	r5
	.align	2
.Lzs:
	ZPIX	0
	ZPIX	1
	ZPIX	2
	ZPIX	3
	ZPIX	4
	ZPIX	5
	ZPIX	6
	ZPIX	7
	adds	r1, #8
	adds	r2, #8
	subs	r7, #8
	bgt	litq_zruns
	ldr	r4, =litq_zruns_done	@ back to flash with r1 to r3
	bx	r4
	.ltorg
	.size	litq_zruns, . - litq_zruns

