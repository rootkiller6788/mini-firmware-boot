#ifndef SPI_PROTECTION_H
#define SPI_PROTECTION_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define SPI_MAX_PROTECTED_RANGES   5
#define SPI_FLASH_SIZE             0x1000000
#define SPI_SECTOR_SIZE            0x1000
#define SPI_REG_BIOS_CNTL          0xDC
#define SPI_REG_HSFS               0x04
#define SPI_REG_PR_BASE            0x74
#define SPI_REG_FLOCKDN            0x04

#define SPI_MASTER_BIOS            0
#define SPI_MASTER_ME              1
#define SPI_MASTER_GBE             2
#define SPI_MASTER_HOST            3

#define SPI_ACCESS_READ            0x01
#define SPI_ACCESS_WRITE           0x02
#define SPI_ACCESS_READ_WRITE      0x03

#define SPI_LOCK_BIOS_WE           (1 << 0)
#define SPI_LOCK_SMM_BWP           (1 << 1)
#define SPI_LOCK_BLE               (1 << 2)
#define SPI_LOCK_FLOCKDN           (1 << 3)

#define SPI_DESC_REGION_BIOS       0
#define SPI_DESC_REGION_ME         1
#define SPI_DESC_REGION_GBE        2
#define SPI_DESC_REGION_PDR        3
#define SPI_DESC_REGION_DEVEXP     4

#define SPI_DESC_MASTER_COUNT      4
#define SPI_DESC_REGION_COUNT      5

typedef struct {
    uint32_t base;
    uint32_t limit;
    uint8_t  permissions;
    bool     write_protect;
    bool     read_protect;
} SPIProtectedRange;

typedef struct {
    SPIProtectedRange ranges[SPI_MAX_PROTECTED_RANGES];
} SPIProtectedRanges;

typedef struct {
    uint32_t base;
    uint32_t limit;
    uint8_t  permissions_per_master[SPI_DESC_MASTER_COUNT];
} SPIDescriptorRegion;

typedef struct {
    SPIDescriptorRegion regions[SPI_DESC_REGION_COUNT];
    uint8_t             master_read[SPI_DESC_MASTER_COUNT];
    uint8_t             master_write[SPI_DESC_MASTER_COUNT];
} FlashDescriptor;

typedef struct {
    bool bios_we;
    bool smm_bwp;
    bool ble;
    bool flockdn;
} SPILock;

typedef struct {
    uint8_t           flash_memory[SPI_FLASH_SIZE];
    FlashDescriptor   descriptor;
    SPIProtectedRanges protected_ranges;
    SPILock           lock_state;
    bool              initialized;
} SPIController;

void spi_protect_init(SPIController *ctrl);
bool spi_set_protected_range(SPIController *ctrl, uint8_t range_idx,
                             uint32_t base, uint32_t limit,
                             uint8_t permissions);
bool spi_lock_config(SPIController *ctrl, uint32_t lock_flags);
bool spi_check_access(SPIController *ctrl, uint8_t master_id,
                      uint32_t address, size_t length,
                      bool is_write);
bool spi_attack_attempt(SPIController *ctrl, uint32_t address,
                        const uint8_t *data, size_t length);
bool spi_read_flash(SPIController *ctrl, uint8_t master_id,
                    uint32_t address, uint8_t *buf, size_t length);
bool spi_write_flash(SPIController *ctrl, uint8_t master_id,
                     uint32_t address, const uint8_t *buf, size_t length);

#endif
