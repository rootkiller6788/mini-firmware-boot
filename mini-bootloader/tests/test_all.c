#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "stage1.h"
#include "stage2.h"
#include "linux_boot.h"
#include "grub_modules.h"
#include "filesys_boot.h"
#include "boot_elf.h"
#include "boot_compress.h"
#include "boot_memory.h"
#include "boot_chain.h"
#include "boot_config.h"
#include "boot_secure.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  TEST %s ... ", name); \
} while(0)

#define PASS() do { \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

#define FAIL(msg) do { \
    tests_failed++; \
    printf("FAIL: %s\n", msg); \
} while(0)

#define CHECK(cond, msg) do { \
    if (!(cond)) { FAIL(msg); return; } \
} while(0)

/* =================================================================
 * Stage 1 (MBR/VBR) Tests
 * ================================================================= */
static void test_mbr_init(void)
{
    TEST("mbr_init");
    MBR mbr;
    mbr_init(&mbr);
    CHECK(mbr.signature == MBR_SIGNATURE, "signature mismatch");
    CHECK(mbr.bootstrap_code[0] == 0x31, "bootstrap code missing");
    PASS();
}

static void test_mbr_validate(void)
{
    TEST("mbr_validate");
    MBR mbr;
    mbr_init(&mbr);
    CHECK(mbr_validate(&mbr), "valid MBR rejected");
    mbr.signature = 0;
    CHECK(!mbr_validate(&mbr), "invalid MBR accepted");
    PASS();
}

static void test_mbr_find_bootable(void)
{
    TEST("mbr_find_bootable");
    MBR mbr;
    mbr_init(&mbr);
    PartitionEntry *entries = (PartitionEntry *)(mbr.partition_table);
    entries[0].status = BOOTABLE_MARK;
    entries[0].first_lba = 63;
    entries[0].sectors = 2048;
    PartitionEntry *boot = mbr_find_bootable(&mbr);
    CHECK(boot != NULL, "bootable partition not found");
    CHECK(boot->first_lba == 63, "wrong LBA");
    PASS();
}

/* =================================================================
 * Stage 2 (Multiboot) Tests
 * ================================================================= */
static void test_multiboot_init(void)
{
    TEST("multiboot_init");
    MultibootHeader header;
    MultibootInfo info;
    stage2_init(&header, &info);
    CHECK(header.magic == MULTIBOOT_MAGIC, "magic mismatch");
    CHECK(info.mem_lower == 640, "mem_lower mismatch");
    PASS();
}

static void test_multiboot_parse_header(void)
{
    TEST("multiboot_parse_header");
    MultibootHeader header;
    MultibootInfo info;
    stage2_init(&header, &info);
    /* Fix checksum for validation: magic + flags + checksum == 0 */
    header.load_addr = 0x100000;
    header.load_end_addr = 0x500000;
    header.bss_end_addr = 0x600000;
    header.entry_addr = 0x101000;
    header.checksum = (uint32_t)(-(int32_t)(header.magic + header.flags));
    CHECK(stage2_parse_multiboot_header(&header), "valid header rejected");
    header.magic = 0xDEADBEEF;
    CHECK(!stage2_parse_multiboot_header(&header), "invalid header accepted");
    PASS();
}

static void test_multiboot_memory_map(void)
{
    TEST("multiboot_memory_map");
    MultibootHeader header;
    MultibootInfo info;
    stage2_init(&header, &info);
    MemoryMapEntry entries[3];
    entries[0].base_addr = 0; entries[0].length = 0x9FC00; entries[0].type = MEMORY_FREE;
    entries[1].base_addr = 0x100000; entries[1].length = 0x1000000; entries[1].type = MEMORY_FREE;
    entries[2].base_addr = 0x1100000; entries[2].length = 0x10000; entries[2].type = MEMORY_RESERVED;
    CHECK(stage2_setup_memory_map(&info, entries, 3), "memory map setup failed");
    CHECK(info.flags & MULTIBOOT_INFO_MMAP, "MMAP flag not set");
    PASS();
}

/* =================================================================
 * Linux Boot Tests
 * ================================================================= */
