#include "cpu_init.h"

#include <stdio.h>
#include <string.h>

static const CPUInitState g_default_cpu = {
    .microcode_version = MICROCODE_DEFAULT_VER,
    .enabled_features   = 0,
    .msr_count          = 0,
    .apic_id            = 0,
    .is_bsp             = false,
    .paging_enabled     = false,
    .long_mode          = false,
    .smm_enabled        = false,
    .stepping           = 1,
    .model              = 0x1E,
    .family             = 0x06,
    .cpu_type           = 0,
};

const char *cpu_feature_name(uint64_t feature_bit)
{
    switch (feature_bit) {
    case CPU_FEATURE_APIC:   return "APIC";
    case CPU_FEATURE_X2APIC: return "x2APIC";
    case CPU_FEATURE_SMX:    return "SMX";
    case CPU_FEATURE_VMX:    return "VMX";
    case CPU_FEATURE_NX:     return "NX/XD";
    case CPU_FEATURE_SMEP:   return "SMEP";
    case CPU_FEATURE_SMAP:   return "SMAP";
    case CPU_FEATURE_MTRR:   return "MTRR";
    case CPU_FEATURE_PAT:    return "PAT";
    default:                 return "UNKNOWN";
    }
}

static void cpu_add_msr(CPUInitState *cpu, uint32_t addr, uint64_t val)
{
    if (!cpu || cpu->msr_count >= MAX_MSR_COUNT) return;
    cpu->msrs[cpu->msr_count].address = addr;
    cpu->msrs[cpu->msr_count].value = val;
    cpu->msr_count++;
}

void cpu_init_bsp(CPUInitState *cpu)
{
    if (!cpu) return;

    *cpu = g_default_cpu;
    cpu->is_bsp = true;
    cpu->apic_id = DEFAULT_BSP_APIC_ID;
    cpu->enabled_features = CPU_FEATURE_APIC | CPU_FEATURE_X2APIC
                          | CPU_FEATURE_VMX | CPU_FEATURE_NX
                          | CPU_FEATURE_SMEP | CPU_FEATURE_MTRR
                          | CPU_FEATURE_PAT;

    printf("[CPU:BSP] Initializing Bootstrap Processor (APIC ID=%u)...\n", cpu->apic_id);
    printf("[CPU:BSP] Family=0x%02X Model=0x%02X Stepping=%u\n",
           cpu->family, cpu->model, cpu->stepping);

    cpu->microcode_version = MICROCODE_DEFAULT_VER;
    cpu->long_mode = true;
    cpu->paging_enabled = false;

    printf("[CPU:BSP] CR0: Setting PE=1, PG=0, WP=1, NE=1\n");
    printf("[CPU:BSP] CR4: Setting PAE=1, OSFXSR=1, OSXMMEXCPT=1\n");
}

void cpu_init_ap(CPUInitState *cpu, uint32_t apic_id)
{
    if (!cpu) return;

    *cpu = g_default_cpu;
    cpu->is_bsp = false;
    cpu->apic_id = apic_id;
    cpu->enabled_features = CPU_FEATURE_APIC | CPU_FEATURE_NX
                          | CPU_FEATURE_MTRR | CPU_FEATURE_PAT;

    printf("[CPU:AP-%u] Initializing Application Processor...\n", apic_id);
    printf("[CPU:AP-%u] Sending INIT-SIPI-SIPI sequence...\n", apic_id);
    printf("[CPU:AP-%u] AP started at real-mode vector 0x%05X\n", apic_id, 0x7000);
    printf("[CPU:AP-%u] Switching to protected mode...\n", apic_id);
    printf("[CPU:AP-%u] Switching to long mode...\n", apic_id);
    printf("[CPU:AP-%u] AP ready, waiting in MWAIT state.\n", apic_id);
}

