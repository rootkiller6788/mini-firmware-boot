#include "uefi_protocols.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

EFIDevicePathProtocol *uefi_init_device_path(uint8_t type, uint8_t sub_type) {
    uint8_t *raw = calloc(1, sizeof(EFIDevicePathProtocol) + END_DEVICE_PATH_LENGTH);
    if (!raw) return NULL;
    EFIDevicePathProtocol *dp = (EFIDevicePathProtocol *)raw;
    dp->type     = type;
    dp->sub_type = sub_type;
    dp->length[0] = (uint8_t)sizeof(EFIDevicePathProtocol);
    dp->length[1] = 0;
    /* Place end marker after the node */
    EFIDevicePathProtocol *end = (EFIDevicePathProtocol *)(raw + sizeof(EFIDevicePathProtocol));
    end->type     = DEVICE_PATH_TYPE_END;
    end->sub_type = MEDIA_END_DP;
    end->length[0] = END_DEVICE_PATH_LENGTH;
    end->length[1] = 0;
    return dp;
}

EFIDevicePathProtocol *uefi_append_device_path(EFIDevicePathProtocol *src,
                                                EFIDevicePathProtocol *node) {
    if (!node) return src;

    if (!src) {
        EFIDevicePathProtocol *result = malloc(sizeof(EFIDevicePathProtocol) + 256);
        if (!result) return NULL;
        memcpy(result, node, sizeof(EFIDevicePathProtocol));
        return result;
    }

    uint16_t src_len = uefi_device_path_length(src);
    uint16_t node_len = (uint16_t)(node->length[0] | (node->length[1] << 8));
    size_t total = (size_t)src_len + (size_t)node_len + 64;

    EFIDevicePathProtocol *new_dp = realloc(src, total);
    if (!new_dp) {
        free(src);
        return NULL;
    }
    memcpy((uint8_t *)new_dp + src_len, node, (size_t)node_len);

    return new_dp;
}

uint16_t uefi_device_path_length(const EFIDevicePathProtocol *dp) {
    if (!dp) return 0;
    uint16_t total = 0;
    while (dp->type != DEVICE_PATH_TYPE_END || dp->sub_type != MEDIA_END_DP) {
        uint16_t len = (uint16_t)(dp->length[0] | (dp->length[1] << 8));
        total += len;
        dp = (const EFIDevicePathProtocol *)((const uint8_t *)dp + len);
    }
    total += END_DEVICE_PATH_LENGTH;
    return total;
}

EFIStatus uefi_block_io_read(void *bio_ptr, uint32_t media_id, uint64_t lba,
                              uint64_t count, void *buffer) {
    EFIBlockIoProtocol *bio = (EFIBlockIoProtocol *)bio_ptr;
    (void)media_id;
    if (!bio || !bio->media || !buffer) return EFI_INVALID_PARAMETER;
    if (!bio->media->media_present) return EFI_NO_MAPPING;

    if (lba + count > bio->media->last_block + 1) {
        return EFI_BAD_BUFFER_SIZE;
    }

    printf("  BlockIo->ReadBlocks(LBA=%llu, Count=%llu, BlockSize=%u)\n",
           (unsigned long long)lba, (unsigned long long)count,
           bio->media->block_size);

    memset(buffer, 0, (size_t)(count * bio->media->block_size));

    return EFI_SUCCESS;
}

EFIStatus uefi_block_io_write(void *bio_ptr, uint32_t media_id, uint64_t lba,
                               uint64_t count, void *buffer) {
    EFIBlockIoProtocol *bio = (EFIBlockIoProtocol *)bio_ptr;
    (void)media_id;
    if (!bio || !bio->media || !buffer) return EFI_INVALID_PARAMETER;
    if (!bio->media->media_present) return EFI_NO_MAPPING;
    if (bio->media->read_only) return EFI_ACCESS_DENIED;

    if (lba + count > bio->media->last_block + 1) {
        return EFI_BAD_BUFFER_SIZE;
    }

    printf("  BlockIo->WriteBlocks(LBA=%llu, Count=%llu, BlockSize=%u)\n",
           (unsigned long long)lba, (unsigned long long)count,
           bio->media->block_size);

    return EFI_SUCCESS;
}

