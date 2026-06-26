#include "spi_protection.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *master_name(uint8_t id) {
    switch (id) {
    case SPI_MASTER_BIOS: return "BIOS";
    case SPI_MASTER_ME:   return "ME";
    case SPI_MASTER_GBE:  return "GBE";
    case SPI_MASTER_HOST: return "Host";
    default:              return "Unknown";
    }
}

int main(void) {
    SPIController spi_ctrl;
    uint8_t flash_data[256];
    uint8_t read_buf[256];
    bool success;
    uint32_t addr;

    printf("===== SPI Flash Protection Demo =====\n\n");

    printf("[1] Initializing SPI controller with descriptor regions...\n");
    spi_protect_init(&spi_ctrl);
    printf("    BIOS Region: 0x%08X - 0x%08X\n",
           spi_ctrl.descriptor.regions[SPI_DESC_REGION_BIOS].base,
           spi_ctrl.descriptor.regions[SPI_DESC_REGION_BIOS].limit);
    printf("    ME Region:   0x%08X - 0x%08X\n",
           spi_ctrl.descriptor.regions[SPI_DESC_REGION_ME].base,
           spi_ctrl.descriptor.regions[SPI_DESC_REGION_ME].limit);
    printf("    GBE Region:  0x%08X - 0x%08X\n",
           spi_ctrl.descriptor.regions[SPI_DESC_REGION_GBE].base,
           spi_ctrl.descriptor.regions[SPI_DESC_REGION_GBE].limit);

    printf("\n[2] Setting up PR0 (Protected Range 0): BIOS boot block\n");
    success = spi_set_protected_range(&spi_ctrl, 0,
                                       0x00000000, 0x0000FFFF,
                                       SPI_ACCESS_READ);
    printf("    PR0: base=0x00000000 limit=0x0000FFFF, read-only => %s\n",
           success ? "OK" : "FAILED");

    printf("\n[3] Setting up PR1: ME firmware region\n");
    success = spi_set_protected_range(&spi_ctrl, 1,
                                       0x00600000, 0x009FFFFF,
                                       SPI_ACCESS_READ);
    printf("    PR1: base=0x00600000 limit=0x009FFFFF, read-only => %s\n",
           success ? "OK" : "FAILED");

    printf("\n[4] Locking the flash configuration (FLOCKDN)...\n");
    success = spi_lock_config(&spi_ctrl, SPI_LOCK_FLOCKDN |
                                         SPI_LOCK_BLE |
                                         SPI_LOCK_SMM_BWP);
    printf("    FLOCKDN + BLE + SMM_BWP locked => %s\n",
           success ? "OK" : "FAILED");

    printf("\n[5] Attempting legitimate BIOS master read (should succeed)...\n");
    memset(read_buf, 0, sizeof(read_buf));
    success = spi_read_flash(&spi_ctrl, SPI_MASTER_BIOS,
                             0x00001000, read_buf, 64);
    printf("    BIOS master read at 0x00001000 (64 bytes) => %s\n",
           success ? "ALLOWED" : "DENIED");

    printf("\n[6] Attempting Host CPU write to BIOS region (should be DENIED)...\n");
    memset(flash_data, 0x41, sizeof(flash_data));
    success = spi_write_flash(&spi_ctrl, SPI_MASTER_HOST,
                              0x00000100, flash_data, 64);
    printf("    Host write to BIOS region at 0x00000100 => %s\n",
           success ? "ALLOWED" : "DENIED");

    printf("\n[7] Attempting attacker write via spi_attack_attempt...\n");
    memset(flash_data, 0xCC, sizeof(flash_data));
    success = spi_attack_attempt(&spi_ctrl, 0x00000200, flash_data, 32);
    printf("    Attack write to 0x00000200 => %s\n",
           success ? "SUCCEEDED (BAD!)" : "BLOCKED (GOOD)");

    printf("\n[8] Checking access permissions summary:\n");
    for (addr = 0x00000000; addr < 0x00100000; addr += 0x20000) {
        bool read_ok = spi_check_access(&spi_ctrl, SPI_MASTER_HOST,
                                        addr, 1, false);
        bool write_ok = spi_check_access(&spi_ctrl, SPI_MASTER_HOST,
                                         addr, 1, true);
        printf("    HOST @ 0x%08X: read=%s write=%s\n",
               addr, read_ok ? "Y" : "N", write_ok ? "Y" : "N");
    }

    printf("\n[9] Flash lockdown state:\n");
    printf("    FLOCKDN = %s\n", spi_ctrl.lock_state.flockdn ? "SET" : "CLEAR");
    printf("    BLE     = %s\n", spi_ctrl.lock_state.ble ? "SET" : "CLEAR");
    printf("    SMM_BWP = %s\n", spi_ctrl.lock_state.smm_bwp ? "SET" : "CLEAR");
    printf("    BIOS_WE = %s\n", spi_ctrl.lock_state.bios_we ? "SET" : "CLEAR");

    printf("\n===== Demo Complete =====\n");
    return 0;
}
