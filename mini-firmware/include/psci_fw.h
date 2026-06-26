#ifndef PSCI_FW_H
#define PSCI_FW_H

/*
 * psci_fw.h -- ARM Power State Coordination Interface (PSCI)
 *
 * Implements ARM PSCI specification used by:
 *   - ARM Trusted Firmware-A (BL31 / EL3 runtime)
 *   - Linux kernel PSCI client
 *   - Hypervisors (KVM, Xen)
 *
 * Reference: ARM PSCI Specification v1.2 (DEN 0022E)
 *
 * Knowledge coverage:
 *   L1: PSCI function IDs, power state, affinity level structs
 *   L2: Power management concept (CPU hotplug, suspend, system reset)
 *   L3: PSCI state machine with SMCCC calling convention
 *   L4: Safety/liveness properties of power state machine
 *   L7: CPU hotplug and power management in datacenter
 *   L8: Secure Monitor Call (SMC) convention
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ??? L1: PSCI Function IDs (SMC32/SMC64) ?????????????????????????? */

/*
 * PSCI functions are called via SMC (Secure Monitor Call) or HVC
 * (Hypervisor Call) instructions, passing the function ID in r0/w0.
 * The SMC Calling Convention (SMCCC) is defined in ARM DEN 0028.
 */

typedef enum {
    PSCI_VERSION              = 0x84000000,
    PSCI_CPU_SUSPEND          = 0xC4000001,
    PSCI_CPU_OFF              = 0x84000002,
    PSCI_CPU_ON               = 0xC4000003,
    PSCI_AFFINITY_INFO        = 0xC4000004,
    PSCI_MIGRATE              = 0xC4000005,
    PSCI_MIGRATE_INFO_TYPE    = 0x84000006,
    PSCI_MIGRATE_INFO_UP_CPU  = 0xC4000007,
    PSCI_SYSTEM_OFF           = 0x84000008,
    PSCI_SYSTEM_RESET         = 0x84000009,
    PSCI_FEATURES             = 0x8400000A,
    PSCI_CPU_FREEZE           = 0x8400000B,
    PSCI_CPU_DEFAULT_SUSPEND  = 0xC400000C,
    PSCI_NODE_HW_STATE        = 0xC400000D,
    PSCI_SYSTEM_SUSPEND       = 0xC400000E,
    PSCI_SET_SUSPEND_MODE     = 0x8400000F,
    PSCI_STAT_RESIDENCY       = 0xC4000010,
    PSCI_STAT_COUNT           = 0xC4000011,
    PSCI_SYSTEM_RESET2        = 0xC4000012,
    PSCI_MEM_PROTECT          = 0x84000013,
    PSCI_MEM_PROTECT_RANGE    = 0xC4000014
} PSCIFunctionID;

/* ??? L1: PSCI Return Codes ?????????????????????????????????????????? */

typedef enum {
    PSCI_SUCCESS            =  0,
    PSCI_NOT_SUPPORTED      = -1,
    PSCI_INVALID_PARAMS     = -2,
    PSCI_DENIED             = -3,
    PSCI_ALREADY_ON         = -4,
    PSCI_ON_PENDING         = -5,
    PSCI_INTERNAL_FAILURE   = -6,
    PSCI_NOT_PRESENT        = -7,
    PSCI_DISABLED           = -8,
    PSCI_INVALID_ADDRESS    = -9
} PSCIReturnCode;

/* ??? L1: Power State Format ???????????????????????????????????????? */

/*
 * PSCI power state encoding (32-bit composite):
 *   [31:30] = StateType (0=standby, 1=powerdown)
 *   [29:28] = StateID format (0=original, 1=extended)
 *   [27:24] = Affinity level (0=core, 1=cluster, 2=system)
 *   [23:0]  = StateID (implementation-defined, 0=default)
 */

typedef enum {
    POWER_STATE_TYPE_STANDBY   = 0,
    POWER_STATE_TYPE_POWERDOWN = 1
} PowerStateType;

typedef enum {
    AFFINITY_LEVEL_CPU    = 0,
    AFFINITY_LEVEL_CLUSTER = 1,
    AFFINITY_LEVEL_SYSTEM  = 2
} AffinityLevel;

typedef struct {
    PowerStateType state_type;
    AffinityLevel  affinity_level;
    uint32_t       state_id;       /* Platform-specific sleep state     */
    bool           is_extended;    /* Extended StateID format?          */
} PSCIPowerState;

/* ??? L1: Affinity Info ???????????????????????????????????????????? */

typedef enum {
    AFFINITY_STATE_ON           = 0,
    AFFINITY_STATE_OFF          = 1,
    AFFINITY_STATE_ON_PENDING   = 2,
    AFFINITY_STATE_SUSPEND      = 3,
    AFFINITY_STATE_RECOVERING   = 4
} AffinityState;

