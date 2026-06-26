#ifndef UEFI_PROTOCOLS_H
#define UEFI_PROTOCOLS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "uefi_boot.h"

/* ===== GUIDs for core protocols ===== */
#define EFI_LOADED_IMAGE_PROTOCOL_GUID  \
    {0x5B1B31A1, 0x9562, 0x11D2, {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}}
#define EFI_DEVICE_PATH_PROTOCOL_GUID   \
    {0x09576E91, 0x6D3F, 0x11D2, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}}
#define EFI_BLOCK_IO_PROTOCOL_GUID      \
    {0x964E5B21, 0x6459, 0x11D2, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}}
#define EFI_SIMPLE_FILE_SYSTEM_GUID     \
    {0x0964E5B2, 0x6459, 0x11D2, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}}
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    {0x9042A9DE, 0x23DC, 0x4A38, {0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A}}
#define EFI_FILE_INFO_GUID              \
    {0x09576E92, 0x6D3F, 0x11D2, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}}

/* ===== Device Path Types ===== */
#define DEVICE_PATH_TYPE_HARDWARE   0x01
#define DEVICE_PATH_TYPE_ACPI       0x02
#define DEVICE_PATH_TYPE_MESSAGING  0x03
#define DEVICE_PATH_TYPE_MEDIA      0x04
#define DEVICE_PATH_TYPE_BIOS_BOOT  0x05
#define DEVICE_PATH_TYPE_END        0x7F

/* Hardware sub-types */
#define HW_PCI_DP            0x01
#define HW_VENDOR_DP         0x04

/* ACPI sub-types */
#define ACPI_DEVICE_PATH     0x01
#define ACPI_ADR_DP          0x03

/* Messaging sub-types */
#define MSG_ATAPI_DP         0x01
#define MSG_USB_DP           0x05
#define MSG_SATA_DP          0x12

/* Media sub-types */
#define MEDIA_HARDDRIVE_DP   0x01
#define MEDIA_CDROM_DP       0x02
#define MEDIA_FILEPATH_DP    0x04
#define MEDIA_END_DP         0xFF
#define END_DEVICE_PATH_LENGTH  0x04

/* ===== Loaded Image Protocol ===== */
typedef struct {
    uint32_t    revision;
    EFIHandle   parent_handle;
    EFISystemTable *system_table;
    EFIHandle   device_handle;
    void        *file_path;
    void        *reserved;
    uint32_t    load_options_size;
    void        *load_options;
    void        *image_base;
    uint64_t    image_size;
    EFIMemoryType image_code_type;
    EFIMemoryType image_data_type;
    void        (*unload)(EFIHandle image_handle);
} EFILoadedImageProtocol;

/* ===== Device Path Protocol ===== */
typedef struct {
    uint8_t  type;
    uint8_t  sub_type;
    uint8_t  length[2];
} EFIDevicePathProtocol;

typedef struct {
    EFIDevicePathProtocol header;
    uint32_t function;
    uint8_t  device;
} PCI_DEVICE_PATH;

typedef struct {
    EFIDevicePathProtocol header;
    uint32_t hid;
    uint32_t uid;
} ACPI_HID_DEVICE_PATH;

typedef struct {
    EFIDevicePathProtocol header;
    uint32_t partition_number;
    uint64_t partition_start;
    uint64_t partition_size;
    uint8_t  signature[16];
    uint8_t  mbr_type;
    uint8_t  signature_type;
} HARDDRIVE_DEVICE_PATH;

typedef struct {
    EFIDevicePathProtocol header;
    uint16_t characters[];
} FILEPATH_DEVICE_PATH;

/* ===== Block I/O Protocol ===== */
#define EFI_BLOCK_IO_PROTOCOL_REVISION 0x00010000
#define EFI_BLOCK_IO_PROTOCOL_REVISION2 0x00020001
#define EFI_BLOCK_IO_PROTOCOL_REVISION3 0x0002001F

typedef struct {
    uint32_t media_id;
    bool     removable_media;
    bool     media_present;
    bool     logical_partition;
    bool     read_only;
    bool     write_caching;
    uint32_t block_size;
    uint32_t io_align;
    uint8_t  pad[4];
    uint64_t last_block;
    uint64_t lowest_aligned_lba;
    uint32_t logical_blocks_per_physical_block;
    uint32_t optimal_transfer_length_granularity;
} EFIBlockIoMedia;

