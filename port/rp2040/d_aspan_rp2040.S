/*
 * d_aspan_rp2040.S -- QuakeOG (Agent AR): id's alias span drawer
 * (D_PolysetDrawSpans8, engine/d_polyse.c, Agent AI's QOG_ALIASSPAN_FAST
 * form) in hand-scheduled Thumb for the M0+, from flash.
 *
 *   void d_aspans_asm (spanpackage_t *p);
 *
 * It reads id's globals (d_aspancount, errorterm, the steps, the skin
 * width, acolormap) and writes back d_aspancount and errorterm, as the C does.
 *
 * GCC keeps about 14 values live through each pixel and spills them to the
 * stack. Here interpolator 1 holds the light (its FULL result is the colour
 * map row, cmap + (light & 0xFF00), and reading it steps the light), and
 * interpolator 0 the depth: in the one-byte z-buffer's two linear pieces
 * (z below 1024, or z from 1024 on with its near steps, both ends of the span
 * in the same piece, as the C tests) its FULL result is the stored depth
 * byte, lzi >> 19 or ((lzi - 2^26) >> 23) + 128, and reading it steps lzi.
 * Other spans read lzi >> 16 from the signed lane and apply D_ZQ per pixel.
 * The texel is ptex[(t >> 16) * skinwidth + (s >> 16)] as in the C.
 *
 * The board checks this against the C at start-up (D_QogAspanCheck) and
 * uses the C when they differ. Player colours (d_atranslate) take the C.
 */
	.syntax unified
	.cpu cortex-m0plus
	.thumb

	@ spanpackage_t (d_polyse.c)
	.equ	P_PDEST, 0
	.equ	P_PZ, 4
	.equ	P_COUNT, 8
	.equ	P_PTEX, 12
	.equ	P_SFRAC, 16
	.equ	P_TFRAC, 20
	.equ	P_LIGHT, 24
	.equ	P_ZI, 28
	.equ	P_SIZE, 32

	@ stack frame
	.equ	K_ACOUNT, 8
	.equ	K_ERR, 12

	.equ	SIO_INTERP, 0xD0000080	@ INTERP0, INTERP1 at + 0x40
	.equ	I0_ACC0, 0x00
	.equ	I0_ACC1, 0x04
	.equ	I0_BASE0, 0x08
	.equ	I0_BASE1, 0x0C
	.equ	I0_BASE2, 0x10
	.equ	I0_FULL, 0x1C
	.equ	I0_CTRL0, 0x2C
	.equ	I0_CTRL1, 0x30
	.equ	I1_ACC0, 0x40
	.equ	I1_ACC1, 0x44
	.equ	I1_BASE0, 0x48
	.equ	I1_BASE1, 0x4C
	.equ	I1_BASE2, 0x50
	.equ	I1_FULL, 0x5C
	.equ	I1_CTRL0, 0x6C
	.equ	I1_CTRL1, 0x70

	@ lane controls: ADD_RAW (bit 18), SIGNED (bit 15), mask msb << 10, lsb << 5, shift
	.equ	CTL_LIGHT, (1 << 18) | (15 << 10) | (8 << 5) | 0	@ light & 0xFF00
	.equ	CTL_Z19, (1 << 18) | (7 << 10) | 19			@ lzi >> 19, 8 bits
	.equ	CTL_Z23, (1 << 18) | (7 << 10) | 23			@ (lzi - 2^26) >> 23
	.equ	CTL_Z16, (1 << 18) | (1 << 15) | (15 << 10) | 16	@ lzi >> 16, signed
	.equ	CTL_OFF, 0

	/* LOAD: the span's pixel state from the package (lr), r5 = lcount:
	   r1 = pd, r12 = its end, r2 = pz, r3 = s, r4 = t, r8 = ptex, and the
	   light on interpolator 1 */
	.macro	LOAD
	mov	r6, lr
	ldr	r1, [r6, #P_PDEST]
	adds	r2, r1, r5
	mov	r12, r2
	ldr	r2, [r6, #P_PTEX]
	mov	r8, r2
	ldr	r2, [r6, #P_LIGHT]
	str	r2, [r0, #I1_ACC0]
	ldr	r3, [r6, #P_SFRAC]
	ldr	r4, [r6, #P_TFRAC]
	ldr	r2, [r6, #P_PZ]
	.endm

	.section .text.d_aspans_asm, "ax"
	.align	2
	.global	d_aspans_asm
	.thumb_func
	.type	d_aspans_asm, %function
d_aspans_asm:
	push	{r4, r5, r6, r7, lr}
	mov	r4, r8
	mov	r5, r9
	mov	r6, r10
	mov	r7, r11
	push	{r4, r5, r6, r7}
	sub	sp, #16
	mov	lr, r0			@ lr = the span package
	ldr	r1, =d_aspancount
	ldr	r2, [r1]
	str	r2, [sp, #K_ACOUNT]
	ldr	r1, =errorterm
	ldr	r2, [r1]
	str	r2, [sp, #K_ERR]
	@ the constants of the call: interpolator 1 for the light, lane 1 of both off
	ldr	r0, =SIO_INTERP
	ldr	r2, =CTL_LIGHT
	str	r2, [r0, #I1_CTRL0]
	movs	r2, #0
	str	r2, [r0, #I1_CTRL1]
	str	r2, [r0, #I1_ACC1]
	str	r2, [r0, #I1_BASE1]
	str	r2, [r0, #I0_CTRL1]
	str	r2, [r0, #I0_ACC1]
	str	r2, [r0, #I0_BASE1]
	ldr	r1, =acolormap
	ldr	r2, [r1]
	str	r2, [r0, #I1_BASE2]
	ldr	r1, =r_lstepx
	ldr	r2, [r1]
	str	r2, [r0, #I1_BASE0]
	ldr	r1, =r_zistepx
	ldr	r2, [r1]
	str	r2, [r0, #I0_BASE0]
	ldr	r1, =r_affinetridesc
	ldr	r2, [r1, #8]		@ skinwidth
	mov	r9, r2
	ldr	r1, =r_sstepx
	ldr	r2, [r1]
	mov	r10, r2
	ldr	r1, =r_tstepx
	ldr	r2, [r1]
	mov	r11, r2

.Lspan:
	@ r0 = SIO_INTERP, lr = the span package. lcount and id's error term step
	mov	r6, lr
	ldr	r3, [sp, #K_ACOUNT]
	ldr	r2, [r6, #P_COUNT]
	subs	r5, r3, r2		@ lcount
	ldr	r2, [sp, #K_ERR]
	ldr	r4, =erroradjustup
	ldr	r4, [r4]
	adds	r2, r4
	bmi	1f
	ldr	r4, =erroradjustdown
	ldr	r4, [r4]
	subs	r2, r4
	ldr	r4, =d_countextrastep
	b	2f
1:	ldr	r4, =ubasestep
2:	ldr	r4, [r4]
	adds	r3, r4
	str	r3, [sp, #K_ACOUNT]
	str	r2, [sp, #K_ERR]
	cmp	r5, #0
	ble	.Lnext

	@ which piece of the depth: z0 = lzi >> 16, z1 at the last pixel
	ldr	r1, [r6, #P_ZI]		@ lzi
	ldr	r7, [r0, #I0_BASE0]	@ zistepx, kept on interpolator 0
	subs	r3, r5, #1
	muls	r7, r3
	adds	r7, r1			@ lzi at the last pixel
	asrs	r3, r1, #16		@ z0
	asrs	r7, r7, #16		@ z1
	movs	r4, #1
	lsls	r4, #10			@ 1024
	cmp	r3, #0
	blt	.Lgen
	cmp	r7, #0
	blt	.Lgen
	cmp	r3, r4
	bge	.Lnear
	cmp	r7, r4
	bge	.Lgen
	ldr	r2, =CTL_Z19		@ both below 1024: z = lzi >> 19
	str	r2, [r0, #I0_CTRL0]
	str	r1, [r0, #I0_ACC0]
	movs	r2, #0
	str	r2, [r0, #I0_BASE2]
	b	.Lfast
.Lnear:
	cmp	r7, r4
	blt	.Lgen
	movs	r2, #1
	lsls	r2, #14
	adds	r2, r4			@ 1024 + (128 << 7)
	cmp	r3, r2
	bge	.Lgen
	cmp	r7, r2
	bge	.Lgen
	ldr	r2, =CTL_Z23		@ the near piece: ((lzi - 2^26) >> 23) + 128
	str	r2, [r0, #I0_CTRL0]
	lsls	r4, #16
	subs	r1, r4
	str	r1, [r0, #I0_ACC0]
	movs	r2, #128
	str	r2, [r0, #I0_BASE2]
.Lfast:
	LOAD
.Lpx:
	ldr	r5, [r0, #I0_FULL]	@ the depth byte, lzi stepped
	ldr	r6, [r0, #I1_FULL]	@ the colour map row, the light stepped
	ldrb	r7, [r2]
	cmp	r5, r7
	blt	1f
	strb	r5, [r2]
	asrs	r7, r4, #16
	mov	r5, r9
	muls	r7, r5
	asrs	r5, r3, #16
	adds	r7, r5
	add	r7, r8
	ldrb	r7, [r7]
	ldrb	r7, [r6, r7]
	strb	r7, [r1]
1:	add	r3, r10
	add	r4, r11
	adds	r1, #1
	adds	r2, #1
	cmp	r1, r12
	bne	.Lpx
	b	.Lnext

.Lgen:
	@ any depth: lzi >> 16 from the signed lane, D_ZQ per pixel
	ldr	r2, =CTL_Z16
	str	r2, [r0, #I0_CTRL0]
	str	r1, [r0, #I0_ACC0]
	movs	r2, #0
	str	r2, [r0, #I0_BASE2]
	LOAD
.Lgpx:
	ldr	r5, [r0, #I0_FULL]	@ z = lzi >> 16, lzi stepped
	ldr	r6, [r0, #I1_FULL]
	cmp	r5, #0
	bge	2f
	movs	r5, #0			@ below 0: 0
	b	4f
2:	lsrs	r7, r5, #10
	bne	3f
	lsrs	r5, r5, #3		@ below 1024: z >> 3
	b	4f
3:	movs	r7, #1
	lsls	r7, #10
	subs	r5, r7
	asrs	r5, r5, #7		@ (z - 1024) >> 7
	cmp	r5, #127
	ble	5f
	movs	r5, #255
	b	4f
5:	adds	r5, #128
4:	ldrb	r7, [r2]
	cmp	r5, r7
	blt	1f
	strb	r5, [r2]
	asrs	r7, r4, #16
	mov	r5, r9
	muls	r7, r5
	asrs	r5, r3, #16
	adds	r7, r5
	add	r7, r8
	ldrb	r7, [r7]
	ldrb	r7, [r6, r7]
	strb	r7, [r1]
1:	add	r3, r10
	add	r4, r11
	adds	r1, #1
	adds	r2, #1
	cmp	r1, r12
	bne	.Lgpx

.Lnext:
	mov	r6, lr
	adds	r6, #P_SIZE
	mov	lr, r6
	ldr	r2, [r6, #P_COUNT]
	ldr	r3, =-999999
	cmp	r2, r3
	beq	1f
	b	.Lspan
1:	ldr	r7, =d_aspancount
	ldr	r2, [sp, #K_ACOUNT]
	str	r2, [r7]
	ldr	r7, =errorterm
	ldr	r2, [sp, #K_ERR]
	str	r2, [r7]
	add	sp, #16
	pop	{r4, r5, r6, r7}
	mov	r8, r4
	mov	r9, r5
	mov	r10, r6
	mov	r11, r7
	pop	{r4, r5, r6, r7, pc}

	.ltorg
	.size	d_aspans_asm, . - d_aspans_asm