static void test_linux_boot_init(void)
{
    TEST("linux_boot_init");
    LinuxBootContext ctx;
    linux_boot_init(&ctx);
    CHECK(ctx.kernel_load_addr == KERNEL_BASE_ADDR, "kernel load addr mismatch");
    CHECK(ctx.e820_count > 0, "no E820 entries");
    PASS();
}

static void test_linux_parse_header(void)
{
    TEST("linux_parse_header");
    LinuxBootContext ctx;
    linux_boot_init(&ctx);
    BootParams params;
    memset(&params, 0, sizeof(params));
    params.boot_flag = LINUX_BOOT_SIGNATURE;
    params.header = LINUX_HEADER_MAGIC;
    params.setup_sects = 4;
    CHECK(linux_parse_setup_header(&ctx, &params), "valid header rejected");
    params.boot_flag = 0;
    CHECK(!linux_parse_setup_header(&ctx, &params), "invalid header accepted");
    PASS();
}

static void test_linux_e820(void)
{
    TEST("linux_e820");
    LinuxBootContext ctx;
    linux_boot_init(&ctx);
    CHECK(ctx.e820_count <= E820MAX, "E820 overflow");
    bool has_ram = false;
    uint32_t i;
    for (i = 0; i < ctx.e820_count; i++) {
        if (ctx.e820_map[i].type == E820_RAM) has_ram = true;
    }
    CHECK(has_ram, "no RAM regions in E820 map");
    PASS();
}

/* =================================================================
 * GRUB Modules Tests
 * ================================================================= */
static void test_grub_module_load(void)
{
    TEST("grub_module_load");
    GRUBModuleList list;
    grub_module_list_init(&list);
    GRUBModuleHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = MODULE_TYPE_FILESYSTEM;
    hdr.size = sizeof(hdr);
    CHECK(grub_load_module(&list, "ext2_fs", &hdr, NULL, NULL), "module load failed");
    CHECK(list.count == 1, "count mismatch");
    PASS();
}

static void test_grub_find_module(void)
{
    TEST("grub_find_module");
    GRUBModuleList list;
    grub_module_list_init(&list);
    GRUBModuleHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = MODULE_TYPE_DISK;
    grub_load_module(&list, "ata_driver", &hdr, NULL, NULL);
    grub_load_module(&list, "nvme_driver", &hdr, NULL, NULL);
    const GRUBModule *mod = grub_find_module(&list, "ata_driver");
    CHECK(mod != NULL, "module not found");
    CHECK(strcmp(mod->name, "ata_driver") == 0, "wrong module name");
    CHECK(grub_find_module(&list, "nonexistent") == NULL, "found nonexistent");
    PASS();
}

static void test_grub_topological_sort(void)
{
    TEST("grub_topological_sort");
    GRUBModuleList list;
    grub_module_list_init(&list);
    GRUBModuleHeader hdr_a, hdr_b, hdr_c;
    memset(&hdr_a, 0, sizeof(hdr_a)); hdr_a.type = MODULE_TYPE_DISK; hdr_a.size = 100;
    memset(&hdr_b, 0, sizeof(hdr_b)); hdr_b.type = MODULE_TYPE_FILESYSTEM; hdr_b.size = 200;
    memset(&hdr_c, 0, sizeof(hdr_c)); hdr_c.type = MODULE_TYPE_BOOT; hdr_c.size = 300;
    /* B depends on A (index 0) */
    hdr_b.dep_count = 1;
    hdr_b.dependencies[0] = 0;
    grub_load_module(&list, "A", &hdr_a, NULL, NULL);
    grub_load_module(&list, "B", &hdr_b, NULL, NULL);
    grub_load_module(&list, "C", &hdr_c, NULL, NULL);
    int order[32], count = 0;
    CHECK(grub_dependency_resolve(&list, order, &count), "dependency resolve failed");
    CHECK(count == 3, "wrong order count");
    /* A must come before B */
    int a_pos = -1, b_pos = -1;
    int i;
    for (i = 0; i < count; i++) {
        if (order[i] == 0) a_pos = i;
        if (order[i] == 1) b_pos = i;
    }
    CHECK(a_pos < b_pos, "dependency order violated");
    PASS();
}

/* =================================================================
 * Filesys Boot Tests
 * ================================================================= */