bool cpu_load_microcode(CPUInitState *cpu, uint32_t version)
{
    if (!cpu) return false;

    printf("[CPU:UCODE] Loading microcode update...\n");
    printf("[CPU:UCODE] Current version: 0x%08X\n", cpu->microcode_version);
    printf("[CPU:UCODE] New version:     0x%08X\n", version);

    if (version > cpu->microcode_version) {
        printf("[CPU:UCODE] Microcode update applied.\n");
        cpu->microcode_version = version;
        return true;
    }

    printf("[CPU:UCODE] No update needed (current >= new).\n");
    return false;
}

void cpu_init_msrs(CPUInitState *cpu)
{
    if (!cpu) return;

    printf("[CPU:MSR] Programming Model-Specific Registers...\n");

    uint64_t efer = 0;
    if (cpu->long_mode) {
        efer |= (1ULL << 8);
    }
    if (cpu->enabled_features & CPU_FEATURE_NX) {
        efer |= (1ULL << 11);
    }
    cpu_add_msr(cpu, MSR_IA32_EFER, efer);
    printf("[CPU:MSR] EFER (0x%08X) = 0x%016llX [LME=%d, NXE=%d]\n",
           MSR_IA32_EFER, (unsigned long long)efer,
           (efer >> 8) & 1, (efer >> 11) & 1);

    uint64_t apic_base = 0xFEE00000ULL;
    if (cpu->enabled_features & CPU_FEATURE_X2APIC) {
        apic_base |= (1ULL << 10);
    }
    apic_base |= (1ULL << 11);
    cpu_add_msr(cpu, MSR_IA32_APIC_BASE, apic_base);
    printf("[CPU:MSR] APIC_BASE (0x%08X) = 0x%016llX\n",
           MSR_IA32_APIC_BASE, (unsigned long long)apic_base);

    uint64_t misc_enable = (1ULL << 3) | (1ULL << 7)
                         | (1ULL << 16) | (1ULL << 22)
                         | (1ULL << 34);
    cpu_add_msr(cpu, MSR_IA32_MISC_ENABLE, misc_enable);
    printf("[CPU:MSR] MISC_ENABLE (0x%08X) = 0x%016llX\n",
           MSR_IA32_MISC_ENABLE, (unsigned long long)misc_enable);

    if (cpu->enabled_features & CPU_FEATURE_VMX) {
        uint64_t feat_ctrl = (1ULL << 0) | (1ULL << 2);
        cpu_add_msr(cpu, MSR_IA32_FEATURE_CTRL, feat_ctrl);
        printf("[CPU:MSR] FEATURE_CTRL (0x%08X) = 0x%016llX [VMX enabled]\n",
               MSR_IA32_FEATURE_CTRL, (unsigned long long)feat_ctrl);
    }

    printf("[CPU:MSR] STAR/LSTAR/FMASK set for syscall entry.\n");
    cpu_add_msr(cpu, MSR_IA32_STAR,  0x001B000800000000ULL);
    cpu_add_msr(cpu, MSR_IA32_LSTAR, 0xFFFFFFFF81800000ULL);
    cpu_add_msr(cpu, MSR_IA32_FMASK, 0x0000000000034700ULL);

    printf("[CPU:MSR] Total MSRs programmed: %u\n", cpu->msr_count);
}

void cpu_init_caches(CPUInitState *cpu)
{
    if (!cpu) return;

    printf("[CPU:CACHE] Initializing CPU caches...\n");
    printf("[CPU:CACHE] CR0.CD=0, CR0.NW=0 (cache enabled, write-back)\n");
    printf("[CPU:CACHE] Enabling MTRRs for memory type range control...\n");

    if (cpu->enabled_features & CPU_FEATURE_MTRR) {
        cpu_init_mtrr(cpu);
    }

    printf("[CPU:CACHE] L1 Data Cache: 32KB, 8-way\n");
    printf("[CPU:CACHE] L1 Inst Cache: 32KB, 8-way\n");
    printf("[CPU:CACHE] L2 Unified Cache: 256KB, 4-way\n");
    printf("[CPU:CACHE] L3 Unified Cache: 8MB, 16-way\n");
    printf("[CPU:CACHE] Cache line size: 64 bytes.\n");
    printf("[CPU:CACHE] WBINVD executed to flush caches.\n");
}

