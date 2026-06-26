#include "psci_fw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * psci_fw.c -- ARM PSCI Firmware Implementation
 *
 * Reference: ARM PSCI Specification v1.2 (DEN 0022E)
 *
 * PSCI is the standard interface for power management on ARM systems.
 * It runs at EL3 (Secure Monitor) and provides services to:
 *   - Rich OS (Linux) at EL1 via SMC instruction
 *   - Hypervisor (KVM) at EL2 via HVC instruction
 */

/* ??? L2: PSCI Initialization ???????????????????????????????? */

bool psci_init(PSCIFirmware *psci, uint32_t version_major,
               uint32_t version_minor)
{
    if (!psci) return false;
    memset(psci, 0, sizeof(PSCIFirmware));
    psci->version_major = version_major;
    psci->version_minor = version_minor;
    return true;
}

bool psci_register_cpu(PSCIFirmware *psci, uint64_t mpidr,
                       uint32_t cpu_id, uint64_t entry_point)
{
    if (!psci) return false;
    if (psci->num_cpus >= PSCI_MAX_CPUS) return false;

    PSCICPU *cpu = &psci->cpus[psci->num_cpus];
    memset(cpu, 0, sizeof(PSCICPU));
    cpu->mpidr       = mpidr;
    cpu->cpu_id      = cpu_id;
    cpu->state       = AFFINITY_STATE_OFF;
    cpu->entry_point = entry_point;
    cpu->context_id  = 0;
    cpu->hotplug_capable = true;

    psci->num_cpus++;
    return true;
}

/* ??? L2: CPU lookup by MPIDR ???????????????????????????????? */

static PSCICPU *psci_find_cpu(PSCIFirmware *psci, uint64_t mpidr)
{
    if (!psci) return NULL;
    for (uint32_t i = 0; i < psci->num_cpus; i++) {
        if (psci->cpus[i].mpidr == mpidr) return &psci->cpus[i];
    }
    return NULL;
}

/* ??? L3: PSCI_VERSION ?????????????????????????????????????? */

int32_t psci_call_version(const PSCIFirmware *psci)
{
    if (!psci) return PSCI_NOT_SUPPORTED;
    return (int32_t)((psci->version_major << 16) | psci->version_minor);
}

/* ??? L3: PSCI_CPU_OFF ?????????????????????????????????????? */

/*
 * Power down the calling CPU.
 *
 * Procedure:
 *   1. Verify caller is the target CPU (self-power-off only)
 *   2. Save CPU context to secure memory
 *   3. Flush caches (clean + invalidate)
 *   4. Notify power controller via SCPI/IPC
 *   5. Execute WFI (Wait For Interrupt)
 *
 * Precondition:  CPU is in ON state
 * Postcondition: CPU is in OFF state, caches flushed
 * Side effect:   CPU context is saved to secure SRAM
 */
int32_t psci_call_cpu_off(PSCIFirmware *psci, uint32_t cpu_id)
{
    if (!psci) return PSCI_NOT_SUPPORTED;
    if (cpu_id >= psci->num_cpus) return PSCI_INVALID_PARAMS;

    PSCICPU *cpu = &psci->cpus[cpu_id];

    /* Precondition: CPU must be ON */
    if (cpu->state != AFFINITY_STATE_ON) {
        return PSCI_ALREADY_ON;  /* Or DENIED if already off */
    }

    /* Power down sequence (simulated) */
    cpu->state = AFFINITY_STATE_OFF;
    psci->total_cpu_off_count++;

    printf("[PSCI] CPU%u: OFF (MPIDR=0x%08llX)\n",
           cpu_id, (unsigned long long)cpu->mpidr);

    return PSCI_SUCCESS;
}

/* ??? L3: PSCI_CPU_ON ??????????????????????????????????????? */

/*
 * Power on a target CPU.
 *
 * Procedure:
 *   1. Verify target CPU exists and is OFF
 *   2. Set entry_point and context_id in target CPU descriptor
 *   3. Notify power controller to supply power
 *   4. Assert reset (CPU held in reset until released)
 *   5. Release reset ? CPU fetches from entry_point
 *
 * Invariant: CPU_ON is only valid from ON ? OFF or SUSPEND ? ON.
 * Security:  Entry point must be in non-secure memory.
 *
 * Reference: DEN 0022E ?5.5 (CPU_ON)
 */
