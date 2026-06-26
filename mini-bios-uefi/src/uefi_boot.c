#include "uefi_boot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t g_map_key_counter = 0;

static EFIStatus stub_raise_tpl(uint64_t new_tpl) {
    (void)new_tpl;
    return EFI_SUCCESS;
}

static void stub_restore_tpl(uint64_t old_tpl) {
    (void)old_tpl;
}

static EFIStatus stub_allocate_pages(EFIAllocateType type, EFIMemoryType mem_type,
                                     uint64_t pages, EFIPhysicalAddress *memory) {
    (void)type; (void)mem_type;
    void *ptr = calloc((size_t)pages, EFI_PAGE_SIZE);
    if (!ptr) return EFI_OUT_OF_RESOURCES;
    *memory = (EFIPhysicalAddress)(uintptr_t)ptr;
    return EFI_SUCCESS;
}

static EFIStatus stub_free_pages(EFIPhysicalAddress memory, uint64_t pages) {
    (void)pages;
    free((void *)(uintptr_t)memory);
    return EFI_SUCCESS;
}

static EFIStatus stub_get_memory_map(uint64_t *size, void *map, uint64_t *key,
                                     uint64_t *desc_size, uint32_t *desc_version) {
    *size = 0;
    if (!map) return EFI_BUFFER_TOO_SMALL;
    *key = ++g_map_key_counter;
    *desc_size = 48;
    *desc_version = 1;
    return EFI_SUCCESS;
}

EFIStatus uefi_allocate_pool_stub(EFIMemoryType pool_type, uint64_t size, void **buffer) {
    (void)pool_type;
    if (!buffer) return EFI_INVALID_PARAMETER;
    *buffer = calloc(1, (size_t)size);
    if (!*buffer) return EFI_OUT_OF_RESOURCES;
    return EFI_SUCCESS;
}

EFIStatus uefi_free_pool_stub(void *buffer) {
    free(buffer);
    return EFI_SUCCESS;
}

static EFIStatus stub_create_event(uint32_t type, uint64_t notify_tpl,
                                   void *notify_fn, void *ctx, void **event) {
    (void)type; (void)notify_tpl; (void)notify_fn; (void)ctx;
    if (event) *event = malloc(64);
    return EFI_SUCCESS;
}

static EFIStatus stub_close_event(void *event) {
    free(event);
    return EFI_SUCCESS;
}

static EFIStatus stub_signal_event(void *event) {
    (void)event;
    return EFI_SUCCESS;
}

static EFIStatus stub_stall(uint64_t microseconds) {
    (void)microseconds;
    return EFI_SUCCESS;
}

static uint64_t stub_get_monotonic_count(uint64_t *count) {
    if (count) *count = 1;
    return 0;
}

static void stub_set_watchdog(uint64_t timeout, uint64_t code,
                              uint64_t data_size, uint16_t *data) {
    (void)timeout; (void)code; (void)data_size; (void)data;
}

static EFIStatus stub_load_image(bool boot_policy, EFIHandle parent, void *path,
                                 void *src, uint64_t size, EFIHandle *image) {
    (void)boot_policy; (void)parent; (void)path; (void)src; (void)size;
    if (image) *image = malloc(1);
    return EFI_SUCCESS;
}

static EFIStatus stub_start_image(EFIHandle handle, uint64_t *exit_size, uint16_t **exit_data) {
    (void)handle;
    if (exit_size) *exit_size = 0;
    if (exit_data) *exit_data = NULL;
    printf("  BootServices->StartImage() called – transferring control to loaded image\n");
    return EFI_SUCCESS;
}

static EFIStatus stub_connect_controller(EFIHandle handle, EFIHandle *driver,
                                        void *path, bool recursive) {
    (void)handle; (void)driver; (void)path; (void)recursive;
    return EFI_SUCCESS;
}

static EFIStatus stub_disconnect_controller(EFIHandle handle, EFIHandle driver, EFIHandle child) {
    (void)handle; (void)driver; (void)child;
    return EFI_SUCCESS;
}