static void test_bootfs_mount(void)
{
    TEST("bootfs_mount");
    BootFS fs;
    bootfs_mount(&fs, NULL, 1024 * 1024, BOOTFS_FAT32, NULL);
    CHECK(fs.type == BOOTFS_FAT32, "type mismatch");
    CHECK(fs.sb.fat32.bytes_per_sector == 512, "sector size mismatch");
    PASS();
}

static void test_bootfs_list_dir(void)
{
    TEST("bootfs_list_dir");
    BootFS fs;
    bootfs_mount(&fs, NULL, 1024 * 1024, BOOTFS_EXT2, NULL);
    DirEntry entries[16];
    int count = bootfs_list_dir(&fs, "/", entries, 16);
    CHECK(count > 0, "no entries");
    PASS();
}

static void test_bootfs_find_file(void)
{
    TEST("bootfs_find_file");
    BootFS fs;
    bootfs_mount(&fs, NULL, 1024 * 1024, BOOTFS_FAT32, NULL);
    DirEntry entry;
    CHECK(bootfs_find_file(&fs, "vmlinuz", &entry), "vmlinuz not found");
    CHECK(strcmp(entry.name, "vmlinuz") == 0, "wrong filename");
    PASS();
}

/* =================================================================
 * ELF Loader Tests
 * ================================================================= */
static void test_elf_validate(void)
{
    TEST("elf_validate");
    uint8_t elf_image[128];
    memset(elf_image, 0, sizeof(elf_image));
    elf_image[0] = 0x7F; elf_image[1] = 'E';
    elf_image[2] = 'L';  elf_image[3] = 'F';
    elf_image[4] = ELFCLASS32;
    elf_image[5] = ELFDATA2LSB;
    elf_image[6] = EV_CURRENT;

    BootElfContext ctx;
    boot_elf_init(&ctx);
    ctx.image = elf_image;
    ctx.image_size = sizeof(elf_image);
    CHECK(boot_elf_validate(&ctx), "valid ELF rejected");

    elf_image[2] = 'X';
    CHECK(!boot_elf_validate(&ctx), "invalid ELF accepted");
    PASS();
}

static void test_elf_parse_header(void)
{
    TEST("elf_parse_header");
    uint8_t elf_image[256];
    memset(elf_image, 0, sizeof(elf_image));
    elf_image[0] = 0x7F; elf_image[1] = 'E';
    elf_image[2] = 'L';  elf_image[3] = 'F';
    elf_image[4] = ELFCLASS32;
    elf_image[5] = ELFDATA2LSB;
    elf_image[6] = EV_CURRENT;
    /* Set e_type = ET_EXEC, e_machine = 3 (i386) */
    *(uint16_t *)(elf_image + 16) = ET_EXEC;
    *(uint16_t *)(elf_image + 18) = 3;
    *(uint32_t *)(elf_image + 24) = 0x100000;  /* entry */

    BootElfContext ctx;
    boot_elf_init(&ctx);
    ctx.image = elf_image;
    ctx.image_size = sizeof(elf_image);
    CHECK(boot_elf_parse_header(&ctx), "parse failed");
    CHECK(ctx.elf_class == ELFCLASS32, "wrong class");
    PASS();
}

/* =================================================================
 * Compression Tests
 * ================================================================= */
static void test_rle_decompress(void)
{
    TEST("rle_decompress");
    /* Encode: 5x 'A' + 3x 'B' */
    uint8_t input[] = {5, 'A', 3, 'B', 0, 0};  /* 0 = EOF */
    uint8_t output[128];
    uint32_t out_size = 0;

    CHECK(rle_decompress(input, sizeof(input), output, &out_size, 128),
          "rle decompress failed");
    CHECK(out_size == 8, "wrong output size");
    CHECK(memcmp(output, "AAAAABBB", 8) == 0, "wrong content");
    PASS();
}