int32_t psci_call_cpu_on(PSCIFirmware *psci, uint64_t mpidr,
                         uint64_t entry_point, uint64_t context_id)
{
    if (!psci) return PSCI_NOT_SUPPORTED;

    PSCICPU *cpu = psci_find_cpu(psci, mpidr);
    if (!cpu) return PSCI_INVALID_PARAMS;

    /* Target CPU must be OFF */
    if (cpu->state != AFFINITY_STATE_OFF) {
        if (cpu->state == AFFINITY_STATE_ON)
            return PSCI_ALREADY_ON;
        if (cpu->state == AFFINITY_STATE_ON_PENDING)
            return PSCI_ON_PENDING;
        return PSCI_DENIED;
    }

    /* Validate entry point */
    if (entry_point == 0) return PSCI_INVALID_ADDRESS;

    /* Transition: OFF ? ON_PENDING */
    cpu->state       = AFFINITY_STATE_ON_PENDING;
    cpu->entry_point = entry_point;
    cpu->context_id  = context_id;

    /* Simulated: immediately transitions to ON */
    cpu->state = AFFINITY_STATE_ON;
    psci->total_cpu_on_count++;

    printf("[PSCI] CPU%u: ON (MPIDR=0x%08llX, entry=0x%08llX)\n",
           cpu->cpu_id, (unsigned long long)mpidr,
           (unsigned long long)entry_point);

    return PSCI_SUCCESS;
}

/* ??? L3: PSCI_CPU_SUSPEND ?????????????????????????????????? */

/*
 * Suspend the calling CPU.
 *
 * Power states:
 *   - Standby (StateType=0): CPU clock-gated, caches retained
 *   - Powerdown (StateType=1): CPU power-gated, caches lost
 *   - System Suspend: Entire SoC enters low-power, OS must re-init
 *
 * Affinity levels:
 *   - Level 0 (CPU): Only the calling core is suspended
 *   - Level 1 (Cluster): Entire cluster (L2 shared) is suspended
 *   - Level 2 (System): Entire system may suspend
 *
 * Wakeup sources: IRQ, FIQ, debug event, external wakeup pin.
 */
int32_t psci_call_cpu_suspend(PSCIFirmware *psci, uint32_t cpu_id,
                              uint32_t power_state, uint64_t entry_point,
                              uint64_t context_id)
{
    if (!psci) return PSCI_NOT_SUPPORTED;
    if (cpu_id >= psci->num_cpus) return PSCI_INVALID_PARAMS;

    PSCICPU *cpu = &psci->cpus[cpu_id];
    if (cpu->state != AFFINITY_STATE_ON) return PSCI_DENIED;

    /* Decode power state */
    uint32_t state_type = (power_state >> 30) & 0x3;
    uint32_t aff_level  = (power_state >> 24) & 0x3;
    uint32_t state_id   = power_state & 0x00FFFFFF;
    const char *type_str = (state_type == 0) ? "Standby" : "Powerdown";

    printf("[PSCI] CPU%u: SUSPEND (%s, aff=%u, stateID=0x%06X)\n",
           cpu_id, type_str, aff_level, state_id);

    /* Save entry point and context ID for resume */
    cpu->entry_point = entry_point;
    cpu->context_id  = context_id;

    /* Transition to SUSPEND state */
    cpu->state = AFFINITY_STATE_SUSPEND;
    cpu->suspend_count++;
    psci->total_suspend_count++;

    /* Simulated: CPU resumes immediately */
    cpu->state = AFFINITY_STATE_ON;

    return PSCI_SUCCESS;
}

/* ??? L3: PSCI_AFFINITY_INFO ???????????????????????????????? */

int32_t psci_call_affinity_info(const PSCIFirmware *psci, uint64_t mpidr)
{
    if (!psci) return PSCI_NOT_SUPPORTED;

    PSCICPU *cpu = psci_find_cpu((PSCIFirmware *)psci, mpidr);
    if (!cpu) return PSCI_INVALID_PARAMS;

    return (int32_t)cpu->state;
}

/* ??? L3: PSCI_SYSTEM_OFF ??????????????????????????????????? */

/*
 * System power-off sequence:
 *   1. Notify all secondary CPUs to power off (CPU_OFF broadcast)
 *   2. Disable interrupts (CPSR.I = 1)
 *   3. Clean and invalidate all caches
 *   4. Write PMIC power-off register
 *   5. Execute WFI (Wait For Interrupt) loop
 *
 * After SYSTEM_OFF, the system can only be restarted by:
 *   - Power button press
 *   - RTC alarm
 *   - Wake-on-LAN
 *   - External power cycle
 */
int32_t psci_call_system_off(PSCIFirmware *psci)
{
    if (!psci) return PSCI_NOT_SUPPORTED;

    printf("[PSCI] SYSTEM_OFF: Powering down all CPUs\n");

    /* Power off all CPUs */
    for (uint32_t i = 0; i < psci->num_cpus; i++) {
        psci->cpus[i].state = AFFINITY_STATE_OFF;
    }

    printf("[PSCI] SYSTEM_OFF: All CPUs halted, power-off sequence complete\n");
    return PSCI_SUCCESS;
}

/* ??? L3: PSCI_SYSTEM_RESET ???????????????????????????????? */

