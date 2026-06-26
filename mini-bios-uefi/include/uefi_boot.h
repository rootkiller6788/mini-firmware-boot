#ifndef UEFI_BOOT_H
#define UEFI_BOOT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define EFI_SYSTEM_TABLE_SIGNATURE      0x5453595320494249ULL
#define EFI_BOOT_SERVICES_SIGNATURE     0x565245535F4F4F42ULL
#define EFI_RUNTIME_SERVICES_SIGNATURE  0x56524E455F545255ULL
#define EFI_SYSTEM_TABLE_REVISION_2_10  0x0002000A
#define MAX_PROTOCOL_ENTRIES            128
#define EFI_PAGE_SIZE                   4096
#define EFI_PAGE_MASK                   0xFFF

typedef void* EFIHandle;

typedef struct EFISystemTable     EFISystemTable;
typedef struct EFIBootServices    EFIBootServices;
typedef struct EFIRuntimeServices EFIRuntimeServices;

#pragma pack(push, 1)

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];
} EFIGuid;

#pragma pack(pop)

typedef uint64_t EFIStatus;
#define EFI_SUCCESS               0x0000000000000000ULL
#define EFI_LOAD_ERROR            0x8000000000000001ULL
#define EFI_INVALID_PARAMETER     0x8000000000000002ULL
#define EFI_UNSUPPORTED           0x8000000000000003ULL
#define EFI_BAD_BUFFER_SIZE       0x8000000000000004ULL
#define EFI_BUFFER_TOO_SMALL      0x8000000000000005ULL
#define EFI_NOT_READY             0x8000000000000006ULL
#define EFI_OUT_OF_RESOURCES      0x8000000000000009ULL
#define EFI_NOT_FOUND             0x800000000000000EULL
#define EFI_ACCESS_DENIED         0x800000000000000FULL
#define EFI_NO_MAPPING            0x8000000000000011ULL

typedef uint64_t EFIPhysicalAddress;
typedef uint64_t EFIVirtualAddress;

typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMaxMemoryType
} EFIMemoryType;

typedef enum {
    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
    MaxAllocateType
} EFIAllocateType;

typedef struct {
    uint32_t (*reset)(void *self, bool extended);
    uint32_t (*output_string)(void *self, const uint16_t *str);
    uint32_t (*test_string)(void *self, const uint16_t *str);
    uint32_t (*query_mode)(void *self, uint64_t mode, uint64_t *cols, uint64_t *rows);
    uint32_t (*set_mode)(void *self, uint64_t mode);
    uint32_t (*set_attribute)(void *self, uint64_t attr);
    uint32_t (*clear_screen)(void *self);
} EFISimpleTextOutputProtocol;

typedef struct {
    uint32_t (*reset)(void *self, bool extended);
    uint32_t (*read_key)(void *self, uint16_t *key);
} EFISimpleTextInputProtocol;

typedef struct {
    EFIGuid guid;
    void    *protocol_interface;
} EFIProtocol;

typedef struct EFIProtocolEntry {
    EFIGuid guid;
    void    *interface;
    EFIHandle handle;
    struct EFIProtocolEntry *next;
} EFIProtocolEntry;

