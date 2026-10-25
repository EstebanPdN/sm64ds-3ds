/* Native ARM11 user-mode primitives, adapted from tangosdev/sm64ds-decomp
 * commit 6230b2996636e3fd0e03cb178161c94cb4b73cc9 (MIT).
 * See source/vendor/ntr/LICENSE and docs/NATIVE_PRIMITIVES.md.
 * These accept real mapped memory, never DS physical addresses.
 */
.syntax unified
.arm

.section .text.CpuCopy8, "ax", %progbits
.align 2
.global CpuCopy8
.type CpuCopy8, %function
CpuCopy8:

    cmp r2, #0
    bxeq lr
    tst r1, #1
    beq .LCpuCopy8_L40
    ldrh ip, [r1, #-1]
    and ip, ip, #0xff
    tst r0, #1
    ldrhne r3, [r0, #-1]
    movne r3, r3, lsr #8
    ldrheq r3, [r0]
    orr r3, ip, r3, lsl #8
    strh r3, [r1, #-1]
    add r0, r0, #1
    add r1, r1, #1
    subs r2, r2, #1
    bxeq lr
.LCpuCopy8_L40:
    eor ip, r1, r0
    tst ip, #1
    beq .LCpuCopy8_L94
    bic r0, r0, #1
    ldrh ip, [r0], #2
    mov r3, ip, lsr #8
    subs r2, r2, #2
    blo .LCpuCopy8_L78
.LCpuCopy8_L60:
    ldrh ip, [r0], #2
    orr ip, r3, ip, lsl #8
    strh ip, [r1], #2
    mov r3, ip, lsr #0x10
    subs r2, r2, #2
    bhs .LCpuCopy8_L60
.LCpuCopy8_L78:
    tst r2, #1
    bxeq lr
    ldrh ip, [r1]
    and ip, ip, #0xff00
    orr ip, ip, r3
    strh ip, [r1]
    bx lr
.LCpuCopy8_L94:
    tst ip, #2
    beq .LCpuCopy8_Lc0
    bics r3, r2, #1
    beq .LCpuCopy8_L10c
    sub r2, r2, r3
    add ip, r3, r1
.LCpuCopy8_Lac:
    ldrh r3, [r0], #2
    strh r3, [r1], #2
    cmp r1, ip
    blo .LCpuCopy8_Lac
    b .LCpuCopy8_L10c
.LCpuCopy8_Lc0:
    cmp r2, #2
    blo .LCpuCopy8_L10c
    tst r1, #2
    beq .LCpuCopy8_Le0
    ldrh r3, [r0], #2
    strh r3, [r1], #2
    subs r2, r2, #2
    bxeq lr
.LCpuCopy8_Le0:
    bics r3, r2, #3
    beq .LCpuCopy8_L100
    sub r2, r2, r3
    add ip, r3, r1
.LCpuCopy8_Lf0:
    ldr r3, [r0], #4
    str r3, [r1], #4
    cmp r1, ip
    blo .LCpuCopy8_Lf0
.LCpuCopy8_L100:
    tst r2, #2
    ldrhne r3, [r0], #2
    strhne r3, [r1], #2
.LCpuCopy8_L10c:
    tst r2, #1
    bxeq lr
    ldrh r2, [r1]
    ldrh r0, [r0]
    and r2, r2, #0xff00
    and r0, r0, #0xff
    orr r0, r2, r0
    strh r0, [r1]
    bx lr

.size CpuCopy8, .-CpuCopy8

.section .text.DecompressLZ16, "ax", %progbits
.align 2
.global DecompressLZ16
.type DecompressLZ16, %function
DecompressLZ16:

	stmfd	sp!, {r4,r5,r6,r7,r8,r9,r10,lr}
	mov	r3, #0
	ldr	r8, [r0], #4
	mov	r10, r8, lsr #8
	mov	r2, #0
.LDecompressLZ16_L14:
	cmp	r10, #0
	ble	.LDecompressLZ16_Ld0
	ldrb	r6, [r0], #1
	mov	r7, #8
.LDecompressLZ16_L24:
	subs	r7, r7, #1
	blt	.LDecompressLZ16_L14
	tst	r6, #0x80
	bne	.LDecompressLZ16_L50
	ldrb	r9, [r0], #1
	orr	r3, r3, r9, lsl r2
	sub	r10, r10, #1
	eors	r2, r2, #8
	strheq	r3, [r1], #2
	moveq	r3, #0
	b	.LDecompressLZ16_Lc0
.LDecompressLZ16_L50:
	ldrb	r9, [r0]
	mov	r8, #3
	add	r5, r8, r9, asr #4
	ldrb	r9, [r0], #1
	and	r8, r9, #0xf
	mov	r4, r8, lsl #8
	ldrb	r9, [r0], #1
	orr	r8, r9, r4
	add	r4, r8, #1
	rsb	r8, r2, #8
	and	r9, r4, #1
	eor	lr, r8, r9, lsl #3
	sub	r10, r10, r5
.LDecompressLZ16_L84:
	eor	lr, lr, #8
	rsb	r8, r2, #8
	add	r8, r4, r8, lsr #3
	mov	r8, r8, lsr #1
	mov	r8, r8, lsl #1
	ldrh	r9, [r1, -r8]
	mov	r8, #0xff
	and	r8, r9, r8, lsl lr
	mov	r8, r8, asr lr
	orr	r3, r3, r8, lsl r2
	eors	r2, r2, #8
	strheq	r3, [r1], #2
	moveq	r3, #0
	subs	r5, r5, #1
	bgt	.LDecompressLZ16_L84
.LDecompressLZ16_Lc0:
	cmp	r10, #0
	movgt	r6, r6, lsl #1
	bgt	.LDecompressLZ16_L24
	b	.LDecompressLZ16_L14
.LDecompressLZ16_Ld0:
	ldmfd	sp!, {r4,r5,r6,r7,r8,r9,r10,lr}
	bx	lr

.size DecompressLZ16, .-DecompressLZ16

/* Original 0x02052ec8: packed fx32 identity 4x4 (16 words).
 * Keep the original STM order and r0 advancement, including caller-saved
 * register effects. No DS registers or privileged CPU state are involved.
 */
.section .text.func_02052ec8, "ax", %progbits
.align 2
.global func_02052ec8
.type func_02052ec8, %function
func_02052ec8:
    mov r2, #0x1000
    mov r3, #0
    stmia r0!, {r2, r3}
    mov r1, #0
    stmia r0!, {r1, r3}
    stmia r0!, {r1, r2, r3}
    stmia r0!, {r1, r3}
    stmia r0!, {r1, r2, r3}
    stmia r0!, {r1, r3}
    stmia r0!, {r1, r2}
    bx lr
.size func_02052ec8, .-func_02052ec8

/* Pure-memory Thumb constructors used by the menu/model matrix preparation.
 * Their source-ROM instruction bytes, including wraparound NEG and flag
 * effects, are checked by tools/test_identity_primitives_arm.py.
 */
.thumb
.section .text.func_02052550, "ax", %progbits
.align 1
.global func_02052550
.type func_02052550, %function
.thumb_func
func_02052550:
    movs r3, #1
    lsls r3, r3, #12
    str r3, [r0]
    movs r3, #0
    str r3, [r0, #4]
    str r3, [r0, #8]
    str r3, [r0, #12]
    str r2, [r0, #16]
    str r1, [r0, #20]
    str r3, [r0, #24]
    negs r1, r1
    str r1, [r0, #28]
    str r2, [r0, #32]
    bx lr
.size func_02052550, .-func_02052550

.section .text.func_0205256c, "ax", %progbits
.align 1
.global func_0205256c
.type func_0205256c, %function
.thumb_func
func_0205256c:
    str r2, [r0]
    str r2, [r0, #32]
    movs r3, #0
    str r3, [r0, #4]
    str r3, [r0, #12]
    str r3, [r0, #20]
    str r3, [r0, #28]
    negs r2, r1
    movs r3, #1
    lsls r3, r3, #12
    str r1, [r0, #24]
    str r2, [r0, #8]
    str r3, [r0, #16]
    bx lr
.size func_0205256c, .-func_0205256c

.section .text.func_020527e8, "ax", %progbits
.align 1
.global func_020527e8
.type func_020527e8, %function
.thumb_func
func_020527e8:
    stmia r0!, {r1}
    movs r1, #0
    str r3, [r0, #28]
    movs r3, #0
    stmia r0!, {r1, r3}
    stmia r0!, {r1, r2, r3}
    movs r2, #0
    stmia r0!, {r1, r3}
    adds r0, #4
    stmia r0!, {r1, r2, r3}
    bx lr
.size func_020527e8, .-func_020527e8

.section .note.GNU-stack, "", %progbits