static void test_lz77_decompress(void)
{
    TEST("lz77_decompress");
    LZ77Token tokens[6];
    tokens[0].is_literal = true;  tokens[0].literal = 'H';
    tokens[1].is_literal = true;  tokens[1].literal = 'e';
    tokens[2].is_literal = true;  tokens[2].literal = 'l';
    tokens[3].is_literal = true;  tokens[3].literal = 'l';
    tokens[4].is_literal = true;  tokens[4].literal = 'o';
    tokens[5].is_literal = false; tokens[5].length = 5; tokens[5].distance = 5;
    /* Token[5] copies 5 bytes from offset 5 back: "Hello" */
    uint8_t output[128];
    uint32_t out_size = 0;
    CHECK(lz77_decompress(tokens, 6, output, &out_size, 128), "lz77 failed");
    CHECK(out_size == 10, "wrong output size");
    CHECK(memcmp(output, "HelloHello", 10) == 0, "wrong content");
    PASS();
}

static void test_gzip_validate_header(void)
{
    TEST("gzip_validate_header");
    GzipHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.id1 = GZIP_MAGIC1;
    hdr.id2 = GZIP_MAGIC2;
    hdr.cm  = GZIP_CM_DEFLATE;
    CHECK(gzip_validate_header(&hdr), "valid gzip rejected");
    hdr.id2 = 0;
    CHECK(!gzip_validate_header(&hdr), "invalid gzip accepted");
    PASS();
}

/* =================================================================
 * Memory Map Tests
 * ================================================================= */
static void test_bootmem_add_region(void)
{
    TEST("bootmem_add_region");
    BootMemoryMap map;
    bootmem_init(&map);
    CHECK(bootmem_add_region(&map, 0, 0x100000, MEMTYPE_FREE), "add failed");
    CHECK(bootmem_add_region(&map, 0x100000, 0x100000, MEMTYPE_RESERVED), "add2 failed");
    CHECK(map.count == 2, "count mismatch");
    PASS();
}

static void test_bootmem_sanitize(void)
{
    TEST("bootmem_sanitize");
    BootMemoryMap map;
    bootmem_init(&map);
    /* Overlapping regions: should sanitize */
    bootmem_add_region(&map, 0, 0x200000, MEMTYPE_FREE);
    bootmem_add_region(&map, 0x100000, 0x200000, MEMTYPE_FREE);
    CHECK(bootmem_sanitize(&map), "sanitize failed");
    CHECK(map.free_memory > 0, "no free memory");
    PASS();
}

static void test_bootmem_is_available(void)
{
    TEST("bootmem_is_available");
    BootMemoryMap map;
    bootmem_init(&map);
    bootmem_add_region(&map, 0x100000, 0x1000000, MEMTYPE_FREE);
    bootmem_sanitize(&map);
    CHECK(bootmem_is_available(&map, 0x100000, 0x100000), "available rejected");
    CHECK(!bootmem_is_available(&map, 0, 0x100000), "unavailable accepted");
    PASS();
}

static void test_bootmem_page_alloc(void)
{
    TEST("bootmem_page_alloc");
    BootMemoryMap map;
    bootmem_init(&map);
    bootmem_add_region(&map, 0, 1024 * 1024, MEMTYPE_FREE);
    uint64_t heap_base = 0x10000000;
    uint64_t heap_size = 1024 * 1024;
    CHECK(bootmem_init_allocator(&map, heap_base, heap_size), "allocator init failed");
    uint64_t p1 = bootmem_alloc_pages(&map, 4);
    CHECK(p1 >= heap_base, "page alloc failed");
    uint64_t p2 = bootmem_alloc_pages(&map, 4);
    CHECK(p2 != 0 && p2 != p1, "second alloc failed/duplicate");
    CHECK(bootmem_free_pages(&map, p1, 4), "free failed");
    free(map.page_bitmap);
    PASS();
}

/* =================================================================
 * Chain Loading Tests
 * ================================================================= */
static void test_chs_lba_translation(void)
{
    TEST("chs_lba_translation");
    /* LBA 0 = CHS (0,0,1) */
    CHSAddr chs = chs_from_lba(0, 16, 63);
    CHECK(chs.cylinder == 0 && chs.head == 0 && chs.sector == 1, "LBA0 CHS wrong");
    uint32_t lba = lba_from_chs(chs, 16, 63);
    CHECK(lba == 0, "round-trip failed");

    /* LBA for typical boot partition: 63 sectors/track, 16 heads */
    CHSAddr chs2 = chs_from_lba(63, 16, 63);
    CHECK(chs2.cylinder == 0 && chs2.head == 1 && chs2.sector == 1,
          "LBA63 CHS wrong");
    PASS();
}