static void stub_copy_mem(void *dst, void *src, uint64_t len) {
    memmove(dst, src, (size_t)len);
}

static void stub_set_mem(void *buf, uint64_t len, uint8_t value) {
    memset(buf, (int)value, (size_t)len);
}

static EFIStatus stub_check_event(void *event) {
    (void)event;
    return EFI_NOT_READY;
}

static EFIStatus stub_set_timer(void *event, uint64_t type, uint64_t trigger) {
    (void)event; (void)type; (void)trigger;
    return EFI_SUCCESS;
}

static EFIStatus stub_wait_for_event(uint64_t num, void **events, uint64_t *idx) {
    (void)num; (void)events;
    if (idx) *idx = 0;
    return EFI_SUCCESS;
}

static EFIStatus stub_open_protocol(EFIHandle handle, EFIGuid *protocol,
                                    void **interface, EFIHandle agent,
                                    EFIHandle controller, uint32_t attr) {
    (void)handle; (void)protocol; (void)interface;
    (void)agent; (void)controller; (void)attr;
    return EFI_UNSUPPORTED;
}

static EFIStatus stub_close_protocol(EFIHandle handle, EFIGuid *protocol,
                                     EFIHandle agent, EFIHandle controller) {
    (void)handle; (void)protocol; (void)agent; (void)controller;
    return EFI_SUCCESS;
}

static EFIStatus stub_register_protocol_notify(EFIGuid *protocol, void *event,
                                               void **registration) {
    (void)protocol; (void)event;
    if (registration) *registration = malloc(1);
    return EFI_SUCCESS;
}

static EFIStatus stub_locate_device_path(EFIGuid *protocol, void **path, EFIHandle *device) {
    (void)protocol; (void)path;
    if (device) *device = malloc(1);
    return EFI_SUCCESS;
}

static EFIStatus stub_protocols_per_handle(EFIHandle handle, void ***buf, uint64_t *count) {
    (void)handle;
    if (buf) *buf = NULL;
    if (count) *count = 0;
    return EFI_SUCCESS;
}

static EFIStatus stub_locate_handle_buffer(uint64_t type, EFIGuid *protocol, void *key,
                                           uint64_t *num, EFIHandle **buf) {
    (void)type; (void)protocol; (void)key;
    if (num) *num = 0;
    if (buf) *buf = NULL;
    return EFI_NOT_FOUND;
}

static EFIStatus stub_calculate_crc32(void *data, uint64_t size, uint32_t *crc32) {
    (void)data; (void)size;
    if (crc32) *crc32 = 0;
    return EFI_SUCCESS;
}

static EFIStatus stub_handle_protocol(EFIHandle handle, EFIGuid *protocol, void **interface) {
    (void)handle; (void)protocol;
    if (interface) *interface = NULL;
    return EFI_NOT_FOUND;
}

static EFIStatus stub_open_protocol_info(EFIHandle handle, EFIGuid *protocol,
                                         void **entry_buf, uint64_t *entry_count) {
    (void)handle; (void)protocol;
    if (entry_buf) *entry_buf = NULL;
    if (entry_count) *entry_count = 0;
    return EFI_NOT_FOUND;
}

void uefi_init_system_table(EFISystemTable *st) {
    if (!st) return;
    memset(st, 0, sizeof(EFISystemTable));

    st->signature  = EFI_SYSTEM_TABLE_SIGNATURE;
    st->revision   = EFI_SYSTEM_TABLE_REVISION_2_10;
    st->header_size = sizeof(EFISystemTable);

    st->firmware_vendor    = (void *)"mini-uefi Firmware v1.0";
    st->firmware_revision  = 0x00010000;

    static EFISimpleTextInputProtocol  con_in_stub  = {0};
    static EFISimpleTextOutputProtocol con_out_stub = {0};
    st->con_in  = &con_in_stub;
    st->con_out = &con_out_stub;
    st->std_err = &con_out_stub;
}

