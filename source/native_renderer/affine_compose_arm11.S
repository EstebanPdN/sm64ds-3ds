// Native Fix12 affine composition, shared by all model/animation producers.
// r0: local[12], r1: parent[12], r2: disjoint output[12], word-aligned.
// Inputs may alias each other. No input stores, allocation or peripheral I/O.
// Signed products accumulate modulo 2^64, then bits 12..43 become the result;
// the parent's translation adds modulo 2^32, exactly as the portable owner.
.syntax unified
.arch armv6k
.fpu vfp
.eabi_attribute 28, 1
.arm
.section .text.sm64ds_affine_compose_arm11,"ax",%progbits
.align 2
.global sm64ds_affine_compose_arm11
.type sm64ds_affine_compose_arm11,%function
sm64ds_affine_compose_arm11:
    .fnstart
    .save {r4-r10,lr}
    push {r4-r10,lr}
    // Keep one parent row resident for four output columns. The short fixed
    // workload is unrolled so no loop branches or repeated zeroing remain.
    .irp row,0,1,2
        ldr r3,[r1,#(4*\row)]
        ldr r4,[r1,#(12+4*\row)]
        ldr r5,[r1,#(24+4*\row)]
        .irp column,0,1,2,3
            .if \column == 0
                ldmia r0,{r6-r8}
            .else
                add ip,r0,#(12*\column)
                ldmia ip,{r6-r8}
            .endif
            smull r9,r10,r6,r3
            smlal r9,r10,r7,r4
            smlal r9,r10,r8,r5
            lsr r9,r9,#12
            orr r9,r9,r10,lsl #20
            .if \column == 3
                ldr ip,[r1,#(36+4*\row)]
                add r9,r9,ip
            .endif
            str r9,[r2,#(12*\column+4*\row)]
        .endr
    .endr
    pop {r4-r10,pc}
    .fnend
.size sm64ds_affine_compose_arm11,.-sm64ds_affine_compose_arm11
.section .note.GNU-stack,"",%progbits
