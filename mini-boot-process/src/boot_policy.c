#include "boot_policy.h"
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Boot Manager Initialization - L1/L3
 *
 * Initializes the BDS (Boot Device Selection) phase state machine.
 * Sets default timeout, clears all boot options, and prepares the
 * NVRAM variable store for boot configuration.
 *
 * L3: The BootManager is the central state structure for BDS.
 *     It integrates boot options, boot order priority queue, and
 *     NVRAM variable storage into a single management interface.
 * ============================================================================ */

void boot_mgr_init(BootManager *mgr, uint16_t timeout_seconds)
{
    if (!mgr) return;

    memset(mgr, 0, sizeof(BootManager));
    mgr->timeout_seconds = timeout_seconds;
    mgr->boot_next = 0;
    mgr->boot_manager_menu = false;
    mgr->initialized = true;
    mgr->nvram_var_count = 0;

    printf("[BDS] Boot Manager initialized. Timeout: %u seconds\n",
           timeout_seconds);
}

/* ============================================================================
 * Boot Option Management - L1/L3
 *
 * L1: Boot options follow the EFI_LOAD_OPTION format (UEFI Spec 3.1.3).
 * L3: Options are stored in a fixed-size array indexed by option_number
 *     for O(1) lookup. The BootOrder array defines priority ordering.
 *
 * Each boot option contains:
 *   - attributes (ACTIVE, HIDDEN, CATEGORY)
 *   - description string (e.g. "UEFI Hard Drive")
 *   - device path (binary EFI Device Path protocol encoding)
 *   - optional data (platform-specific)
 * ============================================================================ */

uint16_t boot_option_add(BootManager *mgr, const char *description,
                         const uint8_t *device_path, uint16_t path_len,
                         uint32_t attributes)
{
    if (!mgr || !mgr->initialized) return 0;

    /* Find first available slot or next sequential option number */
    uint16_t opt_num = 0;
    for (uint16_t i = 0; i < MAX_BOOT_OPTIONS; i++) {
        if (!mgr->options[i].valid) {
            opt_num = (i == 0) ? 1 : (uint16_t)(i + 1);
            break;
        }
    }
    if (opt_num == 0) {
        printf("[BDS] ERROR: No free boot option slots (max %d)\n", MAX_BOOT_OPTIONS);
        return 0;
    }

    uint16_t idx = opt_num - 1;
    BootOption *opt = &mgr->options[idx];

    memset(opt, 0, sizeof(BootOption));
    opt->option_number = opt_num;
    opt->attributes = attributes;

    if (description) {
        strncpy(opt->description, description, MAX_DESCRIPTION_LEN - 1);
        opt->description[MAX_DESCRIPTION_LEN - 1] = '\0';
    }

    if (device_path && path_len > 0) {
        uint16_t copy_len = (path_len > MAX_FILE_PATH_LEN) ? MAX_FILE_PATH_LEN : path_len;
        memcpy(opt->file_path, device_path, copy_len);
        opt->file_path_length = copy_len;
    }

    opt->valid = true;

    printf("[BDS] Added Boot%04X: \"%s\" (attr=0x%08X)\n",
           opt_num, opt->description, attributes);

    return opt_num;
}

bool boot_option_remove(BootManager *mgr, uint16_t option_number)
{
    if (!mgr || option_number == 0 || option_number > MAX_BOOT_OPTIONS) return false;

    uint16_t idx = option_number - 1;
    if (!mgr->options[idx].valid) return false;

    mgr->options[idx].valid = false;

    printf("[BDS] Removed Boot%04X\n", option_number);

    /* Remove from boot order */
    for (uint32_t i = 0; i < mgr->boot_order.count; i++) {
        if (mgr->boot_order.option_numbers[i] == option_number) {
            /* Shift remaining entries left */
            for (uint32_t j = i; j < mgr->boot_order.count - 1; j++) {
                mgr->boot_order.option_numbers[j] = mgr->boot_order.option_numbers[j + 1];
            }
            mgr->boot_order.count--;
            break;
        }
    }

    return true;
}

int32_t boot_option_find(const BootManager *mgr, const char *description)
{
    if (!mgr || !description) return -1;

    for (uint16_t i = 0; i < MAX_BOOT_OPTIONS; i++) {
        if (mgr->options[i].valid
            && strstr(mgr->options[i].description, description)) {
            return (int32_t)i;
        }
    }
    return -1;
}