EFIStatus uefi_install_protocol(EFIBootServices *bs, EFIHandle *handle_ptr,
                                EFIGuid *protocol, void *interface) {
    if (!bs || !handle_ptr || !protocol || !interface) return EFI_INVALID_PARAMETER;

    EFIHandle handle = *handle_ptr;
    if (!handle) handle = malloc(1); /* auto-create handle */
    *handle_ptr = handle;

    for (uint32_t i = 0; i < bs->protocol_count; i++) {
        EFIProtocolEntry *entry = &bs->protocol_db[i];
        if (entry->handle == handle &&
            memcmp(&entry->guid, protocol, sizeof(EFIGuid)) == 0) {
            printf("  Protocol already installed on handle, re-installing\n");
            entry->interface = interface;
            return EFI_SUCCESS;
        }
    }

    if (bs->protocol_count >= MAX_PROTOCOL_ENTRIES) {
        return EFI_OUT_OF_RESOURCES;
    }

    EFIProtocolEntry *entry = &bs->protocol_db[bs->protocol_count++];
    memcpy(&entry->guid, protocol, sizeof(EFIGuid));
    entry->interface = interface;
    entry->handle    = handle;
    entry->next      = NULL;

    printf("  Protocol installed on handle %p (count=%u)\n", (void *)handle, bs->protocol_count);
    return EFI_SUCCESS;
}

EFIStatus uefi_locate_protocol(EFIBootServices *bs, EFIGuid *protocol,
                                void *registration, void **interface) {
    (void)registration;
    if (!bs || !protocol || !interface) return EFI_INVALID_PARAMETER;

    for (uint32_t i = 0; i < bs->protocol_count; i++) {
        if (memcmp(&bs->protocol_db[i].guid, protocol, sizeof(EFIGuid)) == 0) {
            *interface = bs->protocol_db[i].interface;
            printf("  Protocol located at index %u, handle=%p\n", i,
                   (void *)bs->protocol_db[i].handle);
            return EFI_SUCCESS;
        }
    }

    *interface = NULL;
    printf("  Protocol not found in database\n");
    return EFI_NOT_FOUND;
}

EFIStatus uefi_exit_boot_services(EFIBootServices *bs, EFIHandle image_handle,
                                  uint64_t map_key) {
    (void)image_handle; (void)map_key;
    printf("  ExitBootServices() called – terminating boot services\n");
    printf("  Map key: 0x%016llX\n", (unsigned long long)map_key);

    bs->signature = 0xDEADBEEFDEADBEEFULL;

    printf("  Freeing firmware memory pools...\n");
    printf("  Closing boot service events...\n");
    printf("  Disabling watchdog timer...\n");
    printf("  Boot services exited. Control passed to OS loader.\n");
    return EFI_SUCCESS;
}

EFIStatus uefi_install_configuration_table(EFISystemTable *st, EFIGuid *guid, void *table) {
    if (!st || !guid || !table) return EFI_INVALID_PARAMETER;
    /* In a full implementation, this would append to the config table array.
       For the demo, we just note the call. */
    (void)guid; (void)table;
    printf("  Configuration table installed\n");
    return EFI_SUCCESS;
}

void uefi_collect_garbage_protocols(EFIBootServices *bs) {
    if (!bs) return;
    printf("  GC: protocol_count = %u, cleaning stale entries...\n", bs->protocol_count);
    uint32_t keep = 0;
    for (uint32_t i = 0; i < bs->protocol_count; i++) {
        if (bs->protocol_db[i].interface != NULL) {
            if (i != keep) {
                bs->protocol_db[keep] = bs->protocol_db[i];
            }
            keep++;
        }
    }
    bs->protocol_count = keep;
    printf("  GC: after cleanup protocol_count = %u\n", keep);
}