/* ??? L1: CPU Descriptor ??????????????????????????????????????????? */

typedef struct {
    uint64_t       mpidr;           /* Multi-Processor Affinity Register */
    uint32_t       cpu_id;          /* Logical CPU number (0..N-1)       */
    AffinityState  state;           /* Current power state               */
    uint64_t       entry_point;     /* Resume address for CPU_ON         */
    uint64_t       context_id;      /* Context ID for CPU_ON             */
    bool           hotplug_capable; /* Can this CPU be hotplugged?       */
    uint32_t       suspend_count;   /* Number of suspend operations      */
} PSCICPU;

/* ??? L1: PSCI Firmware Instance ??????????????????????????????????? */

#define PSCI_MAX_CPUS   256

typedef struct {
    uint32_t       version_major;   /* PSCI specification version    */
    uint32_t       version_minor;
    PSCICPU        cpus[PSCI_MAX_CPUS];
    uint32_t       num_cpus;
    bool           system_suspended; /* Is entire system suspended?  */
    uint32_t       total_suspend_count;
    uint32_t       total_cpu_on_count;
    uint32_t       total_cpu_off_count;
    uint64_t       reset_count;      /* Number of system resets      */
} PSCIFirmware;

/* ??? L2/L3: PSCI API ??????????????????????????????????????????????? */

/* Initialize PSCI firmware with specified number of CPUs */
bool psci_init(PSCIFirmware *psci, uint32_t version_major,
               uint32_t version_minor);

/* Register a CPU with its MPIDR */
bool psci_register_cpu(PSCIFirmware *psci, uint64_t mpidr,
                       uint32_t cpu_id, uint64_t entry_point);

/* ??? L3: PSCI Core Functions ??????????????????????????????????????? */

/*
 * PSCI_VERSION ? Returns PSCI version implemented by firmware.
 * Return: (major << 16) | minor
 */
int32_t psci_call_version(const PSCIFirmware *psci);

/*
 * PSCI_CPU_OFF ? Power down the calling CPU.
 * Must be called from the CPU being powered off.
 * After this call, the CPU enters OFF state.
 * The calling CPU must have saved its context.
 *
 * Safety property: A CPU cannot power off another CPU.
 * Liveness property: CPU_OFF cannot be denied without cause.
 */
int32_t psci_call_cpu_off(PSCIFirmware *psci, uint32_t cpu_id);

/*
 * PSCI_CPU_ON ? Power on a target CPU.
 * Arguments:
 *   target_cpu: MPIDR of CPU to power on
 *   entry_point: Physical address of first instruction
 *   context_id: Platform-specific context ID
 *
 * Invariant: target_cpu must be in OFF state.
 * Postcondition: target_cpu transitions to ON_PENDING then ON.
 */
int32_t psci_call_cpu_on(PSCIFirmware *psci, uint64_t mpidr,
                         uint64_t entry_point, uint64_t context_id);

/*
 * PSCI_CPU_SUSPEND ? Suspend the calling CPU.
 * power_state: Encoded power state (see PSCIPowerState)
 * entry_point: Resume address
 * context_id: Context ID for resume
 *
 * The CPU may save state and enter low-power mode.
 * On wakeup, execution resumes at entry_point.
 */
int32_t psci_call_cpu_suspend(PSCIFirmware *psci, uint32_t cpu_id,
                              uint32_t power_state, uint64_t entry_point,
                              uint64_t context_id);

/*
 * PSCI_AFFINITY_INFO ? Query the state of a CPU.
 * Returns: Affinity state (ON, OFF, ON_PENDING, SUSPEND)
 */
int32_t psci_call_affinity_info(const PSCIFirmware *psci, uint64_t mpidr);

/*
 * PSCI_SYSTEM_OFF ? Power down the entire system.
 * This is the final firmware call before system power-off.
 * Implementation: Firmware notifies power controller, then WFI loop.
 */
int32_t psci_call_system_off(PSCIFirmware *psci);

/*
 * PSCI_SYSTEM_RESET ? Reset entire system (cold boot).
 * Same as power-on reset but without power cycling.
 * Used by Linux kernel panic handler and reboot syscall.
 */
int32_t psci_call_system_reset(PSCIFirmware *psci);

/*
 * PSCI_FEATURES ? Check if a PSCI function is supported.
 * Returns: 0 if unsupported, >0 with feature-specific flags.
 */
int32_t psci_call_features(const PSCIFirmware *psci, uint32_t func_id);

/* ??? L7: Diagnostics ??????????????????????????????????????????????? */

void psci_print_info(const PSCIFirmware *psci);
void psci_print_cpu_states(const PSCIFirmware *psci);

#endif /* PSCI_FW_H */
