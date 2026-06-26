#include "cache_as_ram.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct __attribute__((packed)) {
    uint32_t signature;
    uint32_t version;
    uint64_t pei_stack_base;
    uint64_t pei_stack_size;
    uint64_t mem_controller_base;
    uint64_t cpu_bsp_apic_id;
    uint32_t boot_mode;
    uint32_t pei_core_entry;
} PEICoreData;

static void print_dram_hex(const uint8_t *buf, uint64_t len, const char *label)
{
    printf("  %s (%llu bytes):\n    ", label, (unsigned long long)len);
    for (uint64_t i = 0; i < len && i < 128; i++) {
        printf("%02X ", buf[i]);
        if ((i + 1) % 32 == 0 && i + 1 < len && i + 1 < 128) {
            printf("\n    ");
        }
    }
    printf("\n");
}

int main(void)
{
    printf("============================================================\n");
    printf("  mini-boot-process: Cache-as-RAM (CAR) Demo\n");
    printf("  Pre-memory initialization stack in CPU cache\n");
    printf("============================================================\n\n");

    CARState car;
    car_init(&car);
    car_print_state(&car);

    printf("\n--- Step 1: Enable CAR (Cache-as-RAM) ---\n");
    bool enabled = car_enable(&car);
    printf("CAR enabled: %s\n\n", enabled ? "YES" : "NO");
    car_print_state(&car);

    printf("\n--- Step 2: Store PEI Core Data in CAR ---\n");
    PEICoreData pei_data = {
        .signature         = 0x50454943,
        .version           = 0x00010000,
        .pei_stack_base    = CAR_STACK_TOP - 0x1000,
        .pei_stack_size    = 0x1000,
        .mem_controller_base = 0xFED1C000,
        .cpu_bsp_apic_id   = 0,
        .boot_mode         = 0,
        .pei_core_entry    = 0xFFF00000
    };

    car_write_block(&car, 0x100, &pei_data, sizeof(PEICoreData));
    printf("  PEI Core Data written to CAR offset 0x100:\n");
    printf("    Signature:          0x%08X\n", pei_data.signature);
    printf("    Version:            0x%08X\n", pei_data.version);
    printf("    PEI Stack Base:     0x%08llX\n", (unsigned long long)pei_data.pei_stack_base);
    printf("    PEI Core Entry:     0x%08X\n", pei_data.pei_core_entry);
    printf("    Boot Mode:          %u\n", pei_data.boot_mode);
    printf("    Memory Controller:  0x%08llX\n", (unsigned long long)pei_data.mem_controller_base);

    printf("\n--- Step 3: Read back PEI Core Data from CAR ---\n");
    PEICoreData readback;
    memset(&readback, 0, sizeof(readback));
    car_read_block(&car, 0x100, &readback, sizeof(PEICoreData));

    printf("  Read-back verification:\n");
    printf("    Signature:  0x%08X %s\n",
           readback.signature,
           readback.signature == pei_data.signature ? "(MATCH)" : "(MISMATCH!)");
    printf("    Version:    0x%08X %s\n",
           readback.version,
           readback.version == pei_data.version ? "(MATCH)" : "(MISMATCH!)");
    printf("    PEI Entry:  0x%08X %s\n",
           readback.pei_core_entry,
           readback.pei_core_entry == pei_data.pei_core_entry ? "(MATCH)" : "(MISMATCH!)");

    printf("\n--- Step 4: Store temporary data patterns in CAR ---\n");
    uint64_t test_offsets[] = {0x000, 0x080, 0x200, 0x500, 0x1000, 0x2000};
    uint64_t test_values[] = {
        0xDEADBEEFCAFEBABEULL,
        0xFEEDFACEC001C0DEULL,
        0xBADDCAFE01234567ULL,
        0x89ABCDEF76543210ULL,
        0x0011223344556677ULL,
        0xAABBCCDDEEFF0011ULL
    };

    for (int i = 0; i < 6; i++) {
        car_write(&car, test_offsets[i], test_values[i]);
        uint64_t read_val = car_read(&car, test_offsets[i]);
        printf("  CAR[0x%04llX] = 0x%016llX (read: 0x%016llX) %s\n",
               (unsigned long long)test_offsets[i],
               (unsigned long long)test_values[i],
               (unsigned long long)read_val,
               read_val == test_values[i] ? "OK" : "FAIL");
    }

    printf("\n--- Step 5: Simulate PEI code running from CAR ---\n");
    printf("  [PEI-CORE] Stack: 0x%08llX\n", (unsigned long long)CAR_STACK_TOP);
    printf("  [PEI-CORE] PEIM: MemoryInit loads from CAR FV...\n");

    uint8_t msr_data[64];
    memset(msr_data, 0xAB, sizeof(msr_data));
    car_write_block(&car, 0x3000, msr_data, sizeof(msr_data));

    uint8_t msr_read[64];
    car_read_block(&car, 0x3000, msr_read, sizeof(msr_read));
    printf("  [PEI-CORE] MSR init data written to CAR offset 0x3000.\n");
    printf("  [PEI-CORE] msr_data[0..7] = ");
    for (int i = 0; i < 8; i++) printf("%02X ", msr_read[i]);
    printf("\n");

    printf("\n--- Step 6: CAR Teardown to DRAM ---\n");
    printf("  DRAM is now initialized. Flushing CAR to DRAM...\n");

    uint8_t *dram = (uint8_t *)malloc(CAR_SIZE);
    if (!dram) {
        printf("  [ERROR] Failed to allocate DRAM buffer.\n");
        return 1;
    }
    memset(dram, 0, CAR_SIZE);

    printf("  DRAM before teardown (first 64 bytes):\n");
    print_dram_hex(dram, 64, "DRAM[0000-0040]");

    car_teardown(&car, dram);

    printf("\n  DRAM after teardown (first 128 bytes):\n");
    print_dram_hex(dram, 128, "DRAM[0000-0080]");

    printf("  DRAM[0x100-0x120] (PEI Core Data):\n");
    print_dram_hex(dram + 0x100, 32, "DRAM[0100-0120]");

    printf("\n  Data integrity check:\n");
    for (int i = 0; i < 6; i++) {
        uint64_t dram_val = 0;
        if (test_offsets[i] < CAR_SIZE) {
            memcpy(&dram_val, dram + test_offsets[i], sizeof(uint64_t));
        }
        printf("  CAR[0x%04llX] -> DRAM[0x%04llX] = 0x%016llX %s\n",
               (unsigned long long)test_offsets[i],
               (unsigned long long)test_offsets[i],
               (unsigned long long)dram_val,
               dram_val == test_values[i] ? "(PRESERVED)" : "(CORRUPTED!)");
    }

    printf("\n");
    car_print_state(&car);

    free(dram);

    printf("\n--- Step 7: Verify CAR is disabled after teardown ---\n");
    printf("  CAR address check: 0x%08X in CAR? %s\n",
           CAR_BASE + 0x100,
           car_is_addr_in_car(CAR_BASE + 0x100) ? "YES" : "NO");
    printf("  DRAM address check: 0x%08X in CAR? %s\n",
           0x01000000,
           car_is_addr_in_car(0x01000000) ? "YES" : "NO");

    uint64_t after_teardown = car_read(&car, 0x100);
    printf("  Read CAR[0x100] after teardown: 0x%016llX (should be 0)\n",
           (unsigned long long)after_teardown);

    printf("\n============================================================\n");
    printf("  Cache-as-RAM Demo Complete!\n");
    printf("============================================================\n");

    return 0;
}
