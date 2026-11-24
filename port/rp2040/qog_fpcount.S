/* QuakeOG (Agent Z): counting stubs for the engine library's float, double
 * and divide helper calls (QOG_PHASE_FPCOUNT, port/common/qog_fpcount.h).
 * Measurement builds of the board client only. apps/quake/CMakeLists.txt
 * renames each helper the library calls to its stub here (objcopy
 * --redefine-syms with qog_fpcount.syms). A stub adds one to the calling
 * core's counter of its group and to the core's total for the group's kind,
 * then jumps to the real helper (the SDK's wrapper, through the linker's
 * --wrap) with r0 to r3 and lr as the caller left them. Only ip and the
 * flags change, which the run-time ABI allows for every helper, the
 * flag-returning compares included. The stubs run from flash (no RAM left
 * for them), so read XIP numbers from a QOG_BOARD_XIPSTATS build without
 * FPCOUNT. Generated, keep in step with qog_fpcount.syms. */
	.syntax unified
	.cpu cortex-m0plus
	.thumb

	.section .text.qog_fpcount, "ax"
	.balign 4

@ r0 the helper, r1 the group's byte offset, r2 the total's, r0 to r2 pushed
	.thumb_func
qog_fpcount_common:
	mov		ip, r0
	ldr		r0, =0xd0000000		@ SIO CPUID, 0 or 1
	ldr		r0, [r0]
	lsls	r0, r0, #7			@ 32 counters a core
	adds	r1, r1, r0
	adds	r2, r2, r0
	ldr		r0, =qog_fpc
	adds	r1, r1, r0
	adds	r2, r2, r0
	ldr		r0, [r1]
	adds	r0, r0, #1
	str		r0, [r1]
	ldr		r0, [r2]
	adds	r0, r0, #1
	str		r0, [r2]
	pop		{r0, r1, r2}
	bx		ip
	.ltorg

	.macro	QOG_FPSTUB name, group, total
	.global	qog_fpcnt_\name
	.type	qog_fpcnt_\name, %function
	.thumb_func
qog_fpcnt_\name:
	push	{r0, r1, r2}
	ldr		r0, =\name
	movs	r1, #(\group * 4)
	movs	r2, #(\total * 4)
	b		qog_fpcount_common
	.ltorg
	.endm
	QOG_FPSTUB	__aeabi_fadd, 0, 30
	QOG_FPSTUB	__aeabi_fsub, 1, 30
	QOG_FPSTUB	__aeabi_fmul, 2, 30
	QOG_FPSTUB	__aeabi_fdiv, 3, 30
	QOG_FPSTUB	__aeabi_fcmpeq, 4, 30
	QOG_FPSTUB	__aeabi_fcmplt, 4, 30
	QOG_FPSTUB	__aeabi_fcmple, 4, 30
	QOG_FPSTUB	__aeabi_fcmpge, 4, 30
	QOG_FPSTUB	__aeabi_fcmpgt, 4, 30
	QOG_FPSTUB	__aeabi_cfcmpeq, 4, 30
	QOG_FPSTUB	__aeabi_cfcmple, 4, 30
	QOG_FPSTUB	__aeabi_cfrcmple, 4, 30
	QOG_FPSTUB	__aeabi_f2iz, 5, 30
	QOG_FPSTUB	__aeabi_f2uiz, 5, 30
	QOG_FPSTUB	__aeabi_i2f, 6, 30
	QOG_FPSTUB	__aeabi_ui2f, 6, 30
	QOG_FPSTUB	__aeabi_f2d, 7, 30
	QOG_FPSTUB	sqrtf, 8, 30
	QOG_FPSTUB	expf, 8, 30
	QOG_FPSTUB	logf, 8, 30
	QOG_FPSTUB	atan2f, 8, 30
	QOG_FPSTUB	__aeabi_dadd, 9, 31
	QOG_FPSTUB	__aeabi_dsub, 10, 31
	QOG_FPSTUB	__aeabi_dmul, 11, 31
	QOG_FPSTUB	__aeabi_ddiv, 12, 31
	QOG_FPSTUB	__aeabi_dcmpeq, 13, 31
	QOG_FPSTUB	__aeabi_dcmplt, 13, 31
	QOG_FPSTUB	__aeabi_dcmple, 13, 31
	QOG_FPSTUB	__aeabi_dcmpge, 13, 31
	QOG_FPSTUB	__aeabi_dcmpgt, 13, 31
	QOG_FPSTUB	__aeabi_dcmpun, 13, 31
	QOG_FPSTUB	__aeabi_cdcmpeq, 13, 31
	QOG_FPSTUB	__aeabi_cdcmple, 13, 31
	QOG_FPSTUB	__aeabi_cdrcmple, 13, 31
	QOG_FPSTUB	__aeabi_d2iz, 14, 31
	QOG_FPSTUB	__aeabi_d2uiz, 14, 31
	QOG_FPSTUB	__aeabi_d2f, 15, 31
	QOG_FPSTUB	__aeabi_i2d, 16, 31
	QOG_FPSTUB	__aeabi_ui2d, 16, 31
	QOG_FPSTUB	ceil, 17, 31
	QOG_FPSTUB	floor, 17, 31
	QOG_FPSTUB	sqrt, 17, 31
	QOG_FPSTUB	sin, 17, 31
	QOG_FPSTUB	cos, 17, 31
	QOG_FPSTUB	tan, 17, 31
	QOG_FPSTUB	pow, 17, 31
	QOG_FPSTUB	exp, 17, 31
	QOG_FPSTUB	log, 17, 31
	QOG_FPSTUB	atan, 17, 31
	QOG_FPSTUB	atan2, 17, 31
	QOG_FPSTUB	exp2, 17, 31
	QOG_FPSTUB	__aeabi_idiv, 18, 29
	QOG_FPSTUB	__aeabi_idivmod, 18, 29
	QOG_FPSTUB	__aeabi_uidiv, 18, 29
	QOG_FPSTUB	__aeabi_uidivmod, 18, 29
	QOG_FPSTUB	__aeabi_lmul, 19, 29
	QOG_FPSTUB	__aeabi_ldivmod, 19, 29
	QOG_FPSTUB	__aeabi_uldivmod, 19, 29