EFIStatus uefi_gop_set_mode(EFIGraphicsOutputProtocol *gop, uint32_t mode) {
    if (!gop || !gop->mode) return EFI_INVALID_PARAMETER;

    if (mode > gop->mode->max_mode) {
        printf("  GOP: Mode %u exceeds max mode %u\n", mode, gop->mode->max_mode);
        return EFI_UNSUPPORTED;
    }

    gop->mode->mode = mode;

    EFIStatus result = EFI_SUCCESS;
    if (gop->set_mode) {
        result = gop->set_mode(gop, mode);
    }

    EFIGraphicsOutputModeInfo *info = gop->mode->info;
    if (info) {
        printf("  GOP: Set mode %u — %ux%u, format=%u, scanline=%u\n",
               mode, info->horizontal_resolution, info->vertical_resolution,
               (unsigned)info->pixel_format, info->pixels_per_scan_line);
    }

    return result;
}

EFIStatus uefi_file_open(EFIFileProtocol *root, EFIFileProtocol **file,
                          uint16_t *name) {
    if (!root || !file || !name) return EFI_INVALID_PARAMETER;

    EFIStatus status = root->open_fn(root, file, name, 1, 0);
    if (status == EFI_SUCCESS) {
        printf("  File opened: %S\n", name);
    } else {
        printf("  File open failed: 0x%llX\n", (unsigned long long)status);
    }

    return status;
}

void uefi_print_device_path(const EFIDevicePathProtocol *dp) {
    if (!dp) { printf("  DevicePath: NULL\n"); return; }

    printf("  Device Path:\n");
    while (dp->type != DEVICE_PATH_TYPE_END || dp->sub_type != MEDIA_END_DP) {
        uint16_t len = (uint16_t)(dp->length[0] | (dp->length[1] << 8));
        printf("    Type=0x%02X SubType=0x%02X Len=%u — ",
               dp->type, dp->sub_type, len);

        switch (dp->type) {
        case DEVICE_PATH_TYPE_HARDWARE:
            printf("Hardware Device Path");
            if (dp->sub_type == HW_PCI_DP) {
                const PCI_DEVICE_PATH *pci = (const PCI_DEVICE_PATH *)dp;
                printf(" (PCI: func=%u dev=%u)", pci->function, pci->device);
            }
            break;
        case DEVICE_PATH_TYPE_ACPI:
            printf("ACPI Device Path");
            break;
        case DEVICE_PATH_TYPE_MESSAGING:
            printf("Messaging Device Path");
            break;
        case DEVICE_PATH_TYPE_MEDIA:
            printf("Media Device Path");
            if (dp->sub_type == MEDIA_HARDDRIVE_DP) {
                const HARDDRIVE_DEVICE_PATH *hd = (const HARDDRIVE_DEVICE_PATH *)dp;
                printf(" (Part=%u Start=%llu Size=%llu)",
                       hd->partition_number,
                       (unsigned long long)hd->partition_start,
                       (unsigned long long)hd->partition_size);
            } else if (dp->sub_type == MEDIA_FILEPATH_DP) {
                printf(" (FilePath)");
            }
            break;
        default:
            printf("Unknown Type");
            break;
        }
        printf("\n");

        dp = (const EFIDevicePathProtocol *)((const uint8_t *)dp + len);
    }
    printf("    Type=0x%02X SubType=0x%02X — End of Device Path\n",
           dp->type, dp->sub_type);
}

void uefi_print_gop_modes(const EFIGraphicsOutputProtocol *gop) {
    if (!gop || !gop->mode) {
        printf("  GOP not available\n");
        return;
    }

    printf("  GOP Max Modes: %u, Current Mode: %u\n",
           gop->mode->max_mode, gop->mode->mode);

    if (gop->mode->info) {
        const EFIGraphicsOutputModeInfo *info = gop->mode->info;
        printf("  Current Resolution: %ux%u\n",
               info->horizontal_resolution, info->vertical_resolution);
        printf("  Pixel Format: %u\n", (unsigned)info->pixel_format);
        printf("  Frame Buffer: 0x%llX (%llu bytes)\n",
               (unsigned long long)gop->mode->frame_buffer_base,
               (unsigned long long)gop->mode->frame_buffer_size);
    }

    printf("  Available resolutions:\n");
    for (uint32_t m = 0; m <= gop->mode->max_mode && m < 16; m++) {
        uint64_t size = 0;
        EFIGraphicsOutputModeInfo *info = NULL;
        if (gop->query_mode && gop->query_mode((void *)gop, m, &size, &info) == EFI_SUCCESS) {
            printf("    Mode %u: %ux%u (fmt=%u)\n",
                   m, info->horizontal_resolution, info->vertical_resolution,
                   (unsigned)info->pixel_format);
        }
    }
}
