#include "uefi_boot.h"
#include "uefi_protocols.h"
#include "pe_coff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declaration from uefi_boot.c */
extern void uefi_boot_services_init(EFIBootServices *bs);

static EFIGuid g_loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFIGuid g_block_io_guid     = EFI_BLOCK_IO_PROTOCOL_GUID;
static EFIGuid g_gop_guid          = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFIGuid g_file_system_guid  = EFI_SIMPLE_FILE_SYSTEM_GUID;

static EFIStatus dummy_gop_query_mode(void *self, uint32_t mode_number,
                                       uint64_t *size_of_info,
                                       EFIGraphicsOutputModeInfo **info) {
    (void)self; (void)size_of_info;
    static EFIGraphicsOutputModeInfo modes[3] = {
        {0, 1024, 768,  PixelBlueGreenRedReserved8BitPerColor, {0, 0, 0, 0}, 1024},
        {0, 1920, 1080, PixelBlueGreenRedReserved8BitPerColor, {0, 0, 0, 0}, 1920},
        {0, 2560, 1440, PixelBlueGreenRedReserved8BitPerColor, {0, 0, 0, 0}, 2560},
    };
    if (mode_number > 2) return EFI_INVALID_PARAMETER;
    *info = &modes[mode_number];
    *size_of_info = sizeof(EFIGraphicsOutputModeInfo);
    return EFI_SUCCESS;
}

static EFIStatus dummy_gop_set_mode(void *self, uint32_t mode_number) {
    (void)self;
    printf("  GOP->SetMode(%u)\n", mode_number);
    return EFI_SUCCESS;
}

