/* tests/test_firmware.c -- Comprehensive firmware module test suite */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "firmware_layout.h"
#include "reset_vector.h"
#include "mmio.h"
#include "smbios_fw.h"
#include "spi_nor.h"
#include "bootblock.h"
#include "acpi_fw.h"
#include "fit_image.h"
#include "meminit.h"
#include "psci_fw.h"
#include "firmware_update.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  TEST %s... ", name); \
} while(0)

#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { tests_failed++; printf("FAIL: %s\n", msg); } while(0)

#define CHECK(cond) do { \
    if (!(cond)) { FAIL(#cond); return; } \
} while(0)

/* ??? Firmware Layout Tests ?????????????????????????????????????? */

static void test_flash_init(void)
{
    TEST("flash_init");
    FlashDevice dev;
    CHECK(flash_init(&dev, 16 * 1024 * 1024));
    CHECK(dev.size == 16 * 1024 * 1024);
    CHECK(dev.sector_size == SECTOR_SIZE);
    CHECK(dev.max_erase_cycles == 100000);
    PASS();
}

static void test_flash_read_write(void)
{
    TEST("flash_read_write");
    FlashDevice dev;
    flash_init(&dev, 4 * 4096);
    uint8_t buf[256];
    CHECK(flash_read(&dev, 0, buf, 128));
    uint8_t wbuf[256];
    memset(wbuf, 0xAA, 256);
    CHECK(flash_write(&dev, 0, wbuf, 256));
    CHECK(dev.write_count[0] == 1);
    PASS();
}

static void test_flash_erase(void)
{
    TEST("flash_erase_sector");
    FlashDevice dev;
    flash_init(&dev, 4 * 4096);
    CHECK(flash_erase_sector(&dev, 0));
    CHECK(dev.erase_count[0] == 1);
    CHECK(dev.total_erase_count == 1);
    PASS();
}

static void test_crc32(void)
{
    TEST("crc32_compute");
    const char *test_str = "123456789";
    uint32_t crc = crc32_compute((const uint8_t *)test_str, 9);
    /* Known CRC32 of "123456789" = 0xCBF43926 */
    CHECK(crc == 0xCBF43926);
    CHECK(crc32_verify((const uint8_t *)test_str, 9, crc));
    PASS();
}

static void test_fw_validate(void)
{
    TEST("fw_validate_header");
    FirmwareImage fw;
    memset(&fw, 0, sizeof(fw));
    fw.fw_magic = FW_MAGIC;
    fw.base_addr = 0xFFF00000;
    fw.entry_point = 0xFFF00100;
    fw.text_section.offset = 0xFFF00000;
    fw.text_section.size = 0x8000;
    CHECK(fw_validate_header(&fw));
    CHECK(fw_find_entry_point(&fw) == 0xFFF00100);
    PASS();
}

static void test_flash_descriptor(void)
{
    TEST("flash_descriptor");
    FlashDescriptor desc;
    CHECK(flash_desc_init(&desc, 16 * 1024 * 1024));
    CHECK(flash_desc_add_region(&desc, FLASH_REGION_DESCRIPTOR, 0, 4096));
    CHECK(flash_desc_add_region(&desc, FLASH_REGION_BIOS, 0x400000, 0x600000));
    CHECK(flash_desc_add_region(&desc, FLASH_REGION_ME, 0xA00000, 0x200000));
    CHECK(flash_desc_validate(&desc));

    const FlashRegion *r = flash_desc_find_region(&desc, FLASH_REGION_BIOS);
    CHECK(r != NULL);
    CHECK(r->offset == 0x400000);
    PASS();
}

static void test_wear_leveling(void)
{
    TEST("wear_leveling");
    FlashDevice dev;
    flash_init(&dev, 8 * 4096);
    for (int i = 0; i < 50; i++) {
        flash_erase_sector(&dev, 2);
    }
    flash_erase_sector(&dev, 0);
    uint32_t least = flash_find_least_worn_sector(&dev);
    CHECK(least != UINT32_MAX);
    CHECK(dev.erase_count[least] <= 1);
    CHECK(flash_should_relocate(&dev, 2));
    PASS();
}

/* ??? Reset Vector Tests ????????????????????????????????????????? */

static void test_reset_vector(void)
{
    TEST("reset_vector_init");
    ResetVector rv;
    CHECK(reset_vector_init(&rv, 0xFFFFFFF0));
    CHECK(rv.startup_ip == 0xFFFFFFF0);
    PASS();
}

static void test_cpu_reset(void)
{
    TEST("cpu_reset");
    ResetVector rv;
    reset_vector_init(&rv, 0xFFFF0000);
    CPUContext ctx;
    CHECK(cpu_reset(&ctx, &rv));
    CHECK(ctx.current_mode == CPU_MODE_REAL);
    CHECK(ctx.cr0 == 0x00000010);
    CHECK(ctx.eflags == 0x00000002);
    PASS();
}

static void test_cpu_mode_switch(void)
{
    TEST("cpu_mode_switch");
    CPUContext ctx;
    ResetVector rv;
    reset_vector_init(&rv, 0xFFFF0000);
    cpu_reset(&ctx, &rv);
    CHECK(cpu_switch_mode(&ctx, CPU_MODE_PROTECTED));
    CHECK(ctx.current_mode == CPU_MODE_PROTECTED);
    CHECK(cpu_switch_mode(&ctx, CPU_MODE_LONG));
    CHECK(ctx.current_mode == CPU_MODE_LONG);
    PASS();
}

/* ??? MMIO Tests ????????????????????????????????????????????????? */

static void test_mmio_map(void)
{
    TEST("mmio_map");
    MMIOManager mgr;
    mmio_init(&mgr);
    CHECK(mmio_map(&mgr, 0x3F8, 8, MMIO_DEV_UART, "UART0"));
    CHECK(mmio_map(&mgr, 0x40, 8, MMIO_DEV_TIMER, "PIT"));
    CHECK(mmio_map(&mgr, 0x500, 8, MMIO_DEV_GPIO, "GPIOA"));
    CHECK(mgr.num_regions == 3);
    PASS();
}

static void test_mmio_rw(void)
{
    TEST("mmio_read_write");
    MMIOManager mgr;
    mmio_init(&mgr);
    mmio_map(&mgr, 0x1000, 64, MMIO_DEV_TIMER, "TIMER");
    mmio_write32(&mgr, 0x1000, 0xDEADBEEF);
    uint32_t v = mmio_read32(&mgr, 0x1000);
    CHECK(v == 0xDEADBEEF);
    mmio_write8(&mgr, 0x1004, 0x42);
    uint8_t b = mmio_read8(&mgr, 0x1004);
    CHECK(b == 0x42);
    PASS();
}

/* ??? SPI NOR Tests ??????????????????????????????????????????????? */

static void test_spi_init(void)
{
    TEST("spi_init");
    SPIFlash flash;
    CHECK(spi_init(&flash, 0x20BA18, 8 * 1024 * 1024));
    CHECK(spi_read_jedec_id(&flash) == 0x20BA18);
    CHECK(spi_read_status(&flash) == 0x00);
    free(flash.data);
    PASS();
}

static void test_spi_erase_program(void)
{
    TEST("spi_erase_program");
    SPIFlash flash;
    spi_init(&flash, 0xEF4018, 4 * 1024 * 1024);
    CHECK(spi_write_enable(&flash));
    uint8_t buf[256];
    memset(buf, 0x55, 256);
    CHECK(spi_page_program(&flash, 0x1000, buf, 256));
    uint8_t rbuf[256];
    CHECK(spi_read(&flash, 0x1000, rbuf, 256));
    CHECK(memcmp(buf, rbuf, 256) == 0);
    CHECK(spi_sector_erase(&flash, 0x1000));
    free(flash.data);
    PASS();
}

/* ??? SMBIOS Tests ???????????????????????????????????????????????? */

static void test_smbios_init(void)
{
    TEST("smbios_init");
    SMBIOS smbios;
    CHECK(smbios_init(&smbios, 3, 4));
    CHECK(smbios.entry_point.major_ver == 3);
    CHECK(smbios.entry_point.minor_ver == 4);
    PASS();
}

static void test_smbios_add_table(void)
{
    TEST("smbios_add_table");
    SMBIOS smbios;
    smbios_init(&smbios, 3, 0);
    SMBIOSBIOSInfo bios;
    memset(&bios, 0, sizeof(bios));
    bios.header.type = SMBIOS_TYPE_BIOS_INFO;
    bios.header.length = sizeof(SMBIOSBIOSInfo);
    bios.header.handle = 0x0001;
    CHECK(smbios_add_table(&smbios, &bios, sizeof(bios)));
    CHECK(smbios.num_tables == 1);
    void *found = smbios_find_by_type(&smbios, SMBIOS_TYPE_BIOS_INFO);
    CHECK(found != NULL);
    PASS();
}

/* ??? Bootblock / Verified Boot Tests ???????????????????????????? */

static void test_bootblock_chain(void)
{
    TEST("bootblock_chain");
    SHA256Digest rot_hash;
    memset(&rot_hash, 0xAB, sizeof(rot_hash));

    VerificationChain vc;
    CHECK(vb_init_chain(&vc, &rot_hash));
    CHECK(vc.chain_valid == true);

    /* Register a trusted key */
    uint8_t key_fp[32];
    memset(key_fp, 0x42, 32);
    CHECK(vb_register_key(&vc, key_fp, 32));
    CHECK(vc.num_trusted_keys == 1);
    PASS();
}

static void test_bootblock_hash(void)
{
    TEST("bootblock_hash");
    const char *data = "Hello, Verified Boot!";
    SHA256Digest digest;
    CHECK(vb_compute_image_hash((const uint8_t *)data,
                                 (uint32_t)strlen(data), &digest));
    /* Verify hash is non-zero */
    bool non_zero = false;
    for (int i = 0; i < SHA256_DIGEST_SIZE; i++) {
        if (digest.data[i] != 0) { non_zero = true; break; }
    }
    CHECK(non_zero);
    PASS();
}

static void test_bootblock_extend_chain(void)
{
    TEST("bootblock_extend_chain");
    SHA256Digest rot_hash;
    memset(&rot_hash, 0x01, sizeof(rot_hash));

    VerificationChain vc;
    vb_init_chain(&vc, &rot_hash);

    SHA256Digest stage_hash;
    memset(&stage_hash, 0x02, sizeof(stage_hash));
    CHECK(vb_extend_chain(&vc, BOOT_STAGE_BOOTBLK, &stage_hash, 1));
    CHECK(vc.chain_length == 1);
    CHECK(vc.chain[0].stage == BOOT_STAGE_BOOTBLK);
    CHECK(vc.chain[0].version == 1);
    PASS();
}

static void test_bootblock_verify_chain(void)
{
    TEST("bootblock_verify_chain");
    SHA256Digest rot_hash;
    memset(&rot_hash, 0xCC, sizeof(rot_hash));

    VerificationChain vc;
    vb_init_chain(&vc, &rot_hash);

    SHA256Digest h1, h2, h3;
    memset(&h1, 0x11, sizeof(h1));
    memset(&h2, 0x22, sizeof(h2));
    memset(&h3, 0x33, sizeof(h3));

    vb_extend_chain(&vc, BOOT_STAGE_BOOTBLK, &h1, 1);
    vb_extend_chain(&vc, BOOT_STAGE_PEI, &h2, 2);
    vb_extend_chain(&vc, BOOT_STAGE_DXE, &h3, 3);

    CHECK(vb_verify_chain(&vc));
    CHECK(vc.chain_valid == true);
    CHECK(vc.last_good_version == 3);
    PASS();
}

static void test_bootblock_pcr(void)
{
    TEST("bootblock_pcr");
    PCRLog log;
    vb_init_pcr_log(&log);

    SHA256Digest m;
    memset(&m, 0xAB, sizeof(m));
    CHECK(vb_extend_pcr(&log, 0, &m, 1, "BIOS measurement"));
    CHECK(vb_extend_pcr(&log, 7, &m, 2, "Bootloader measurement"));
    CHECK(log.event_count == 2);
    CHECK(log.events[0].pcr_index == 0);
    CHECK(log.events[1].pcr_index == 7);
    PASS();
}

static void test_anti_rollback(void)
{
    TEST("anti_rollback");
    CHECK(vb_check_anti_rollback(5, 3) == true);
    CHECK(vb_check_anti_rollback(3, 5) == false);
    CHECK(vb_check_anti_rollback(3, 3) == true);
    PASS();
}

static void test_vb_header_validate(void)
{
    TEST("vb_header_validate");
    VBootHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = 0x56424F54;  /* "VBOT" */
    hdr.header_size = sizeof(VBootHeader);
    hdr.image_size = 1024;
    hdr.version = 5;
    hdr.hash_alg = HASH_ALG_SHA256;

    CHECK(vb_validate_header(&hdr, 3) == VERIFY_OK);
    CHECK(vb_validate_header(&hdr, 7) == VERIFY_ERR_ROLLBACK);
    PASS();
}

/* ??? ACPI Tests ?????????????????????????????????????????????????? */

static void test_acpi_init(void)
{
    TEST("acpi_init");
    ACPIManager mgr;
    CHECK(acpi_init(&mgr, "MINIFW", "FIRMWARE"));
    CHECK(strcmp(mgr.oem_id, "MINIFW") == 0);
    CHECK(mgr.rsdp.revision == 2);
    PASS();
}

static void test_acpi_build_fadt(void)
{
    TEST("acpi_build_fadt");
    ACPIManager mgr;
    acpi_init(&mgr, "MINIFW", "FIRMWARE");
    CHECK(acpi_build_fadt(&mgr, 1, 9, 0x000004A5));
    CHECK(mgr.table_count == 1);
    CHECK(acpi_find_table(&mgr, "FACP") != NULL);
    PASS();
}

static void test_acpi_build_madt(void)
{
    TEST("acpi_build_madt");
    ACPIManager mgr;
    acpi_init(&mgr, "MINIFW", "FIRMWARE");
    uint8_t apic_ids[] = {0, 2, 4, 6};
    CHECK(acpi_build_madt_lapic(&mgr, 0xFEE00000, 4, apic_ids));
    CHECK(mgr.table_count == 1);
    PASS();
}

static void test_acpi_checksum(void)
{
    TEST("acpi_checksum");
    ACPIManager mgr;
    acpi_init(&mgr, "OEM001", "TBL001__");
    acpi_build_fadt(&mgr, 1, 9, 0);
    acpi_set_checksum(&mgr.fadt.header);
    /* After set_checksum, the byte should make sum=0 */
    PASS();
}

/* ??? FIT Image Tests ????????????????????????????????????????????? */

static void test_fit_image_add(void)
{
    TEST("fit_image_add");
    FITImage fit;
    memset(&fit, 0, sizeof(fit));
    uint8_t kernel_data[64];
    memset(kernel_data, 0xCC, sizeof(kernel_data));

    CHECK(fit_add_image(&fit, "Linux kernel 5.15", FIT_TYPE_KERNEL,
                        0x80000000, 0x80000000, kernel_data, 64));
    CHECK(fit.image_count == 1);
    CHECK(strcmp(fit.images[0].description, "Linux kernel 5.15") == 0);
    PASS();
}

static void test_fit_config(void)
{
    TEST("fit_config");
    FITImage fit;
    memset(&fit, 0, sizeof(fit));
    CHECK(fit_add_config(&fit, "conf@1", "kernel@1", "fdt@1", true));
    CHECK(fit.config_count == 1);
    CHECK(fit.configs[0].is_default == true);

    const FITConfigNode *def = fit_default_config(&fit);
    CHECK(def != NULL);
    CHECK(def->is_default == true);
    PASS();
}

/* ??? Memory Init Tests ??????????????????????????????????????????? */

static void test_memctrl_init(void)
{
    TEST("memctrl_init");
    MemoryController mc;
    CHECK(memctrl_init(&mc));
    CHECK(mc.num_channels == 0);
    CHECK(mc.total_memory == 0);
    PASS();
}

static void test_memctrl_add_channel(void)
{
    TEST("memctrl_add_channel");
    MemoryController mc;
    memctrl_init(&mc);
    CHECK(memctrl_add_channel(&mc, MEM_CHANNEL_A, 0x00000000, 8ULL * 1024 * 1024 * 1024));
    CHECK(memctrl_add_channel(&mc, MEM_CHANNEL_B, 0x200000000ULL, 8ULL * 1024 * 1024 * 1024));
    CHECK(mc.num_channels == 2);
    CHECK(mc.total_memory == 16ULL * 1024 * 1024 * 1024);
    PASS();
}

static void test_memctrl_spd_parse(void)
{
    TEST("memctrl_spd_parse");
    SPDData spd;
    memset(&spd, 0, sizeof(spd));
    spd.dram_device_type = 0x0C;  /* DDR4 SDRAM */
    spd.density_banks    = 0x34;  /* 8 Gb die, 16 banks */
    spd.bus_width        = 0x03;  /* 64-bit */
    spd.organization     = 0x01;  /* x8 width, 1 rank */
    spd.tck_min          = 0x0A;  /* 0.625ns */
    spd.cas_latencies_byte1 = 0xFE;

    DDRTiming timing;
    uint64_t capacity;
    CHECK(memctrl_parse_spd(&spd, &timing, &capacity));
    CHECK(capacity > 0);
    CHECK(timing.frequency_mhz > 0);
    PASS();
}

static void test_memctrl_timing_calc(void)
{
    TEST("memctrl_timing_calc");
    DDRTiming t;
    memset(&t, 0, sizeof(t));
    t.frequency_mhz = 3200;
    t.cas_latency   = 22;
    t.trcd          = 22;
    t.cwl           = 20;
    uint32_t rl = memctrl_calc_read_latency(&t);
    uint32_t wl = memctrl_calc_write_latency(&t);
    CHECK(rl >= 22);
    CHECK(wl >= 20);
    PASS();
}

/* ??? PSCI Tests ?????????????????????????????????????????????????? */

static void test_psci_init(void)
{
    TEST("psci_init");
    PSCIFirmware psci;
    CHECK(psci_init(&psci, 1, 2));
    CHECK(psci.version_major == 1);
    CHECK(psci.version_minor == 2);
    CHECK(psci_call_version(&psci) == ((1 << 16) | 2));
    PASS();
}

static void test_psci_cpu_on_off(void)
{
    TEST("psci_cpu_on_off");
    PSCIFirmware psci;
    psci_init(&psci, 1, 1);
    CHECK(psci_register_cpu(&psci, 0x00000000, 0, 0x80000000));
    CHECK(psci_register_cpu(&psci, 0x00000001, 1, 0x80000000));

    /* CPU 0 is OFF initially, turn it ON */
    CHECK(psci_call_cpu_on(&psci, 0x00000000, 0x80000000, 0) == PSCI_SUCCESS);
    CHECK(psci.cpus[0].state == AFFINITY_STATE_ON);

    /* Turn it OFF */
    CHECK(psci_call_cpu_off(&psci, 0) == PSCI_SUCCESS);
    CHECK(psci.cpus[0].state == AFFINITY_STATE_OFF);
    PASS();
}

static void test_psci_system(void)
{
    TEST("psci_system_off_reset");
    PSCIFirmware psci;
    psci_init(&psci, 1, 1);
    psci_register_cpu(&psci, 0, 0, 0x80000000);
    psci_register_cpu(&psci, 1, 1, 0x80000000);

    CHECK(psci_call_cpu_on(&psci, 0, 0x80000000, 0) == PSCI_SUCCESS);
    CHECK(psci_call_cpu_on(&psci, 1, 0x80000000, 0) == PSCI_SUCCESS);

    CHECK(psci_call_system_off(&psci) == PSCI_SUCCESS);
    CHECK(psci.cpus[0].state == AFFINITY_STATE_OFF);
    CHECK(psci.cpus[1].state == AFFINITY_STATE_OFF);

    CHECK(psci_call_system_reset(&psci) == PSCI_SUCCESS);
    CHECK(psci.reset_count == 1);
    PASS();
}

static void test_psci_features(void)
{
    TEST("psci_features");
    PSCIFirmware psci;
    psci_init(&psci, 1, 0);
    CHECK(psci_call_features(&psci, PSCI_CPU_ON) == 0);
    CHECK(psci_call_features(&psci, PSCI_MEM_PROTECT) == PSCI_NOT_SUPPORTED);
    PASS();
}

/* ??? Firmware Update Tests ??????????????????????????????????????? */

static void test_fw_update_setup(void)
{
    TEST("fw_update_setup_slots");
    FwUpdateManager mgr;
    fw_update_init(&mgr);
    CHECK(fw_update_setup_slots(&mgr,
        0x400000, 0x600000,   /* Slot A: 6MB */
        0xA00000, 0x600000,   /* Slot B: 6MB */
        0x000000, 0x200000)); /* Recovery: 2MB */
    CHECK(mgr.slot_mgr.slots[FW_SLOT_A].status == SLOT_STATUS_ACTIVE);
    CHECK(mgr.slot_mgr.slots[FW_SLOT_B].status == SLOT_STATUS_STANDBY);
    CHECK(mgr.slot_mgr.active_slot == FW_SLOT_A);
    PASS();
}

static void test_fw_update_boot(void)
{
    TEST("fw_update_boot_mark");
    FwUpdateManager mgr;
    fw_update_init(&mgr);
    fw_update_setup_slots(&mgr,
        0x400000, 0x600000, 0xA00000, 0x600000, 0x000000, 0x200000);

    CHECK(fw_update_mark_boot_success(&mgr));
    CHECK(mgr.slot_mgr.slots[FW_SLOT_A].successful_boots == 1);

    CHECK(fw_update_mark_boot_failed(&mgr));
    CHECK(mgr.slot_mgr.slots[FW_SLOT_A].boot_attempts == 1);
    PASS();
}

static void test_fw_update_slot_select(void)
{
    TEST("fw_update_slot_select");
    FwUpdateManager mgr;
    fw_update_init(&mgr);
    fw_update_setup_slots(&mgr,
        0x400000, 0x600000, 0xA00000, 0x600000, 0x000000, 0x200000);

    FirmwareSlotID slot = fw_update_select_boot_slot(&mgr);
    CHECK(slot == FW_SLOT_A);
    PASS();
}

static void test_fw_update_recovery(void)
{
    TEST("fw_update_recovery");
    FwUpdateManager mgr;
    fw_update_init(&mgr);
    fw_update_setup_slots(&mgr,
        0x400000, 0x600000, 0xA00000, 0x600000, 0x000000, 0x200000);

    CHECK(fw_update_enter_recovery(&mgr));
    CHECK(mgr.recovery_mode == true);
    CHECK(mgr.slot_mgr.active_slot == FW_SLOT_RECOVERY);
    PASS();
}

/* ??? Main Test Runner ???????????????????????????????????????????? */

int main(void)
{
    printf("=== mini-firmware Test Suite ===\n\n");

    printf("--- Firmware Layout ---\n");
    test_flash_init();
    test_flash_read_write();
    test_flash_erase();
    test_crc32();
    test_fw_validate();
    test_flash_descriptor();
    test_wear_leveling();

    printf("\n--- Reset Vector ---\n");
    test_reset_vector();
    test_cpu_reset();
    test_cpu_mode_switch();

    printf("\n--- MMIO ---\n");
    test_mmio_map();
    test_mmio_rw();

    printf("\n--- SPI NOR ---\n");
    test_spi_init();
    test_spi_erase_program();

    printf("\n--- SMBIOS ---\n");
    test_smbios_init();
    test_smbios_add_table();

    printf("\n--- Bootblock (Verified Boot) ---\n");
    test_bootblock_chain();
    test_bootblock_hash();
    test_bootblock_extend_chain();
    test_bootblock_verify_chain();
    test_bootblock_pcr();
    test_anti_rollback();
    test_vb_header_validate();

    printf("\n--- ACPI Tables ---\n");
    test_acpi_init();
    test_acpi_build_fadt();
    test_acpi_build_madt();
    test_acpi_checksum();

    printf("\n--- FIT Image ---\n");
    test_fit_image_add();
    test_fit_config();

    printf("\n--- Memory Init ---\n");
    test_memctrl_init();
    test_memctrl_add_channel();
    test_memctrl_spd_parse();
    test_memctrl_timing_calc();

    printf("\n--- PSCI ---\n");
    test_psci_init();
    test_psci_cpu_on_off();
    test_psci_system();
    test_psci_features();

    printf("\n--- Firmware Update ---\n");
    test_fw_update_setup();
    test_fw_update_boot();
    test_fw_update_slot_select();
    test_fw_update_recovery();

    printf("\n========================================\n");
    printf("RESULTS: %d/%d passed, %d failed\n",
           tests_passed, tests_run, tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