/*
 * System reset (cold boot without power cycle).
 *
 * Procedure:
 *   1. Notify all CPUs (IPI)
 *   2. Disable interrupts on calling CPU
 *   3. Clean all caches
 *   4. Write system reset register (e.g., PSCI_SYSTEM_RESET)
 *   5. Assert external system reset signal
 *
 * This is NOT a warm reset ? all state is lost.
 *
 * Reference: DEN 0022E ?5.11 (SYSTEM_RESET)
 */
int32_t psci_call_system_reset(PSCIFirmware *psci)
{
    if (!psci) return PSCI_NOT_SUPPORTED;

    printf("[PSCI] SYSTEM_RESET: Resetting system (cold boot)\n");

    /* Reset all CPU states */
    for (uint32_t i = 0; i < psci->num_cpus; i++) {
        psci->cpus[i].state = AFFINITY_STATE_OFF;
    }

    psci->reset_count++;
    psci->system_suspended = false;

    printf("[PSCI] SYSTEM_RESET: Reset signal asserted, count=%llu\n",
           (unsigned long long)psci->reset_count);

    return PSCI_SUCCESS;
}

/* ??? L3: PSCI_FEATURES ????????????????????????????????????? */

/*
 * Query PSCI feature support.
 *
 * This enables the OS to discover which PSCI functions are available
 * before calling them, avoiding undefined SMC traps.
 *
 * Feature flags (version-dependent):
 *   PSCI v1.0+: CPU_SUSPEND supports OS-initiated mode
 *   PSCI v1.1+: SYSTEM_RESET2 (architectural reset types)
 *   PSCI v1.2+: MEM_PROTECT (secure memory region)
 */
int32_t psci_call_features(const PSCIFirmware *psci, uint32_t func_id)
{
    if (!psci) return PSCI_NOT_SUPPORTED;

    switch (func_id) {
    case PSCI_VERSION:
    case PSCI_CPU_OFF:
    case PSCI_CPU_ON:
    case PSCI_CPU_SUSPEND:
    case PSCI_AFFINITY_INFO:
    case PSCI_SYSTEM_OFF:
    case PSCI_SYSTEM_RESET:
    case PSCI_FEATURES:
        return 0;  /* Supported, no feature flags */

    case PSCI_STAT_RESIDENCY:
    case PSCI_STAT_COUNT:
        return (psci->version_major >= 1 && psci->version_minor >= 1) ? 0 : PSCI_NOT_SUPPORTED;

    case PSCI_SYSTEM_RESET2:
        return (psci->version_major >= 1 && psci->version_minor >= 1) ? 0 : PSCI_NOT_SUPPORTED;

    case PSCI_MEM_PROTECT:
        return (psci->version_major >= 1 && psci->version_minor >= 2) ? 0 : PSCI_NOT_SUPPORTED;

    default:
        return PSCI_NOT_SUPPORTED;
    }
}

/* ??? L7: Diagnostics ?????????????????????????????????????? */

void psci_print_info(const PSCIFirmware *psci)
{
    if (!psci) return;

    printf("=== PSCI Firmware ===\n");
    printf("Version:      %u.%u\n", psci->version_major, psci->version_minor);
    printf("CPUs:         %u\n", psci->num_cpus);
    printf("Suspended:    %s\n", psci->system_suspended ? "Yes" : "No");
    printf("Total Suspends: %u\n", psci->total_suspend_count);
    printf("CPU ON calls:   %u\n", psci->total_cpu_on_count);
    printf("CPU OFF calls:  %u\n", psci->total_cpu_off_count);
    printf("System Resets:  %llu\n", (unsigned long long)psci->reset_count);
}

void psci_print_cpu_states(const PSCIFirmware *psci)
{
    if (!psci) return;

    printf("=== PSCI CPU States ===\n");
    for (uint32_t i = 0; i < psci->num_cpus; i++) {
        const PSCICPU *cpu = &psci->cpus[i];
        const char *state_str;
        switch (cpu->state) {
        case AFFINITY_STATE_ON:          state_str = "ON"; break;
        case AFFINITY_STATE_OFF:         state_str = "OFF"; break;
        case AFFINITY_STATE_ON_PENDING:  state_str = "ON_PENDING"; break;
        case AFFINITY_STATE_SUSPEND:     state_str = "SUSPEND"; break;
        case AFFINITY_STATE_RECOVERING:  state_str = "RECOVERING"; break;
        default:                         state_str = "UNKNOWN"; break;
        }
        printf("  CPU%2u: MPIDR=0x%016llX State=%s Entry=0x%08llX Hotplug=%s\n",
               cpu->cpu_id, (unsigned long long)cpu->mpidr, state_str,
               (unsigned long long)cpu->entry_point,
               cpu->hotplug_capable ? "Yes" : "No");
    }
}
