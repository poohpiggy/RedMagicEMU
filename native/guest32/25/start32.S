// aemu32: entry, raw syscalls and the jump into the guest (ARM, A32)
	.syntax unified
	.arm
	.text
	.globl _start
	.type _start,%function
_start:
	mov fp, #0
	mov lr, #0
	mov r0, sp
	bic sp, sp, #7
	bl aemu_main
	udf #1

// long sc(long nr, long a, long b, long c, long d, long e, long f)
// nr in r0, a..c in r1..r3, d..f on the stack
	.globl sc
	.type sc,%function
sc:
	push {r4, r5, r6, r7}
	mov r7, r0
	mov r0, r1
	mov r1, r2
	mov r2, r3
	ldr r3, [sp, #16]
	ldr r4, [sp, #20]
	ldr r5, [sp, #24]
	svc #0
	pop {r4, r5, r6, r7}
	bx lr

// void enter_guest(void *entry, void *sp)
	.globl enter_guest
	.type enter_guest,%function
enter_guest:
	mov ip, r0
	mov sp, r1
	mov r0, #0
	mov r1, #0
	mov r2, #0
	mov r3, #0
	mov fp, #0
	mov lr, #0
	bx ip