/* ============================================================================
 * Boot Option Validation - L6
 *
 * Canonical problem: verify that a boot option's device path contains
 * a valid bootable image. In real UEFI, this involves:
 *   1. Parsing the EFI_DEVICE_PATH_PROTOCOL to locate the storage device
 *   2. Opening the file system on that device
 *   3. Checking for the presence of \EFI\BOOT\BOOT{ARCH}.EFI
 *
 * L6: Boot device selection is the canonical problem of BDS.
 *     The firmware must reliably find and validate bootable images
 *     across diverse storage media (SATA, NVMe, USB, PXE).
 *
 * Here we validate the description and path length as a simulation.
 * ============================================================================ */

bool boot_option_validate(const BootOption *opt)
{
    if (!opt || !opt->valid) return false;

    /* Active bit must be set */
    if (!(opt->attributes & LOAD_OPTION_ACTIVE)) {
        printf("[BDS] Boot%04X: not active (skipped)\n", opt->option_number);
        return false;
    }

    /* Must have a file path */
    if (opt->file_path_length == 0) {
        printf("[BDS] Boot%04X: no device path (skipped)\n", opt->option_number);
        return false;
    }

    /* Hidden options are skipped in normal boot */
    if (opt->attributes & LOAD_OPTION_HIDDEN) {
        printf("[BDS] Boot%04X: hidden (skipped)\n", opt->option_number);
        return false;
    }

    return true;
}

/* ============================================================================
 * Boot Order Management - L3/L5
 *
 * BootOrder is a UEFI variable containing a priority-ordered list of
 * Boot#### option numbers. The Boot Manager iterates through this list
 * in order, attempting to boot each option until one succeeds.
 *
 * L5: Priority queue algorithm - O(n) sequential traversal of ordered
 *     option list. BootOrder defines the priority; the Boot Manager
 *     is the consumer.
 * ============================================================================ */

void boot_order_load(BootManager *mgr, const uint16_t *order, uint32_t count)
{
    if (!mgr || !order || count == 0) return;

    if (count > MAX_BOOT_ORDER_ENTRIES) count = MAX_BOOT_ORDER_ENTRIES;

    memcpy(mgr->boot_order.option_numbers, order, count * sizeof(uint16_t));
    mgr->boot_order.count = count;

    printf("[BDS] BootOrder loaded: %u entries\n", count);
}

bool boot_order_save(BootManager *mgr)
{
    if (!mgr || !mgr->initialized) return false;

    /* Save BootOrder as NVRAM variable:
     *   Name: "BootOrder"
     *   GUID: EFI_GLOBAL_VARIABLE = {8BE4DF61-93CA-11d2-AA0D-00E098032B8C}
     *   Data: uint16_t[] array
     */
    const uint8_t global_var_guid[16] = {
        0x61, 0xDF, 0xE4, 0x8B, 0xCA, 0x93, 0xD2, 0x11,
        0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C
    };

    return nvram_set_variable(mgr, "BootOrder", global_var_guid,
                              (const uint8_t *)mgr->boot_order.option_numbers,
                              mgr->boot_order.count * sizeof(uint16_t),
                              VAR_ATTR_NON_VOLATILE
                              | VAR_ATTR_BOOTSERVICE_ACCESS
                              | VAR_ATTR_RUNTIME_ACCESS);
}

/* ============================================================================
 * Boot Manager Selection Algorithm - L5/L6
 *
 * L5: The Boot Manager iterates through BootOrder entries in priority
 *     order. For each entry, it:
 *       1. Looks up the Boot#### option
 *       2. Validates it (active, non-hidden, has device path)
 *       3. Attempts to boot (simulated here as printf + success)
 *
 * L6: Canonical problem - this is the core of BDS (Boot Device Selection).
 *     The algorithm must handle:
 *       - Missing/invalid options (graceful skip)
 *       - BootNext one-time override
 *       - Hotkey detection (F2=Setup, F12=Boot Menu)
 *       - Timeout for user interaction
 *       - Fallback to Boot Manager menu if all options fail
 *
 * Complexity: O(n) where n = number of entries in BootOrder.
 * ============================================================================ */