typedef struct {
    uint64_t revision;
    EFIBlockIoMedia *media;
    EFIStatus (*reset)(void *self, bool extended);
    EFIStatus (*read_blocks)(void *self, uint32_t media_id, uint64_t lba,
                             uint64_t buf_size, void *buf);
    EFIStatus (*write_blocks)(void *self, uint32_t media_id, uint64_t lba,
                              uint64_t buf_size, void *buf);
    EFIStatus (*flush_blocks)(void *self);
} EFIBlockIoProtocol;

/* ===== Simple File System Protocol ===== */
#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_REVISION 0x00010000

typedef struct {
    uint64_t size;
    uint64_t file_size;
    uint64_t physical_size;
    void     *create_time;
    void     *last_access_time;
    void     *modification_time;
    uint64_t attribute;
    uint16_t file_name[];
} EFIFileInfo;

typedef struct EFIFileProtocol {
    uint64_t revision;
    EFIStatus (*open_fn)(struct EFIFileProtocol *self, struct EFIFileProtocol **new_handle,
                         uint16_t *file_name, uint64_t open_mode, uint64_t attributes);
    EFIStatus (*close)(struct EFIFileProtocol *self);
    EFIStatus (*delete)(struct EFIFileProtocol *self);
    EFIStatus (*read)(struct EFIFileProtocol *self, uint64_t *buf_size, void *buf);
    EFIStatus (*write)(struct EFIFileProtocol *self, uint64_t *buf_size, void *buf);
    EFIStatus (*get_position)(struct EFIFileProtocol *self, uint64_t *position);
    EFIStatus (*set_position)(struct EFIFileProtocol *self, uint64_t position);
    EFIStatus (*get_info)(struct EFIFileProtocol *self, EFIGuid *info_type,
                          uint64_t *buf_size, void *buf);
    EFIStatus (*set_info)(struct EFIFileProtocol *self, EFIGuid *info_type,
                          uint64_t buf_size, void *buf);
    EFIStatus (*flush)(struct EFIFileProtocol *self);
} EFIFileProtocol;

typedef struct {
    uint64_t revision;
    EFIStatus (*open_volume)(void *self, EFIFileProtocol **root);
} EFISimpleFileSystemProtocol;

/* ===== Graphics Output Protocol ===== */
typedef struct {
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t reserved_mask;
} EFIPixelBitmask;

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFIGraphicsPixelFormat;

typedef struct {
    uint32_t version;
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    EFIGraphicsPixelFormat pixel_format;
    EFIPixelBitmask pixel_information;
    uint32_t pixels_per_scan_line;
} EFIGraphicsOutputModeInfo;

typedef struct {
    uint32_t max_mode;
    uint32_t mode;
    EFIGraphicsOutputModeInfo *info;
    uint64_t size_of_info;
    EFIPhysicalAddress frame_buffer_base;
    uint64_t frame_buffer_size;
} EFIGraphicsOutputProtocolMode;

typedef struct {
    EFIStatus (*query_mode)(void *self, uint32_t mode_number, uint64_t *size_of_info,
                            EFIGraphicsOutputModeInfo **info);
    EFIStatus (*set_mode)(void *self, uint32_t mode_number);
    void *blt;
    EFIGraphicsOutputProtocolMode *mode;
} EFIGraphicsOutputProtocol;

/* ===== Function declarations ===== */
EFIDevicePathProtocol* uefi_init_device_path(uint8_t type, uint8_t sub_type);
EFIDevicePathProtocol* uefi_append_device_path(EFIDevicePathProtocol *src,
                                                EFIDevicePathProtocol *node);
uint16_t               uefi_device_path_length(const EFIDevicePathProtocol *dp);
EFIStatus              uefi_block_io_read(void *bio_ptr, uint32_t media_id, uint64_t lba,
                                          uint64_t count, void *buffer);
EFIStatus              uefi_block_io_write(void *bio_ptr, uint32_t media_id, uint64_t lba,
                                           uint64_t count, void *buffer);
EFIStatus              uefi_gop_set_mode(EFIGraphicsOutputProtocol *gop, uint32_t mode);
EFIStatus              uefi_file_open(EFIFileProtocol *root, EFIFileProtocol **file,
                                      uint16_t *name);
void                   uefi_print_device_path(const EFIDevicePathProtocol *dp);
void                   uefi_print_gop_modes(const EFIGraphicsOutputProtocol *gop);

#endif /* UEFI_PROTOCOLS_H */