typedef struct EFIBootServices {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;

    EFIStatus (*raise_tpl)(uint64_t new_tpl);
    void      (*restore_tpl)(uint64_t old_tpl);

    EFIStatus (*allocate_pages)(EFIAllocateType type, EFIMemoryType mem_type,
                                uint64_t pages, EFIPhysicalAddress *memory);
    EFIStatus (*free_pages)(EFIPhysicalAddress memory, uint64_t pages);
    EFIStatus (*get_memory_map)(uint64_t *size, void *map, uint64_t *key,
                                uint64_t *desc_size, uint32_t *desc_version);
    EFIStatus (*allocate_pool)(EFIMemoryType pool_type, uint64_t size, void **buffer);
    EFIStatus (*free_pool)(void *buffer);

    EFIStatus (*create_event)(uint32_t type, uint64_t notify_tpl,
                              void *notify_fn, void *ctx, void **event);
    EFIStatus (*set_timer)(void *event, uint64_t type, uint64_t trigger);
    EFIStatus (*wait_for_event)(uint64_t num_events, void **events, uint64_t *index);
    EFIStatus (*signal_event)(void *event);
    EFIStatus (*close_event)(void *event);
    EFIStatus (*check_event)(void *event);

    EFIStatus (*install_protocol_interface)(EFIBootServices *bs, EFIHandle *handle,
                                            EFIGuid *protocol, void *interface);
    EFIStatus (*reinstall_protocol_interface)(EFIHandle handle, EFIGuid *protocol,
                                              void *old_interface, void *new_interface);
    EFIStatus (*uninstall_protocol_interface)(EFIHandle handle, EFIGuid *protocol,
                                              void *interface);
    EFIStatus (*handle_protocol)(EFIHandle handle, EFIGuid *protocol, void **interface);

    void      *reserved;
    EFIStatus (*register_protocol_notify)(EFIGuid *protocol, void *event,
                                          void **registration);
    EFIStatus (*locate_handle)(uint64_t search_type, EFIGuid *protocol,
                               void *key, uint64_t *size, EFIHandle *buffer);
    EFIStatus (*locate_device_path)(EFIGuid *protocol, void **path, EFIHandle *device);
    EFIStatus (*install_configuration_table)(EFIGuid *guid, void *table);

    EFIStatus (*load_image)(bool boot_policy, EFIHandle parent, void *path,
                            void *src, uint64_t size, EFIHandle *image);
    EFIStatus (*start_image)(EFIHandle handle, uint64_t *exit_size, uint16_t **exit_data);

    EFIStatus (*exit_boot)(EFIHandle handle, EFIStatus status,
                           uint64_t data_size, uint16_t *data);
    EFIStatus (*unload_image)(EFIHandle handle);
    EFIStatus (*exit_boot_services)(EFIBootServices *bs, EFIHandle image_handle, uint64_t map_key);

    uint64_t (*get_next_monotonic_count)(uint64_t *count);
    EFIStatus (*stall)(uint64_t microseconds);
    void      (*set_watchdog_timer)(uint64_t timeout, uint64_t code,
                                    uint64_t data_size, uint16_t *data);

    EFIStatus (*connect_controller)(EFIHandle handle, EFIHandle *driver,
                                    void *path, bool recursive);
    EFIStatus (*disconnect_controller)(EFIHandle handle, EFIHandle driver,
                                       EFIHandle child);

    EFIProtocolEntry *protocol_db;
    uint32_t protocol_count;

    EFIStatus (*open_protocol)(EFIHandle handle, EFIGuid *protocol,
                               void **interface, EFIHandle agent,
                               EFIHandle controller, uint32_t attr);
    EFIStatus (*close_protocol)(EFIHandle handle, EFIGuid *protocol,
                                EFIHandle agent, EFIHandle controller);
    EFIStatus (*open_protocol_information)(EFIHandle handle, EFIGuid *protocol,
                                           void **entry_buf, uint64_t *entry_count);

    EFIStatus (*protocols_per_handle)(EFIHandle handle, void ***protocol_buffer,
                                      uint64_t *protocol_buffer_count);
    EFIStatus (*locate_handle_buffer)(uint64_t search_type, EFIGuid *protocol,
                                      void *key, uint64_t *num_handles,
                                      EFIHandle **buffer);
    EFIStatus (*locate_protocol)(EFIBootServices *bs, EFIGuid *protocol,
                                 void *registration, void **interface);
    EFIStatus (*install_multiple_protocol_interfaces)(EFIHandle *handle, void *first, ...);
    EFIStatus (*uninstall_multiple_protocol_interfaces)(EFIHandle handle, void *first, ...);

    EFIStatus (*calculate_crc32)(void *data, uint64_t size, uint32_t *crc32);

    void (*copy_mem)(void *dst, void *src, uint64_t len);
    void (*set_mem)(void *buf, uint64_t len, uint8_t value);
} EFIBootServices;

typedef struct EFIRuntimeServices {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    void *get_time;
    void *set_time;
    void *get_wakeup_time;
    void *set_wakeup_time;
    void *set_virtual_address_map;
    void *convert_pointer;
    void *get_variable;
    void *get_next_variable_name;
    void *set_variable;
    void *get_next_high_mono_count;
    void *reset_system;
    void *update_capsule;
    void *query_capsule_capabilities;
    void *query_variable_info;
} EFIRuntimeServices;

typedef struct {
    EFIGuid vendor_guid;
    void   *vendor_table;
} EFIConfigurationTable;

typedef struct EFISystemTable {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;

    void    *firmware_vendor;
    uint32_t firmware_revision;

    EFIHandle console_in_handle;
    EFISimpleTextInputProtocol  *con_in;

    EFIHandle console_out_handle;
    EFISimpleTextOutputProtocol *con_out;

    EFIHandle standard_error_handle;
    EFISimpleTextOutputProtocol *std_err;

    EFIRuntimeServices *runtime_services;
    EFIBootServices    *boot_services;

    uint64_t num_table_entries;
    EFIConfigurationTable *config_table;
} EFISystemTable;

void      uefi_init_system_table(EFISystemTable *st);
EFIStatus uefi_install_protocol(EFIBootServices *bs, EFIHandle *handle,
                                EFIGuid *protocol, void *interface);
EFIStatus uefi_locate_protocol(EFIBootServices *bs, EFIGuid *protocol,
                                void *registration, void **interface);
EFIStatus uefi_exit_boot_services(EFIBootServices *bs, EFIHandle image_handle,
                                  uint64_t map_key);
EFIStatus uefi_allocate_pool_stub(EFIMemoryType pool_type, uint64_t size, void **buffer);
EFIStatus uefi_free_pool_stub(void *buffer);
EFIStatus uefi_install_configuration_table(EFISystemTable *st, EFIGuid *guid, void *table);
void      uefi_collect_garbage_protocols(EFIBootServices *bs);
void      uefi_print_system_table(const EFISystemTable *st);
void      uefi_print_guid(const EFIGuid *guid);

#endif /* UEFI_BOOT_H */