int main(void) {
    printf("========================================\n");
    printf("  mini-uefi-boot: UEFI Boot Simulation\n");
    printf("========================================\n\n");

    /* --- Initialize System Table --- */
    printf("--- Step 1: Initialize EFI System Table ---\n");
    EFISystemTable st;
    uefi_init_system_table(&st);

    /* Initialize Runtime Services */
    EFIRuntimeServices *rt = calloc(1, sizeof(EFIRuntimeServices));
    rt->signature    = EFI_RUNTIME_SERVICES_SIGNATURE;
    rt->revision     = EFI_SYSTEM_TABLE_REVISION_2_10;
    rt->header_size  = sizeof(EFIRuntimeServices);
    st.runtime_services = rt;

    /* Initialize Boot Services */
    EFIBootServices *bs = calloc(1, sizeof(EFIBootServices));
    uefi_boot_services_init(bs);
    st.boot_services = bs;

    uefi_print_system_table(&st);

    /* --- Install Protocols (DXE phase simulation) --- */
    printf("\n--- Step 2: DXE Phase — Protocol Installation ---\n");

    EFIHandle image_handle = (EFIHandle)(uintptr_t)0x1000;
    EFIHandle disk_handle  = (EFIHandle)(uintptr_t)0x2000;
    EFIHandle gop_handle   = (EFIHandle)(uintptr_t)0x3000;

    /* Install Loaded Image Protocol */
    EFILoadedImageProtocol *lip = calloc(1, sizeof(EFILoadedImageProtocol));
    lip->revision     = 0x1000;
    lip->parent_handle = NULL;
    lip->system_table  = &st;
    lip->image_base    = (void *)0x100000;
    lip->image_size    = 0x200000;
    uefi_install_protocol(bs, &image_handle, &g_loaded_image_guid, lip);

    /* Install Block I/O Protocol */
    EFIBlockIoProtocol *bio = calloc(1, sizeof(EFIBlockIoProtocol));
    bio->revision = EFI_BLOCK_IO_PROTOCOL_REVISION3;
    bio->media    = calloc(1, sizeof(EFIBlockIoMedia));
    bio->media->block_size    = 512;
    bio->media->last_block    = 2097151;
    bio->media->media_present = true;
    bio->media->removable_media = false;
    bio->media->read_only       = false;
    bio->read_blocks  = uefi_block_io_read;
    bio->write_blocks = uefi_block_io_write;
    uefi_install_protocol(bs, &disk_handle, &g_block_io_guid, bio);

    /* Install Graphics Output Protocol */
    EFIGraphicsOutputProtocol *gop = calloc(1, sizeof(EFIGraphicsOutputProtocol));
    gop->query_mode = dummy_gop_query_mode;
    gop->set_mode   = dummy_gop_set_mode;
    gop->mode       = calloc(1, sizeof(EFIGraphicsOutputProtocolMode));
    gop->mode->max_mode = 2;
    gop->mode->mode     = 0;
    gop->mode->info     = calloc(1, sizeof(EFIGraphicsOutputModeInfo));
    gop->mode->info->horizontal_resolution = 1024;
    gop->mode->info->vertical_resolution   = 768;
    gop->mode->info->pixel_format = PixelBlueGreenRedReserved8BitPerColor;
    uefi_install_protocol(bs, &gop_handle, &g_gop_guid, gop);

    /* --- Protocol Database Lookups --- */
    printf("\n--- Step 3: Protocol Lookup ---\n");

    void *located_lip = NULL;
    EFIStatus status = uefi_locate_protocol(bs, &g_loaded_image_guid, NULL, &located_lip);
    printf("  LocateProtocol(LoadedImage) -> %s (interface=%p)\n",
           status == EFI_SUCCESS ? "SUCCESS" : "NOT_FOUND", located_lip);

    void *located_bio = NULL;
    status = uefi_locate_protocol(bs, &g_block_io_guid, NULL, &located_bio);
    printf("  LocateProtocol(BlockIo) -> %s (interface=%p)\n",
           status == EFI_SUCCESS ? "SUCCESS" : "NOT_FOUND", located_bio);

    void *located_gop = NULL;
    status = uefi_locate_protocol(bs, &g_gop_guid, NULL, &located_gop);
    printf("  LocateProtocol(GOP) -> %s (interface=%p)\n",
           status == EFI_SUCCESS ? "SUCCESS" : "NOT_FOUND", located_gop);

    /* Try locating an unregistered protocol */
    void *not_found = NULL;
    status = uefi_locate_protocol(bs, &g_file_system_guid, NULL, &not_found);
    printf("  LocateProtocol(SimpleFileSystem) -> %s\n",
           status == EFI_SUCCESS ? "SUCCESS" : "NOT_FOUND");

    /* --- Block I/O Test --- */
    printf("\n--- Step 4: Block I/O Read ---\n");
    EFIBlockIoProtocol *bio_ptr = (EFIBlockIoProtocol *)located_bio;
    if (bio_ptr && bio_ptr->media) {
        printf("  Disk geometry: %llu blocks x %u bytes = %llu MB\n",
               (unsigned long long)(bio_ptr->media->last_block + 1),
               bio_ptr->media->block_size,
               (unsigned long long)((bio_ptr->media->last_block + 1) * bio_ptr->media->block_size
                                    / (1024 * 1024)));
        uint8_t sector_buf[512];
        uefi_block_io_read(bio_ptr, 0, 0, 1, sector_buf);
    }

    /* --- GOP Mode Switch --- */
    printf("\n--- Step 5: Graphics Output Protocol ---\n");
    EFIGraphicsOutputProtocol *gop_ptr = (EFIGraphicsOutputProtocol *)located_gop;
    if (gop_ptr) {
        uefi_print_gop_modes(gop_ptr);
        uefi_gop_set_mode(gop_ptr, 1);
        uefi_gop_set_mode(gop_ptr, 0);
    }

    /* --- Device Path --- */
    printf("\n--- Step 6: Device Path Construction ---\n");
    EFIDevicePathProtocol *dp = uefi_init_device_path(DEVICE_PATH_TYPE_MEDIA,
                                                       MEDIA_FILEPATH_DP);
    uefi_print_device_path(dp);
    free(dp);

    /* --- Load PE/COFF Image --- */
    printf("\n--- Step 7: PE/COFF Image Loading ---\n");
    printf("  Note: In a real UEFI system, the boot manager finds bootx64.efi\n");
    printf("  via device path, loads it into memory, and calls StartImage().\n");

    /* Manually simulate loading */
    printf("  LoadImage(bootx64.efi) -> allocating EfiLoaderCode pages...\n");
    void *image_base = malloc(0x200000);
    printf("  Image loaded at 0x%p\n", image_base);

    /* Simulate a dummy PE header in the loaded image */
    printf("  Validating PE/COFF header (simulated)...\n");
    printf("  Machine: x64, Subsystem: EFI_BOOT_SERVICE\n");
    printf("  Sections: .text, .data, .reloc\n");

    printf("  StartImage(ImageHandle) -> calling entry point...\n");
    bs->start_image(image_handle, NULL, NULL);

    /* --- Exit Boot Services --- */
    printf("\n--- Step 8: ExitBootServices() ---\n");
    uint64_t map_key = 1;
    status = uefi_exit_boot_services(bs, image_handle, map_key);
    printf("  ExitBootServices -> %s\n", status == EFI_SUCCESS ? "SUCCESS" : "FAILED");

    /* --- Runtime Phase --- */
    printf("\n--- Step 9: Runtime Phase ---\n");
    printf("  Boot services are now unavailable.\n");
    printf("  Only runtime services remain active.\n");
    printf("  OS kernel takes control of the system.\n");

    uefi_collect_garbage_protocols(bs);

    /* Cleanup */
    printf("\n=== mini-uefi-boot: Demo Complete ===\n");
    free(gop->mode->info);
    free(gop->mode);
    free(gop);
    free(bio->media);
    free(bio);
    free(lip);
    free(bs->protocol_db);
    free(bs);
    free(rt);
    free(image_base);
    return 0;
}
