#ifndef CPU_INIT_H
#define CPU_INIT_H

#include <stdbool.h>
#include <stdint.h>

#define CPU_FEATURE_APIC   (1U << 0)
#define CPU_FEATURE_X2APIC (1U << 1)
#define CPU_FEATURE_SMX    (1U << 2)
#define CPU_FEATURE_VMX    (1U << 3)
#define CPU_FEATURE_NX     (1U << 4)
#define CPU_FEATURE_SMEP   (1U << 5)
#define CPU_FEATURE_SMAP   (1U << 6)
#define CPU_FEATURE_MTRR   (1U << 7)
#define CPU_FEATURE_PAT    (1U << 8)

#define MSR_IA32_MTRRCAP       0x00FE
#define MSR_IA32_SYSENTER_CS   0x0174
#define MSR_IA32_SYSENTER_ESP  0x0175
#define MSR_IA32_SYSENTER_EIP  0x0176
#define MSR_IA32_MCG_CAP       0x0179
#define MSR_IA32_MCG_STATUS    0x017A
#define MSR_IA32_MISC_ENABLE   0x01A0
#define MSR_IA32_FEATURE_CTRL  0x003A
#define MSR_IA32_APIC_BASE     0x001B
#define MSR_IA32_EFER          0xC0000080
#define MSR_IA32_STAR          0xC0000081
#define MSR_IA32_LSTAR         0xC0000082
#define MSR_IA32_FMASK         0xC0000084
#define MSR_IA32_PAT           0x0277
#define MTRR_PHYS_BASE_0       0x0200
#define MTRR_PHYS_MASK_0       0x0201

#define MAX_MSR_COUNT          32
#define MAX_CPU_CORES          64
#define MICROCODE_DEFAULT_VER  0x000000A0
#define DEFAULT_BSP_APIC_ID    0x00000000

typedef struct {
    uint32_t address;
    uint64_t value;
} MSR;

typedef struct {
    uint32_t microcode_version;
    uint64_t enabled_features;
    uint32_t msr_count;
    MSR      msrs[MAX_MSR_COUNT];
    uint32_t apic_id;
    bool     is_bsp;
    bool     paging_enabled;
    bool     long_mode;
    bool     smm_enabled;
    uint8_t  stepping;
    uint8_t  model;
    uint8_t  family;
    uint8_t  cpu_type;
} CPUInitState;

const char *cpu_feature_name(uint64_t feature_bit);

void cpu_init_bsp(CPUInitState *cpu);
void cpu_init_ap(CPUInitState *cpu, uint32_t apic_id);
bool cpu_load_microcode(CPUInitState *cpu, uint32_t version);
void cpu_init_msrs(CPUInitState *cpu);
void cpu_init_caches(CPUInitState *cpu);
bool cpu_enable_paging(CPUInitState *cpu);
void cpu_init_mtrr(CPUInitState *cpu);
void cpu_init_sysenter(CPUInitState *cpu);
void cpu_print_state(const CPUInitState *cpu);

#endif
