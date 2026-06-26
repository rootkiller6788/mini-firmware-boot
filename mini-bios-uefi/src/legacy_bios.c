#include "legacy_bios.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void default_int_handler(void) {
    /* Stub: real BIOS would execute an IRET */
}

void bios_init_ivt(IVT *ivt) {
    memset(ivt, 0, sizeof(IVT));
    for (int i = 0; i < IVT_SIZE; i++) {
        ivt->vectors[i].segment = 0xF000;
        ivt->vectors[i].offset  = (uint16_t)((uintptr_t)default_int_handler & 0xFFFF);
    }
}

void bios_set_interrupt(IVT *ivt, int int_num, uint16_t segment, uint16_t offset) {
    if (int_num < 0 || int_num >= IVT_SIZE) return;
    ivt->vectors[int_num].segment = segment;
    ivt->vectors[int_num].offset  = offset;
}

IVTEntry bios_get_interrupt(const IVT *ivt, int int_num) {
    IVTEntry empty = {0, 0};
    if (int_num < 0 || int_num >= IVT_SIZE) return empty;
    return ivt->vectors[int_num];
}

void bios_int10h_video(uint8_t ah, uint8_t al, uint8_t bh, uint8_t bl, uint16_t cx, uint16_t dx) {
    switch (ah) {
    case VIDEO_SET_MODE:
        printf("  INT 0x10: Set video mode 0x%02X\n", al);
        break;
    case VIDEO_SET_CURSOR:
        printf("  INT 0x10: Set cursor shape (CH=0x%02X, CL=0x%02X)\n", (cx >> 8) & 0xFF, cx & 0xFF);
        break;
    case VIDEO_SET_POS:
        printf("  INT 0x10: Set cursor pos page=%u row=%02X col=%02X\n", bh, dx, (cx & 0xFF));
        break;
    case VIDEO_SCROLL_UP:
        printf("  INT 0x10: Scroll up lines=%u\n", al);
        break;
    case VIDEO_WRITE_CHAR:
        printf("  INT 0x10: Write char '%c' attrib=0x%02X count=%u\n", al, bl, cx);
        break;
    case VIDEO_WRITE_TELETYPE:
        for (uint16_t i = 0; i < cx; i++) {
            printf("%c", al);
        }
        break;
    case VIDEO_GET_MODE:
        printf("  INT 0x10: Get video mode\n");
        break;
    default:
        printf("  INT 0x10: Unknown sub-function AH=0x%02X\n", ah);
        break;
    }
}

int bios_int13h_disk(uint8_t ah, uint8_t dl, uint16_t cx, uint16_t dh,
                     uint16_t es, uint16_t bx, uint16_t num_sectors) {
    uint8_t drive = dl & 0x7F;
    uint8_t cylinder = ((cx >> 8) & 0xC0) | ((cx >> 8) & 0x3F);
    uint8_t sector   = cx & 0x3F;
    uint8_t head     = dh;
    (void)es; (void)bx; (void)drive;

    switch (ah) {
    case DISK_RESET:
        printf("  INT 0x13: Reset disk drive=0x%02X\n", dl);
        printf("  INT 0x13: Disk reset successful, CF=0\n");
        return 0;
    case DISK_READ:
        printf("  INT 0x13: Read sectors drive=0x%02X C/H/S=%u/%u/%u count=%u buf=%04X:%04X\n",
               dl, cylinder, head, sector, num_sectors, es, bx);
        printf("  INT 0x13: Read %u sectors OK, CF=0\n", num_sectors);
        return 0;
    case DISK_WRITE:
        printf("  INT 0x13: Write sectors drive=0x%02X C/H/S=%u/%u/%u count=%u buf=%04X:%04X\n",
               dl, cylinder, head, sector, num_sectors, es, bx);
        printf("  INT 0x13: Write %u sectors OK, CF=0\n", num_sectors);
        return 0;
    case DISK_VERIFY:
        printf("  INT 0x13: Verify sectors drive=0x%02X count=%u\n", dl, num_sectors);
        return 0;
    case DISK_FORMAT:
        printf("  INT 0x13: Format track drive=0x%02X head=%u\n", dl, head);
        return 0;
    default:
        printf("  INT 0x13: Unknown sub-function AH=0x%02X, setting CF=1\n", ah);
        return 1;
    }
}

int bios_int19h_bootstrap(IVT *ivt) {
    printf("  INT 0x19: Bootstrap loader invoked\n");
    IVTEntry disk_vec = bios_get_interrupt(ivt, INT_DISK_SERVICES);
    printf("  INT 0x19: INT 0x13 vector = %04X:%04X\n", disk_vec.segment, disk_vec.offset);
    printf("  INT 0x19: Attempting to load MBR (CHS 0/0/1) into 0000:7C00\n");
    printf("  INT 0x19: Calling INT 0x13 AH=02 to read MBR\n");
    bios_int13h_disk(DISK_READ, 0x80, 0x0001, 0x00, 0x0000, 0x7C00, 1);
    printf("  INT 0x19: Checking MBR signature at 0x7DFE...\n");
    printf("  INT 0x19: Signature 0xAA55 found, jumping to 0000:7C00\n");
    printf("  INT 0x19: Bootstrap complete\n");
    return 0;
}

