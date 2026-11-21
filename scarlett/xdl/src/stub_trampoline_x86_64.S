/*
 * Proton-Scarlett — x86-64 Win64 ABI Diagnostic Auto-Stub Trampoline
 *
 * Entry convention (Microsoft x64 ABI):
 *   R10   = const scarlett_stub_descriptor_t* desc
 *   [RSP] = caller return address (16n + 8 stack alignment on entry)
 *   RCX   = Win64 arg0 (this / api_id)
 *   RDX   = Win64 arg1 (riid)
 *   R8    = Win64 arg2 (out_impl)
 *   R9    = Win64 arg3
 *
 * Preserves ALL Microsoft x64 callee-saved registers across the C dispatch call
 * (including RDI, RSI, and XMM6..XMM15 which are caller-saved under SysV AMD64),
 * invokes `scarlett_stub_dispatch_win64(desc, return_address, rcx, rdx, r8, r9)`,
 * and returns the guaranteed-negative HRESULT in EAX/RAX.
 */

.intel_syntax noprefix
.text

.globl scarlett_stub_trampoline_win64
#if !defined(_WIN32)
.type scarlett_stub_trampoline_win64, @function
#endif
.p2align 4
scarlett_stub_trampoline_win64:
#if defined(_WIN32)
    /* Microsoft x64 target: push 5th/6th args + 32-byte shadow space */
    /* On entry: [rsp] = return_address, rsp = 16n + 8 */
    sub rsp, 56
    /* [rsp + 56] = caller return_address */
    mov [rsp + 40], r9              /* arg6: r9 */
    mov [rsp + 32], r8              /* arg5: r8 */
    mov r9, rdx                     /* arg4: win64 rdx */
    mov r8, rcx                     /* arg3: win64 rcx */
    mov rdx, [rsp + 56]             /* arg2: return_address */
    mov rcx, r10                    /* arg1: desc */
    call scarlett_stub_dispatch_win64
    add rsp, 56
    ret
#else
    /* Linux ELF64 (SysV AMD64 C runtime invoked from ms_abi caller) */
    /* Save Win64 callee-saved GPRs clobbered by SysV: rdi, rsi */
    push rdi
    push rsi
    /* Stack now has 2 pushes (+16 bytes), so rsp is still 16n + 8.
     * Subtract 168 bytes: 160 bytes for XMM6..XMM15 + 8 bytes to align rsp to 16n.
     * Caller return address sits at [rsp + 168 + 16] = [rsp + 184]. */
    sub rsp, 168
    movdqu [rsp +   0], xmm6
    movdqu [rsp +  16], xmm7
    movdqu [rsp +  32], xmm8
    movdqu [rsp +  48], xmm9
    movdqu [rsp +  64], xmm10
    movdqu [rsp +  80], xmm11
    movdqu [rsp +  96], xmm12
    movdqu [rsp + 112], xmm13
    movdqu [rsp + 128], xmm14
    movdqu [rsp + 144], xmm15

    /* Marshal arguments into SysV AMD64 registers:
     *   rdi = desc (r10)
     *   rsi = return_address ([rsp + 184])
     *   rdx = win64 rcx
     *   rcx = win64 rdx
     *   r8  = win64 r8
     *   r9  = win64 r9
     */
    mov rdi, r10
    mov rsi, [rsp + 184]
    mov rax, rdx
    mov rdx, rcx
    mov rcx, rax

    call scarlett_stub_dispatch_win64@PLT

    /* Restore Win64 callee-saved XMM6..XMM15 and RDI/RSI (preserve EAX/RAX) */
    movdqu xmm6,  [rsp +   0]
    movdqu xmm7,  [rsp +  16]
    movdqu xmm8,  [rsp +  32]
    movdqu xmm9,  [rsp +  48]
    movdqu xmm10, [rsp +  64]
    movdqu xmm11, [rsp +  80]
    movdqu xmm12, [rsp +  96]
    movdqu xmm13, [rsp + 112]
    movdqu xmm14, [rsp + 128]
    movdqu xmm15, [rsp + 144]
    add rsp, 168
    pop rsi
    pop rdi
    ret
.size scarlett_stub_trampoline_win64, . - scarlett_stub_trampoline_win64
#endif

#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",@progbits
#endif