static void test_sector_io(void)
{
    TEST("sector_io");
    uint8_t disk[1024];
    memset(disk, 0, sizeof(disk));
    disk[510] = 0x55; disk[511] = 0xAA;

    SectorIO io;
    sector_io_init(&io, disk, sizeof(disk));
    uint8_t buf[512];
    CHECK(sector_read(&io, 0, buf), "sector read failed");
    CHECK(buf[510] == 0x55 && buf[511] == 0xAA, "signature mismatch");

    /* Write to sector 1 */
    uint8_t write_buf[512];
    memset(write_buf, 0xAB, 512);
    CHECK(sector_write(&io, 1, write_buf), "sector write failed");
    CHECK(disk[512] == 0xAB, "write did not persist");
    PASS();
}

static void test_gpt_parse_header(void)
{
    TEST("gpt_parse_header");
    uint8_t sector[512];
    memset(sector, 0, sizeof(sector));
    /* Write "EFI PART" signature + revision + header_size */
    memcpy(sector, "EFI PART", 8);
    /* revision = 0x00010000 at offset 8 (little-endian) */
    sector[8] = 0x00; sector[9] = 0x00; sector[10] = 0x01; sector[11] = 0x00;
    /* header_size = 92 at offset 12 */
    sector[12] = 92; sector[13] = 0; sector[14] = 0; sector[15] = 0;
    GPTHeader gpt;
    CHECK(gpt_parse_header(sector, &gpt), "GPT parse failed");
    CHECK(gpt_validate_header(&gpt), "GPT validate failed");
    PASS();
}

/* =================================================================
 * Boot Config Tests
 * ================================================================= */
static void test_bootcfg_tokenize(void)
{
    TEST("bootcfg_tokenize");
    char *tokens[16];
    char bufs[16][128];
    int i;
    for (i = 0; i < 16; i++) tokens[i] = bufs[i];

    int n = bootcfg_tokenize("linux /vmlinuz root=/dev/sda1 ro", tokens, 16);
    CHECK(n == 4, "wrong token count");
    CHECK(strcmp(tokens[0], "linux") == 0, "wrong token0");
    CHECK(strcmp(tokens[1], "/vmlinuz") == 0, "wrong token1");
    PASS();
}

static void test_bootcfg_parse(void)
{
    TEST("bootcfg_parse");
    const char *config =
        "timeout=5\n"
        "default=0\n"
        "menuentry \"Linux\" {\n"
        "    linux /boot/vmlinuz root=/dev/sda1 ro\n"
        "    initrd /boot/initrd.img\n"
        "}\n"
        "menuentry \"Test OS\" {\n"
        "    linux /test/kernel\n"
        "}\n";

    BootConfig cfg;
    bootcfg_init(&cfg);
    CHECK(bootcfg_parse(&cfg, config), "parse failed");
    CHECK(cfg.entry_count == 2, "wrong entry count");
    CHECK(cfg.timeout_seconds == 5, "wrong timeout");

    const BootMenuEntry *def = bootcfg_get_default(&cfg);
    CHECK(def != NULL, "no default entry");
    CHECK(strcmp(def->title, "Linux") == 0, "wrong default title");
    PASS();
}

static void test_bootcfg_add_entry(void)
{
    TEST("bootcfg_add_entry");
    BootConfig cfg;
    bootcfg_init(&cfg);
    CHECK(bootcfg_add_entry(&cfg, "Test", "/kernel", "/initrd", "ro quiet"),
          "add entry failed");
    CHECK(cfg.entry_count == 1, "count mismatch");
    const BootMenuEntry *e = bootcfg_get_entry(&cfg, 0);
    CHECK(e != NULL, "entry not found");
    CHECK(strcmp(e->title, "Test") == 0, "title mismatch");
    PASS();
}

/* =================================================================
 * Boot Security Tests
 * ================================================================= */
static void test_sha256_known(void)
{
    TEST("sha256_known_vector");
    /* SHA-256 of empty string:
     * e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 */
    const uint8_t expected[32] = {
        0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,
        0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
        0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,
        0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55
    };
    uint8_t digest[32];
    sha256_hash((const uint8_t *)"", 0, digest);
    CHECK(memcmp(digest, expected, 32) == 0, "empty hash mismatch");
    PASS();
}

