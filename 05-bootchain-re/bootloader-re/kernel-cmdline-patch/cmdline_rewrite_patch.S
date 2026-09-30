// Early kernel cmdline rewriter
// Insert this in arch/arm64/kernel/head.S after preserve_boot_args
// This runs BEFORE cmdline parsing, rewrites "protected" -> "nvhe"

SYM_CODE_START_LOCAL(nx679j_cmdline_rewriter)
    // X21 contains FDT pointer (preserved from earlier)
    // Find /chosen/bootargs in FDT and patch it
    
    // For now, simpler approach: patch kernel's boot_command_line buffer
    // This buffer is at a known symbol after kernel decompression
    
    // Load address of boot_command_line (need to get this from System.map)
    adrp    x0, boot_command_line
    add     x0, x0, :lo12:boot_command_line
    
    mov     x1, #4096           // Max cmdline length
    
.Lsearch_loop:
    ldrb    w2, [x0], #1        // Load byte
    cbz     w2, .Lnot_found     // End of string
    
    // Check for 'p' (start of "protected")
    cmp     w2, #'p'
    b.ne    .Lcontinue_search
    
    // Check full "protected" string
    sub     x3, x0, #1          // Back to 'p'
    adr     x4, .Lprotected_str
    mov     x5, #9              // Length of "protected"
    
.Lcompare_loop:
    ldrb    w6, [x3], #1
    ldrb    w7, [x4], #1
    cmp     w6, w7
    b.ne    .Lcontinue_search
    subs    x5, x5, #1
    b.ne    .Lcompare_loop
    
    // Found "protected"! Replace with "nvhe\0\0\0\0\0"
    sub     x3, x3, #9          // Back to start of "protected"
    adr     x4, .Lnvhe_str
    mov     x5, #9
.Lreplace_loop:
    ldrb    w6, [x4], #1
    strb    w6, [x3], #1
    subs    x5, x5, #1
    b.ne    .Lreplace_loop
    
    b       .Ldone
    
.Lcontinue_search:
    subs    x1, x1, #1
    b.ne    .Lsearch_loop
    
.Lnot_found:
.Ldone:
    ret

.Lprotected_str:
    .ascii "protected"
.Lnvhe_str:
    .ascii "nvhe\0\0\0\0\0"

SYM_CODE_END(nx679j_cmdline_rewriter)