uint16_t boot_manager_select(BootManager *mgr)
{
    if (!mgr || !mgr->initialized) return 0;

    printf("\n[BDS] === Boot Manager Selection ===\n");
    printf("[BDS] Timeout: %u seconds\n", mgr->timeout_seconds);

    /* Check for hotkeys */
    uint8_t hotkey = boot_hotkey_detect();
    if (hotkey & BDS_HOTKEY_BOOT_MENU) {
        printf("[BDS] F12 pressed ? entering Boot Manager Menu\n");
        mgr->boot_manager_menu = true;
    }
    if (hotkey & BDS_HOTKEY_SETUP) {
        printf("[BDS] F2 pressed ? entering Setup Utility\n");
        return 0;  /* Setup takes precedence */
    }

    /* Check BootNext one-time override */
    uint16_t next = boot_next_check(mgr);
    if (next != 0) {
        uint16_t idx = next - 1;
        if (mgr->options[idx].valid && boot_option_validate(&mgr->options[idx])) {
            printf("[BDS] BootNext=%04X: booting \"%s\"\n",
                   next, mgr->options[idx].description);
            return next;
        }
    }

    /* Iterate BootOrder */
    if (mgr->boot_order.count == 0) {
        printf("[BDS] No boot options configured. Entering Boot Manager Menu.\n");
        return 0;
    }

    for (uint32_t i = 0; i < mgr->boot_order.count; i++) {
        uint16_t opt_num = mgr->boot_order.option_numbers[i];
        if (opt_num == 0 || opt_num > MAX_BOOT_OPTIONS) continue;

        uint16_t idx = opt_num - 1;
        if (!mgr->options[idx].valid) {
            printf("[BDS] Boot%04X: option not present (skipped)\n", opt_num);
            continue;
        }

        printf("[BDS] Trying Boot%04X: \"%s\"... ", opt_num, mgr->options[idx].description);

        if (boot_option_validate(&mgr->options[idx])) {
            printf("SUCCESS\n");
            return opt_num;
        }

        printf("FAILED\n");
    }

    printf("[BDS] All boot options failed. Entering Boot Manager Menu.\n");
    return 0;
}

/* ============================================================================
 * Hotkey Detection - L7
 *
 * In real UEFI firmware, the BDS phase polls the keyboard for hotkey
 * presses during a configurable timeout window. Common hotkeys:
 *   F2/DEL ? Setup Utility
 *   F12    ? Boot Manager Menu (one-time boot override)
 *   F7     ? UEFI Shell
 *   F10    ? Save & Exit (in Setup)
 *
 * L7 Application: This is the user-facing interface for firmware
 * configuration. Every PC user has seen the "Press F2 for Setup"
 * prompt during POST.
 * ============================================================================ */

uint8_t boot_hotkey_detect(void)
{
    /* In simulation, no hotkey is pressed by default.
     * In real firmware, this would poll EFI_SIMPLE_TEXT_INPUT_PROTOCOL
     * for keystroke events during the BDS timeout window. */
    return BDS_HOTKEY_NONE;
}

/* ============================================================================
 * BootNext One-Time Boot Override - L7
 *
 * BootNext is a UEFI variable that specifies a Boot#### option number
 * to boot once, ignoring the normal BootOrder. After the boot attempt
 * (successful or not), BootNext is cleared. This is used for:
 *   - OS-initiated reboot into firmware setup
 *   - One-time network boot for OS deployment
 *   - Diagnostic boot to alternate OS
 *
 * L7 Application: Windows uses BootNext for "Advanced Startup" options
 * and reboot-to-UEFI functionality (via SetFirmwareEnvironmentVariable).
 * ============================================================================ */

uint16_t boot_next_check(const BootManager *mgr)
{
    if (!mgr) return 0;
    if (mgr->boot_next == 0) return 0;

    printf("[BDS] BootNext=%04X (one-time boot override)\n", mgr->boot_next);
    return mgr->boot_next;
}

void boot_next_set(BootManager *mgr, uint16_t option_number)
{
    if (!mgr) return;
    mgr->boot_next = option_number;
    printf("[BDS] BootNext set to %04X\n", option_number);
}

/* ============================================================================
 * NVRAM Variable Access - L5/L7
 *
 * UEFI variables are stored in non-volatile RAM, identified by a
 * (name, vendor_guid) pair. Variables have attributes controlling
 * accessibility at boot time (BS) vs. runtime (RT).
 *
 * L7 Application: NVRAM is the persistent configuration store for UEFI.
 *     - Boot#### and BootOrder: boot configuration
 *     - Lang: language preference (e.g. "en-US")
 *     - Timeout: boot manager timeout seconds
 *     - ConIn/ConOut: console device paths
 *     - Setup: BIOS setup configuration (option ROM settings)
 *     - db/dbx/KEK/PK: Secure Boot key databases
 *
 * This is a simulated in-memory store for teaching purposes.
 * ============================================================================ */

bool nvram_get_variable(const BootManager *mgr, const char *name,
                        const uint8_t vendor_guid[16],
                        uint8_t *data, uint32_t *data_size,
                        uint32_t *attributes)
{
    if (!mgr || !name || !data || !data_size) return false;

    for (uint32_t i = 0; i < mgr->nvram_var_count; i++) {
        const NVRAMVariable *v = &mgr->nvram_vars[i];
        if (v->valid && strcmp(v->name, name) == 0
            && memcmp(v->vendor_guid, vendor_guid, 16) == 0) {

            uint32_t copy_size = (v->data_size < *data_size) ? v->data_size : *data_size;
            memcpy(data, v->data, copy_size);
            *data_size = v->data_size;

            if (attributes) {
                *attributes = v->attributes;
            }
            return true;
        }
    }
    return false;
}