static void test_sha256_verify(void)
{
    TEST("sha256_verify");
    const char *msg = "mini-bootloader";
    uint8_t digest[32];
    sha256_hash((const uint8_t *)msg, (uint32_t)strlen(msg), digest);
    CHECK(sha256_verify((const uint8_t *)msg, (uint32_t)strlen(msg), digest),
          "self-verify failed");
    digest[0] ^= 0xFF;
    CHECK(!sha256_verify((const uint8_t *)msg, (uint32_t)strlen(msg), digest),
          "tampered verify passed");
    PASS();
}

static void test_pcr_extend(void)
{
    TEST("pcr_extend");
    PCRBank bank;
    pcr_bank_init(&bank);

    uint8_t digest[32];
    memset(digest, 0xAB, 32);

    CHECK(pcr_extend(&bank, 0, digest), "extend failed");
    CHECK(bank.initialized[0], "PCR0 not initialized");

    /* Extending again should change the value */
    uint8_t pcr0_before[32];
    memcpy(pcr0_before, bank.pcrs[0], 32);
    CHECK(pcr_extend(&bank, 0, digest), "second extend failed");
    CHECK(memcmp(bank.pcrs[0], pcr0_before, 32) != 0, "PCR unchanged");
    PASS();
}

static void test_measured_boot(void)
{
    TEST("measured_boot");
    BootMeasureLog log;
    boot_measure_init(&log);

    uint8_t data1[64];
    memset(data1, 0xCC, sizeof(data1));
    CHECK(boot_measure_event(&log, 0, data1, sizeof(data1),
                             "BIOS", 1), "event 1 failed");

    uint8_t data2[64];
    memset(data2, 0xDD, sizeof(data2));
    CHECK(boot_measure_event(&log, 0, data2, sizeof(data2),
                             "MBR", 2), "event 2 failed");

    CHECK(log.event_count == 2, "event count mismatch");
    PASS();
}

static void test_boot_verify(void)
{
    TEST("boot_verify_memory");
    const char *data = "kernel payload";
    uint8_t hash[32];
    sha256_hash((const uint8_t *)data, (uint32_t)strlen(data), hash);
    CHECK(boot_verify_memory_range((const uint8_t *)data,
                                   (uint32_t)strlen(data), hash),
          "verification failed");
    PASS();
}

/* =================================================================
 * Main test runner
 * ================================================================= */
int main(void)
{
    printf("=== mini-bootloader Test Suite ===\n\n");

    /* Stage 1 */
    test_mbr_init();
    test_mbr_validate();
    test_mbr_find_bootable();

    /* Stage 2 */
    test_multiboot_init();
    test_multiboot_parse_header();
    test_multiboot_memory_map();

    /* Linux Boot */
    test_linux_boot_init();
    test_linux_parse_header();
    test_linux_e820();

    /* GRUB Modules */
    test_grub_module_load();
    test_grub_find_module();
    test_grub_topological_sort();

    /* Filesys Boot */
    test_bootfs_mount();
    test_bootfs_list_dir();
    test_bootfs_find_file();

    /* ELF Loader */
    test_elf_validate();
    test_elf_parse_header();

    /* Compression */
    test_rle_decompress();
    test_lz77_decompress();
    test_gzip_validate_header();

    /* Memory Map */
    test_bootmem_add_region();
    test_bootmem_sanitize();
    test_bootmem_is_available();
    test_bootmem_page_alloc();

    /* Chain Loading */
    test_chs_lba_translation();
    test_sector_io();
    test_gpt_parse_header();

    /* Boot Config */
    test_bootcfg_tokenize();
    test_bootcfg_parse();
    test_bootcfg_add_entry();

    /* Boot Security */
    test_sha256_known();
    test_sha256_verify();
    test_pcr_extend();
    test_measured_boot();
    test_boot_verify();

    /* Summary */
    printf("\n=== Results ===\n");
    printf("Total:  %d\n", tests_run);
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    if (tests_failed > 0) {
        printf("\n*** SOME TESTS FAILED ***\n");
        return 1;
    }

    printf("\n*** ALL TESTS PASSED ***\n");
    return 0;
}