void cpu_init_mtrr(CPUInitState *cpu)
{
    if (!cpu) return;

    printf("[CPU:MTRR] Programming Memory Type Range Registers...\n");

    uint64_t mtrrcap = 10;
    cpu_add_msr(cpu, MSR_IA32_MTRRCAP, mtrrcap);

    cpu_add_msr(cpu, MTRR_PHYS_BASE_0, 0x0000000000000006ULL);
    cpu_add_msr(cpu, MTRR_PHYS_MASK_0, 0x0000000FFF000800ULL);

    printf("[CPU:MTRR] Variable MTRRs: %d\n", 0);
    printf("[CPU:MTRR] Default memory type: Write-Back (0x06)\n");
    printf("[CPU:MTRR] MMIO range set to Uncacheable (UC)\n");
}

void cpu_init_sysenter(CPUInitState *cpu)
{
    if (!cpu) return;

    printf("[CPU:SYSENTER] Setting up SYSENTER/SYSEXIT MSRs...\n");
    cpu_add_msr(cpu, MSR_IA32_SYSENTER_CS,  0x0008);
    cpu_add_msr(cpu, MSR_IA32_SYSENTER_ESP, 0xFFFF800001000000ULL);
    cpu_add_msr(cpu, MSR_IA32_SYSENTER_EIP, 0xFFFFFFFF81800000ULL);
}

bool cpu_enable_paging(CPUInitState *cpu)
{
    if (!cpu) return false;

    printf("[CPU:PAGING] Enabling paging...\n");
    printf("[CPU:PAGING] PML4 table at 0x%016llX\n", 0x1000ULL);
    printf("[CPU:PAGING] Identity-mapping first 2MB (PDPT[0] -> 0x0)\n");
    printf("[CPU:PAGING] Identity-mapping 0xFFFFFFFF80000000 (kernel)\n");

    printf("[CPU:PAGING] Setting CR3 = 0x%016llX\n", 0x1000ULL);
    printf("[CPU:PAGING] Setting CR0.PG = 1\n");
    printf("[CPU:PAGING] Setting EFER.LME = 1 (long mode)\n");

    cpu->paging_enabled = true;
    printf("[CPU:PAGING] Paging enabled in long mode.\n");

    return true;
}

void cpu_print_state(const CPUInitState *cpu)
{
    if (!cpu) return;

    printf("\n");
    printf("   ============== CPU STATE ==============\n");
    printf("   Role:             %s\n", cpu->is_bsp ? "BSP" : "AP");
    printf("   APIC ID:          %u\n", cpu->apic_id);
    printf("   Family/Model/Stp: 0x%02X / 0x%02X / %u\n",
           cpu->family, cpu->model, cpu->stepping);

    printf("   Microcode:        0x%08X\n", cpu->microcode_version);
    printf("   Long Mode:        %s\n", cpu->long_mode ? "Yes" : "No");
    printf("   Paging:           %s\n", cpu->paging_enabled ? "Enabled" : "Disabled");
    printf("   SMM:              %s\n", cpu->smm_enabled ? "Yes" : "No");

    printf("   Features Enabled: ");
    bool first = true;
    for (uint64_t bit = 1; bit; bit <<= 1) {
        if (cpu->enabled_features & bit) {
            printf("%s%s", first ? "" : ", ", cpu_feature_name(bit));
            first = false;
        }
    }
    if (first) printf("(none)");
    printf("\n");

    printf("   MSRs Programmed:  %u\n", cpu->msr_count);
    for (uint32_t i = 0; i < cpu->msr_count; i++) {
        printf("     [%02u] 0x%08X = 0x%016llX\n",
               i, cpu->msrs[i].address,
               (unsigned long long)cpu->msrs[i].value);
    }
    printf("   ========================================\n");
}
