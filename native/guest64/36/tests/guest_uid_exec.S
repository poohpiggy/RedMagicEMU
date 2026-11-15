/* Isolated ARM64 guest probe: reports UID transition result, changes no files. */
.text
.global _start
_start:
    /* A privileged process can temporarily drop its effective UID and restore it. */
    mov x0, #-1
    mov x1, #12345
    mov x2, #-1
    mov x8, #147 /* setresuid */
    svc #0
    cbnz x0, fail
    mov x0, #0
    mov x8, #146 /* setuid */
    svc #0
    cbnz x0, fail
    /* Permanently dropping all three IDs must prohibit restoration to root. */
    mov x0, #12345
    mov x1, #12345
    mov x2, #12345
    mov x8, #147
    svc #0
    cbnz x0, fail
    mov x0, #0
    mov x8, #146
    svc #0
    cmn x0, #1 /* -EPERM */
    b.ne fail
    mov x8, #175 /* geteuid */
    svc #0
    mov x1, #12345
    cmp x0, x1
    b.ne fail
    mov x0, #1
    adr x1, success
    mov x2, #success_len
    mov x8, #64 /* write */
    svc #0
    mov x0, #0
    mov x8, #93 /* exit */
    svc #0
fail:
    mov x0, #1
    adr x1, failure
    mov x2, #failure_len
    mov x8, #64
    svc #0
    mov x0, #1
    mov x8, #93
    svc #0
success: .ascii "GUEST_UID_EXEC_OK\n"
.set success_len, . - success
failure: .ascii "GUEST_UID_EXEC_FAILED\n"
.set failure_len, . - failure
