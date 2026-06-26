#include "legacy_bios.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_post_code(uint8_t code) {
    printf("  [POST 0x%02X] ", code);
    fflush(stdout);
}

static bool simulate_ps2_keyboard_init(void) {
    for (volatile int i = 0; i < 100000; i++) {}
    return true;
}

static bool simulate_video_bios_init(void) {
    for (volatile int i = 0; i < 50000; i++) {}
    return true;
}

static bool simulate_cmos_checksum(void) {
    return true;
}

int main(void) {
    printf("========================================\n");
    printf("  mini-legacy-bios: POST Demonstration\n");
    printf("========================================\n");

    IVT ivt;
    BIOSDataArea bda;
    POSTResult result;
    memset(&result, 0, sizeof(result));

    printf("\n--- Phase 1: Early Initialization ---\n");

    print_post_code(POST_CPU_TEST);
    printf("Testing CPU registers and flags...\n");
    volatile int cpu_test = 0x5A5A;
    if (cpu_test == 0x5A5A) {
        printf("  CPU register test PASSED\n");
        result.cpu_ok = true;
    }

    print_post_code(POST_CMOS_CHECKSUM);
    printf("Verifying CMOS checksum...\n");
    if (simulate_cmos_checksum()) {
        printf("  CMOS checksum OK\n");
    }

    print_post_code(POST_DMA_INIT);
    printf("Initializing DMA controller...\n");
    printf("  Channel 0: cascade\n");
    printf("  Channel 1: free\n");
    printf("  Channel 2: floppy\n");
    printf("  Channel 3: free\n");

    printf("\n--- Phase 2: Memory Test ---\n");

    print_post_code(POST_RAM_TEST_BASE);
    printf("Testing base 640KB RAM (0x00000–0x9FFFF)...\n");
    uint16_t base_kb = BIOS_BASE_MEMORY_KB;
    printf("  Writing patterns to each 64KB block...\n");
    printf("  Reading and verifying patterns...\n");
    printf("  Base memory test: %u KB PASSED\n", base_kb);
    result.ram_ok = true;

    print_post_code(POST_RAM_TEST_EXTENDED);
    printf("Testing extended memory (0x100000–0xFFFFFF)...\n");
    printf("  Memory above 1MB available\n");

    printf("\n--- Phase 3: Device Initialization ---\n");

    print_post_code(POST_VIDEO_INIT);
    printf("Initializing video BIOS...\n");
    bios_int10h_video(VIDEO_SET_MODE, 0x03, 0, 0, 0, 0);
    if (simulate_video_bios_init()) {
        printf("  Video BIOS initialized (80x25 text mode)\n");
        result.video_ok = true;
    }

    print_post_code(POST_KEYBOARD_INIT);
    printf("Initializing keyboard controller (8042)...\n");
    if (simulate_ps2_keyboard_init()) {
        printf("  Keyboard BAT passed\n");
        printf("  Keyboard: US 101-key layout\n");
        result.keyboard_ok = true;
    }

    printf("\n--- Phase 4: System BIOS Initialization ---\n");

    print_post_code(POST_INT_VECTORS);
    printf("Setting up Interrupt Vector Table...\n");
    bios_init_ivt(&ivt);

    bios_set_interrupt(&ivt, INT_VIDEO_SERVICES, 0xF000, 0x0100);
    bios_set_interrupt(&ivt, INT_DISK_SERVICES,  0xF000, 0x0200);
    bios_set_interrupt(&ivt, INT_KEYBOARD,       0xF000, 0x0300);
    bios_set_interrupt(&ivt, INT_SERIAL_SERVICES,0xF000, 0x0400);
    bios_set_interrupt(&ivt, INT_PRINTER,        0xF000, 0x0500);
    bios_set_interrupt(&ivt, INT_BOOTSTRAP,      0xF000, 0x0600);
    bios_set_interrupt(&ivt, INT_TIME,           0xF000, 0x0700);
    bios_set_interrupt(&ivt, INT_MEMORY_SIZE,    0xF000, 0x0800);

    printf("  Installed 8 BIOS interrupt handlers at F000:xxxx\n");

    printf("\n--- Phase 5: BIOS Data Area ---\n");
    memset(&bda, 0, sizeof(BIOSDataArea));
    bda.com_ports[0]    = 0x03F8;  /* COM1 */
    bda.com_ports[1]    = 0x02F8;  /* COM2 */
    bda.lpt_ports[0]    = 0x0378;  /* LPT1 */
    bda.lpt_ports[1]    = 0x0278;  /* LPT2 */
    bda.equipment_list  = EQUIP_FPU_PRESENT | EQUIP_VGA_PRESENT | (1 << 6);
    bda.memory_size_kb  = BIOS_BASE_MEMORY_KB;
    bda.video_mode      = 0x03;
    bda.video_columns   = 80;
    bda.active_display_page = 0;
    bda.crtc_port       = 0x03D4;
    bda.keyboard_buffer_head = 0x1E;
    bda.keyboard_buffer_tail = 0x1E;

    bios_print_bda(&bda);

    printf("\n--- Phase 6: Bootstrap ---\n");

    print_post_code(POST_BOOTSTRAP);
    printf("Attempting INT 0x19 bootstrap...\n");
    result.disk_ok = true;

    int boot_result = bios_int19h_bootstrap(&ivt);
    if (boot_result == 0) {
        result.bootstrap_ok = true;
    } else {
        printf("  Bootstrap failed with code %d\n", boot_result);
    }

    print_post_code(POST_COMPLETE);
    printf("POST sequence complete\n");

    result.post_code = POST_COMPLETE;
    bios_print_post_result(&result);

    printf("\n--- IVT Entries Dump ---\n");
    for (int i = 0; i < IVT_SIZE; i++) {
        IVTEntry e = bios_get_interrupt(&ivt, i);
        if (e.segment != 0 || e.offset != 0) {
            printf("  INT 0x%02X: %04X:%04X\n", i, e.segment, e.offset);
        }
    }

    printf("\n=== mini-legacy-bios: Demo Complete ===\n");
    return 0;
}