void uefi_print_system_table(const EFISystemTable *st) {
    if (!st) return;
    printf("\n=== EFI System Table ===\n");
    printf("  Signature:       0x%016llX\n", (unsigned long long)st->signature);
    printf("  Revision:        0x%08X\n", st->revision);
    printf("  Header Size:     %u bytes\n", st->header_size);
    printf("  Firmware Vendor: %s\n", (char *)st->firmware_vendor);
    printf("  Firmware Rev:    0x%08X\n", st->firmware_revision);
    printf("  ConIn Handle:    %p\n", (void *)st->console_in_handle);
    printf("  ConOut Handle:   %p\n", (void *)st->console_out_handle);
    printf("  StdErr Handle:   %p\n", (void *)st->standard_error_handle);
    printf("  Runtime Services: %p\n", (void *)st->runtime_services);
    printf("  Boot Services:    %p\n", (void *)st->boot_services);
    printf("  Config Table Entries: %llu\n", (unsigned long long)st->num_table_entries);
}

void uefi_print_guid(const EFIGuid *guid) {
    if (!guid) { printf("{NULL}"); return; }
    printf("{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
           guid->data1, guid->data2, guid->data3,
           guid->data4[0], guid->data4[1], guid->data4[2], guid->data4[3],
           guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
}

static void bs_init_function_table(EFIBootServices *bs) {
    bs->signature        = EFI_BOOT_SERVICES_SIGNATURE;
    bs->revision         = EFI_SYSTEM_TABLE_REVISION_2_10;
    bs->header_size      = sizeof(EFIBootServices);

    bs->raise_tpl        = stub_raise_tpl;
    bs->restore_tpl      = stub_restore_tpl;
    bs->allocate_pages   = stub_allocate_pages;
    bs->free_pages       = stub_free_pages;
    bs->get_memory_map   = stub_get_memory_map;
    bs->allocate_pool    = uefi_allocate_pool_stub;
    bs->free_pool        = uefi_free_pool_stub;

    bs->create_event     = stub_create_event;
    bs->set_timer        = stub_set_timer;
    bs->wait_for_event   = stub_wait_for_event;
    bs->signal_event     = stub_signal_event;
    bs->close_event      = stub_close_event;
    bs->check_event      = stub_check_event;

    bs->install_protocol_interface    = uefi_install_protocol;
    bs->reinstall_protocol_interface  = NULL;
    bs->uninstall_protocol_interface  = NULL;
    bs->handle_protocol               = stub_handle_protocol;

    bs->reserved                     = NULL;
    bs->register_protocol_notify     = stub_register_protocol_notify;
    bs->locate_handle                = NULL;
    bs->locate_device_path           = stub_locate_device_path;
    bs->install_configuration_table  = NULL;

    bs->load_image         = stub_load_image;
    bs->start_image        = stub_start_image;
    bs->exit_boot          = NULL;
    bs->unload_image       = NULL;
    bs->exit_boot_services = uefi_exit_boot_services;

    bs->get_next_monotonic_count = stub_get_monotonic_count;
    bs->stall              = stub_stall;
    bs->set_watchdog_timer = stub_set_watchdog;

    bs->connect_controller    = stub_connect_controller;
    bs->disconnect_controller = stub_disconnect_controller;

    bs->protocol_db    = calloc(MAX_PROTOCOL_ENTRIES, sizeof(EFIProtocolEntry));
    bs->protocol_count = 0;

    bs->open_protocol              = stub_open_protocol;
    bs->close_protocol             = stub_close_protocol;
    bs->open_protocol_information  = stub_open_protocol_info;

    bs->protocols_per_handle       = stub_protocols_per_handle;
    bs->locate_handle_buffer       = stub_locate_handle_buffer;
    bs->locate_protocol            = uefi_locate_protocol;
    bs->install_multiple_protocol_interfaces   = NULL;
    bs->uninstall_multiple_protocol_interfaces = NULL;

    bs->calculate_crc32 = stub_calculate_crc32;

    bs->copy_mem = stub_copy_mem;
    bs->set_mem  = stub_set_mem;
}

void uefi_boot_services_init(EFIBootServices *bs) {
    bs_init_function_table(bs);
}
