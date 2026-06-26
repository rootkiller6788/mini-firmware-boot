#ifndef RESET_VECTOR_H
#define RESET_VECTOR_H

#include <stdbool.h>
#include <stdint.h>

#define RESET_VECTOR_ADDR 0xFFFFFFF0

typedef enum {
    CPU_MODE_REAL,
    CPU_MODE_PROTECTED,
    CPU_MODE_LONG
} CPUMode;

typedef struct {
    uint32_t startup_ip;
    uint32_t gdt_ptr;
    uint32_t idt_ptr;
} ResetVector;

typedef struct {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t esp;
    uint32_t ebp;
    uint32_t eip;
    uint32_t eflags;
    uint32_t cr0;
    uint32_t cr3;
    uint32_t cr4;
    CPUMode  current_mode;
} CPUContext;

bool reset_vector_init(ResetVector *rv, uint32_t entry_ip);
bool cpu_reset(CPUContext *ctx, const ResetVector *rv);
bool cpu_init_gdt(CPUContext *ctx);
bool cpu_init_idt(CPUContext *ctx);
bool cpu_switch_mode(CPUContext *ctx, CPUMode target_mode);
void cpu_print_registers(const CPUContext *ctx);

#endif