int bios_post(IVT *ivt, BIOSDataArea *bda) {
    printf("\n=== BIOS Power-On Self Test ===\n");

    printf("  POST %02X: CPU test - checking registers, flags, basic instructions...\n", POST_CPU_TEST);
    volatile uint32_t test_val = 0;
    __asm__ volatile("" : : "r"(test_val));
    printf("  POST %02X: CPU test PASSED\n", POST_CPU_TEST);

    printf("  POST %02X: CMOS checksum verification...\n", POST_CMOS_CHECKSUM);
    printf("  POST %02X: CMOS checksum OK\n", POST_CMOS_CHECKSUM);

    printf("  POST %02X: DMA controller initialization...\n", POST_DMA_INIT);
    printf("  POST %02X: DMA channels 0-7 initialized\n", POST_DMA_INIT);

    printf("  POST %02X: RAM refresh test...\n", POST_RAM_REFRESH);
    printf("  POST %02X: RAM refresh OK (15us interval)\n", POST_RAM_REFRESH);

    printf("  POST %02X: Keyboard controller init...\n", POST_KEYBOARD_INIT);
    printf("  POST %02X: Keyboard BAT (Basic Assurance Test) complete\n", POST_KEYBOARD_INIT);

    printf("  POST %02X: Video adapter init...\n", POST_VIDEO_INIT);
    bios_int10h_video(VIDEO_SET_MODE, 0x03, 0, 0, 0, 0);
    printf("  POST %02X: Video adapter OK (VGA text mode 0x03)\n", POST_VIDEO_INIT);

    printf("  POST %02X: Testing base memory (first 640KB)...\n", POST_RAM_TEST_BASE);
    uint16_t base_mem = BIOS_BASE_MEMORY_KB;
    printf("  POST %02X: Base memory test PASSED (%u KB)\n", POST_RAM_TEST_BASE, base_mem);

    printf("  POST %02X: Testing extended memory...\n", POST_RAM_TEST_EXTENDED);
    uint16_t ext_mem = 65536 - BIOS_BASE_MEMORY_KB;
    printf("  POST %02X: Extended memory test PASSED (%u KB)\n", POST_RAM_TEST_EXTENDED, ext_mem);

    printf("  POST %02X: Initializing interrupt vectors...\n", POST_INT_VECTORS);
    bios_init_ivt(ivt);
    bios_set_interrupt(ivt, INT_VIDEO_SERVICES, 0xF000, (uint16_t)((uintptr_t)bios_int10h_video & 0xFFFF));
    bios_set_interrupt(ivt, INT_DISK_SERVICES, 0xF000, (uint16_t)((uintptr_t)bios_int13h_disk & 0xFFFF));
    bios_set_interrupt(ivt, INT_BOOTSTRAP, 0xF000, (uint16_t)((uintptr_t)bios_int19h_bootstrap & 0xFFFF));
    printf("  POST %02X: IVT initialized (256 vectors)\n", POST_INT_VECTORS);

    if (bda) {
        memset(bda, 0, sizeof(BIOSDataArea));
        bda->com_ports[0] = 0x03F8;
        bda->lpt_ports[0] = 0x0378;
        bda->equipment_list = EQUIP_FPU_PRESENT | EQUIP_VGA_PRESENT;
        bda->memory_size_kb = BIOS_BASE_MEMORY_KB;
        bda->video_mode = 0x03;
        bda->video_columns = 80;
        bda->active_display_page = 0;
    }

    printf("  POST %02X: POST complete, booting OS...\n", POST_COMPLETE);
    printf("=== POST finished successfully ===\n");
    return 0;
}

void bios_print_bda(const BIOSDataArea *bda) {
    if (!bda) { printf("BDA is NULL\n"); return; }
    printf("\n=== BIOS Data Area (BDA) at 0000:%04X ===\n", BDA_SEGMENT);
    printf("  COM Ports:     %04X %04X %04X %04X\n",
           bda->com_ports[0], bda->com_ports[1], bda->com_ports[2], bda->com_ports[3]);
    printf("  LPT Ports:     %04X %04X %04X %04X\n",
           bda->lpt_ports[0], bda->lpt_ports[1], bda->lpt_ports[2], bda->lpt_ports[3]);
    printf("  Equipment list: 0x%04X", bda->equipment_list);
    if (bda->equipment_list & EQUIP_FPU_PRESENT)  printf(" [FPU]");
    if (bda->equipment_list & EQUIP_VGA_PRESENT)  printf(" [VGA]");
    printf("\n");
    printf("  Base Memory:   %u KB\n", bda->memory_size_kb);
    printf("  Video Mode:    0x%02X\n", bda->video_mode);
    printf("  Video Columns: %u\n", bda->video_columns);
    printf("  Keyboard flags: shift=0x%02X shift2=0x%02X\n",
           bda->keyboard_shift_flags, bda->keyboard_shift_flags2);
    printf("  CRT Controller port: 0x%04X\n", bda->crtc_port);
}

void bios_print_post_result(const POSTResult *result) {
    if (!result) return;
    printf("\n=== POST Diagnostic Summary ===\n");
    printf("  Last POST code: 0x%02X\n", result->post_code);
    printf("  CPU:      %s\n", result->cpu_ok      ? "PASS" : "FAIL");
    printf("  RAM:      %s\n", result->ram_ok      ? "PASS" : "FAIL");
    printf("  Video:    %s\n", result->video_ok    ? "PASS" : "FAIL");
    printf("  Keyboard: %s\n", result->keyboard_ok ? "PASS" : "FAIL");
    printf("  Disk:     %s\n", result->disk_ok     ? "PASS" : "FAIL");
    printf("  Bootstrap: %s\n", result->bootstrap_ok ? "PASS" : "FAIL");
}

void bios_dump_ivt(const IVT *ivt) {
    if (!ivt) return;
    printf("\n=== Interrupt Vector Table Dump ===\n");
    for (int i = 0; i < IVT_SIZE; i++) {
        if (ivt->vectors[i].segment != 0 || ivt->vectors[i].offset != 0) {
            printf("  INT 0x%02X: %04X:%04X\n", i,
                   ivt->vectors[i].segment, ivt->vectors[i].offset);
        }
    }
}
