// coro_switch(from, to): saves the AAPCS64 callee-saved registers on the current stack, stores
// sp in *from, loads sp from *to and restores. coro_start is the first return of a new coroutine.
#if defined(__aarch64__)
#if defined(__APPLE__)
#define CORO_SWITCH _coro_switch
#define CORO_START _coro_start
#else
#define CORO_SWITCH coro_switch
#define CORO_START coro_start
#endif
	.text
	.p2align 2
	.globl CORO_SWITCH
CORO_SWITCH:
	sub sp, sp, #160
	stp x19, x20, [sp, #0]
	stp x21, x22, [sp, #16]
	stp x23, x24, [sp, #32]
	stp x25, x26, [sp, #48]
	stp x27, x28, [sp, #64]
	stp x29, x30, [sp, #80]
	stp d8, d9, [sp, #96]
	stp d10, d11, [sp, #112]
	stp d12, d13, [sp, #128]
	stp d14, d15, [sp, #144]
	mov x9, sp
	str x9, [x0]
	ldr x9, [x1]
	mov sp, x9
	ldp x19, x20, [sp, #0]
	ldp x21, x22, [sp, #16]
	ldp x23, x24, [sp, #32]
	ldp x25, x26, [sp, #48]
	ldp x27, x28, [sp, #64]
	ldp x29, x30, [sp, #80]
	ldp d8, d9, [sp, #96]
	ldp d10, d11, [sp, #112]
	ldp d12, d13, [sp, #128]
	ldp d14, d15, [sp, #144]
	add sp, sp, #160
	ret

	.globl CORO_START
CORO_START:
	mov x0, x20
	blr x19
	brk #0
#endif
#if defined(__linux__) && defined(__ELF__)
	.section .note.GNU-stack,"",@progbits
#endif
