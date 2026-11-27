    .section .rodata.sm64ds_external_layout,"a",%progbits
    .balign 4
    .global sm64ds_external_layout
    .type sm64ds_external_layout, %object
sm64ds_external_layout:
    // Filled when the final ELF is split. No payload offsets come from the SD.
    .zero (40 + 8 * 8192)
    .size sm64ds_external_layout, . - sm64ds_external_layout
    .section .note.GNU-stack,"",%progbits
