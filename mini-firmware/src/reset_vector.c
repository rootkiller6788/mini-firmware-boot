#include "reset_vector.h"
#include <stdio.h>
#include <string.h>

bool reset_vector_init(ResetVector *rv, uint32_t entry_ip)
{
    if (!rv) return false;

    rv->startup_ip = entry_ip;
    rv->gdt_ptr = 0;
    rv->idt_ptr = 0;
    return true;
}

bool cpu_reset(CPUContext *ctx, const ResetVector *rv)
{
    if (!ctx || !rv) return false;

    memset(ctx, 0, sizeof(CPUContext));
    ctx->eip = rv->startup_ip & 0xFFFF;
    ctx->cr0 = 0x00000010;
    ctx->cr3 = 0;
    ctx->cr4 = 0;
    ctx->eflags = 0x00000002;
    ctx->current_mode = CPU_MODE_REAL;

    return true;
}

bool cpu_init_gdt(CPUContext *ctx)
{
    if (!ctx) return false;

    if (ctx->current_mode != CPU_MODE_REAL) {
        return false;
    }

    return true;
}

bool cpu_init_idt(CPUContext *ctx)
{
    if (!ctx) return false;

    if (ctx->current_mode != CPU_MODE_PROTECTED &&
        ctx->current_mode != CPU_MODE_REAL) {
        return false;
    }

    return true;
}

bool cpu_switch_mode(CPUContext *ctx, CPUMode target_mode)
{
    if (!ctx) return false;

    if (ctx->current_mode == CPU_MODE_REAL && target_mode == CPU_MODE_PROTECTED) {
        ctx->cr0 |= 0x00000001;
        ctx->current_mode = CPU_MODE_PROTECTED;
        return true;
    }

    if (ctx->current_mode == CPU_MODE_PROTECTED && target_mode == CPU_MODE_LONG) {
        ctx->cr0 |= 0x00000001;
        ctx->cr4 |= 0x00000020;
        ctx->current_mode = CPU_MODE_LONG;
        return true;
    }

    if (ctx->current_mode == target_mode) {
        return true;
    }

    return false;
}

void cpu_print_registers(const CPUContext *ctx)
{
    if (!ctx) return;

    printf("=== CPU Register State ===\n");
    printf("EAX: 0x%08X  EBX: 0x%08X\n", ctx->eax, ctx->ebx);
    printf("ECX: 0x%08X  EDX: 0x%08X\n", ctx->ecx, ctx->edx);
    printf("ESI: 0x%08X  EDI: 0x%08X\n", ctx->esi, ctx->edi);
    printf("ESP: 0x%08X  EBP: 0x%08X\n", ctx->esp, ctx->ebp);
    printf("EIP: 0x%08X  EFLAGS: 0x%08X\n", ctx->eip, ctx->eflags);
    printf("CR0: 0x%08X  CR3: 0x%08X  CR4: 0x%08X\n", ctx->cr0, ctx->cr3, ctx->cr4);

    const char *mode_str;
    switch (ctx->current_mode) {
        case CPU_MODE_REAL:      mode_str = "Real Mode (16-bit)"; break;
        case CPU_MODE_PROTECTED: mode_str = "Protected Mode (32-bit)"; break;
        case CPU_MODE_LONG:      mode_str = "Long Mode (64-bit)"; break;
        default:                 mode_str = "Unknown"; break;
    }
    printf("Mode: %s\n", mode_str);
}