bool nvram_set_variable(BootManager *mgr, const char *name,
                        const uint8_t vendor_guid[16],
                        const uint8_t *data, uint32_t data_size,
                        uint32_t attributes)
{
    if (!mgr || !name || !data || data_size > MAX_VAR_DATA_SIZE) return false;

    /* Check if variable already exists (update) */
    for (uint32_t i = 0; i < mgr->nvram_var_count; i++) {
        NVRAMVariable *v = &mgr->nvram_vars[i];
        if (v->valid && strcmp(v->name, name) == 0
            && memcmp(v->vendor_guid, vendor_guid, 16) == 0) {

            /* Update existing */
            memcpy(v->data, data, data_size);
            v->data_size = data_size;
            v->attributes = attributes;
            return true;
        }
    }

    /* New variable */
    if (mgr->nvram_var_count >= MAX_NVRAM_VARS) {
        printf("[NVRAM] ERROR: variable store full\n");
        return false;
    }

    NVRAMVariable *v = &mgr->nvram_vars[mgr->nvram_var_count];
    memset(v, 0, sizeof(NVRAMVariable));
    strncpy(v->name, name, MAX_VAR_NAME_LEN - 1);
    memcpy(v->vendor_guid, vendor_guid, 16);
    memcpy(v->data, data, data_size);
    v->data_size = data_size;
    v->attributes = attributes;
    v->valid = true;
    mgr->nvram_var_count++;

    return true;
}

/* ============================================================================
 * Debug Print Utilities
 * ============================================================================ */

void boot_option_print(const BootOption *opt)
{
    if (!opt || !opt->valid) return;

    printf("  Boot%04X: \"%s\"\n", opt->option_number, opt->description);
    printf("    Attributes: 0x%08X", opt->attributes);
    if (opt->attributes & LOAD_OPTION_ACTIVE)  printf(" ACTIVE");
    if (opt->attributes & LOAD_OPTION_HIDDEN)  printf(" HIDDEN");
    printf("\n");
    printf("    FilePathLen: %u bytes\n", opt->file_path_length);
    if (opt->optional_data_size > 0) {
        printf("    OptionalData: %u bytes\n", opt->optional_data_size);
    }
}

void boot_mgr_print_options(const BootManager *mgr)
{
    if (!mgr) return;

    printf("\n  ===== BOOT OPTIONS =====\n");
    printf("  BootOrder (%u entries): ", mgr->boot_order.count);
    for (uint32_t i = 0; i < mgr->boot_order.count; i++) {
        printf("Boot%04X ", mgr->boot_order.option_numbers[i]);
    }
    printf("\n");

    printf("  BootNext: ");
    if (mgr->boot_next) printf("Boot%04X\n", mgr->boot_next);
    else printf("(none)\n");

    printf("  Timeout: %u seconds\n", mgr->timeout_seconds);
    printf("\n");

    int active_count = 0;
    for (int i = 0; i < MAX_BOOT_OPTIONS; i++) {
        if (mgr->options[i].valid) {
            boot_option_print(&mgr->options[i]);
            active_count++;
        }
    }

    if (active_count == 0) {
        printf("  (no boot options configured)\n");
    }
    printf("  =========================\n");
}

void boot_mgr_print_nvram(const BootManager *mgr)
{
    if (!mgr) return;

    printf("\n  ===== NVRAM VARIABLES =====\n");
    printf("  %u variables stored\n", mgr->nvram_var_count);

    for (uint32_t i = 0; i < mgr->nvram_var_count; i++) {
        const NVRAMVariable *v = &mgr->nvram_vars[i];
        if (!v->valid) continue;

        printf("  [%u] \"%s\"  GUID=", i, v->name);
        for (int j = 0; j < 16; j++) printf("%02X", v->vendor_guid[j]);
        printf("\n");
        printf("      Size=%u  Attr=0x%08X", v->data_size, v->attributes);
        if (v->attributes & VAR_ATTR_NON_VOLATILE)       printf(" NV");
        if (v->attributes & VAR_ATTR_BOOTSERVICE_ACCESS) printf(" BS");
        if (v->attributes & VAR_ATTR_RUNTIME_ACCESS)     printf(" RT");
        printf("\n");
    }
    printf("  ===========================\n");
}
