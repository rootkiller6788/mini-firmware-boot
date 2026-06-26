#ifndef BOOT_POLICY_H
#define BOOT_POLICY_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Boot Policy Manager ！ UEFI Boot Device Selection
 * Reference: UEFI Specification v2.10, 3.1 ！ Boot Manager
 *
 * L1: Core definitions ！ BootOption, BootOrder, NVRAM variable format.
 * L2: The Boot Manager selects which EFI application to launch based on
 *     user-configured boot options stored in non-volatile variables.
 * L3: Priority queue implementation for BootOrder traversal.
 * L6: Canonical problem ！ boot device enumeration and selection.
 */

#define LOAD_OPTION_ACTIVE            0x00000001
#define LOAD_OPTION_FORCE_RECONNECT   0x00000002
#define LOAD_OPTION_HIDDEN            0x00000008
#define LOAD_OPTION_CATEGORY          0x00001F00
#define LOAD_OPTION_CATEGORY_BOOT     0x00000000
#define LOAD_OPTION_CATEGORY_APP      0x00000100

#define MAX_BOOT_OPTIONS      16
#define MAX_DESCRIPTION_LEN   64
#define MAX_FILE_PATH_LEN     128
#define MAX_OPTIONAL_DATA_LEN 256

typedef struct {
    uint32_t attributes;
    uint16_t file_path_length;
    char     description[MAX_DESCRIPTION_LEN];
    uint8_t  file_path[MAX_FILE_PATH_LEN];
    uint8_t  optional_data[MAX_OPTIONAL_DATA_LEN];
    uint32_t optional_data_size;
    uint16_t option_number;
    bool     valid;
} BootOption;

#define MAX_BOOT_ORDER_ENTRIES  MAX_BOOT_OPTIONS

typedef struct {
    uint16_t option_numbers[MAX_BOOT_ORDER_ENTRIES];
    uint32_t count;
} BootOrder;

#define VAR_ATTR_NON_VOLATILE          0x00000001
#define VAR_ATTR_BOOTSERVICE_ACCESS    0x00000002
#define VAR_ATTR_RUNTIME_ACCESS        0x00000004
#define VAR_ATTR_HARDWARE_ERROR_RECORD 0x00000008
#define VAR_ATTR_AUTHENTICATED_WRITE   0x00000010
#define VAR_ATTR_TIME_BASED_AUTH       0x00000020
#define VAR_ATTR_APPEND_WRITE          0x00000040
#define VAR_ATTR_ENHANCED_AUTH         0x00000080

#define MAX_VAR_NAME_LEN    64
#define MAX_VAR_DATA_SIZE   512

typedef struct {
    char     name[MAX_VAR_NAME_LEN];
    uint8_t  vendor_guid[16];
    uint32_t attributes;
    uint32_t data_size;
    uint8_t  data[MAX_VAR_DATA_SIZE];
    bool     valid;
} NVRAMVariable;

#define MAX_NVRAM_VARS      32

typedef struct {
    BootOption    options[MAX_BOOT_OPTIONS];
    BootOrder     boot_order;
    NVRAMVariable nvram_vars[MAX_NVRAM_VARS];
    uint32_t      nvram_var_count;
    uint16_t      timeout_seconds;
    uint16_t      boot_next;
    bool          boot_manager_menu;
    bool          initialized;
} BootManager;

#define BDS_HOTKEY_NONE         0x00
#define BDS_HOTKEY_SETUP        0x01
#define BDS_HOTKEY_BOOT_MENU    0x02
#define BDS_HOTKEY_SHELL        0x04
#define BDS_HOTKEY_NETWORK_BOOT 0x08
#define BDS_HOTKEY_DEVICE_MGR   0x10

void boot_mgr_init(BootManager *mgr, uint16_t timeout_seconds);

uint16_t boot_option_add(BootManager *mgr, const char *description,
                         const uint8_t *device_path, uint16_t path_len,
                         uint32_t attributes);

bool boot_option_remove(BootManager *mgr, uint16_t option_number);

int32_t boot_option_find(const BootManager *mgr, const char *description);

bool boot_option_validate(const BootOption *opt);

void boot_order_load(BootManager *mgr, const uint16_t *order, uint32_t count);

bool boot_order_save(BootManager *mgr);

uint16_t boot_manager_select(BootManager *mgr);

uint8_t boot_hotkey_detect(void);

uint16_t boot_next_check(const BootManager *mgr);

void boot_next_set(BootManager *mgr, uint16_t option_number);

bool nvram_get_variable(const BootManager *mgr, const char *name,
                        const uint8_t vendor_guid[16],
                        uint8_t *data, uint32_t *data_size,
                        uint32_t *attributes);

bool nvram_set_variable(BootManager *mgr, const char *name,
                        const uint8_t vendor_guid[16],
                        const uint8_t *data, uint32_t data_size,
                        uint32_t attributes);

void boot_mgr_print_options(const BootManager *mgr);

void boot_mgr_print_nvram(const BootManager *mgr);

void boot_option_print(const BootOption *opt);

#endif /* BOOT_POLICY_H */
