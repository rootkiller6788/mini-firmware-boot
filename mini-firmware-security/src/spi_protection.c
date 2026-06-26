#include "spi_protection.h"

#include <string.h>
#include <stdio.h>

static bool spi_is_region_access_allowed(SPIController *ctrl,
                                         uint8_t master_id,
                                         uint32_t address) {
    uint32_t i;

    for (i = 0; i < SPI_DESC_REGION_COUNT; i++) {
        SPIDescriptorRegion *region = &ctrl->descriptor.regions[i];
        if (address >= region->base && address <= region->limit) {
            uint8_t perm = region->permissions_per_master[master_id];
            return (perm != 0);
        }
    }
    return false;
}

static bool spi_is_prx_protected(SPIController *ctrl, uint32_t address,
                                 bool is_write) {
    uint32_t i;

    for (i = 0; i < SPI_MAX_PROTECTED_RANGES; i++) {
        SPIProtectedRange *pr = &ctrl->protected_ranges.ranges[i];
        if (address >= pr->base && address <= pr->limit) {
            if (is_write && pr->write_protect)
                return true;
            if (!is_write && pr->read_protect)
                return true;
            return false;
        }
    }
    return false;
}

static bool spi_is_locked_down(SPIController *ctrl) {
    return ctrl->lock_state.flockdn;
}

void spi_protect_init(SPIController *ctrl) {
    uint32_t i;

    if (ctrl == NULL)
        return;

    memset(ctrl, 0, sizeof(SPIController));

    for (i = 0; i < SPI_DESC_REGION_COUNT; i++) {
        ctrl->descriptor.regions[i].base = 0;
        ctrl->descriptor.regions[i].limit = 0;
        memset(ctrl->descriptor.regions[i].permissions_per_master,
               0, SPI_DESC_MASTER_COUNT);
    }

    for (i = 0; i < SPI_MAX_PROTECTED_RANGES; i++) {
        ctrl->protected_ranges.ranges[i].base = 0xFFFFFFFF;
        ctrl->protected_ranges.ranges[i].limit = 0;
        ctrl->protected_ranges.ranges[i].permissions = 0;
        ctrl->protected_ranges.ranges[i].write_protect = false;
        ctrl->protected_ranges.ranges[i].read_protect = false;
    }

    ctrl->descriptor.regions[SPI_DESC_REGION_BIOS].base = 0x00000000;
    ctrl->descriptor.regions[SPI_DESC_REGION_BIOS].limit = 0x005FFFFF;
    ctrl->descriptor.regions[SPI_DESC_REGION_BIOS].permissions_per_master[SPI_MASTER_BIOS] = SPI_ACCESS_READ_WRITE;
    ctrl->descriptor.regions[SPI_DESC_REGION_BIOS].permissions_per_master[SPI_MASTER_HOST] = SPI_ACCESS_READ;

    ctrl->descriptor.regions[SPI_DESC_REGION_ME].base = 0x00600000;
    ctrl->descriptor.regions[SPI_DESC_REGION_ME].limit = 0x009FFFFF;
    ctrl->descriptor.regions[SPI_DESC_REGION_ME].permissions_per_master[SPI_MASTER_ME] = SPI_ACCESS_READ_WRITE;
    ctrl->descriptor.regions[SPI_DESC_REGION_ME].permissions_per_master[SPI_MASTER_HOST] = SPI_ACCESS_READ;

    ctrl->descriptor.regions[SPI_DESC_REGION_GBE].base = 0x00A00000;
    ctrl->descriptor.regions[SPI_DESC_REGION_GBE].limit = 0x00BFFFFF;
    ctrl->descriptor.regions[SPI_DESC_REGION_GBE].permissions_per_master[SPI_MASTER_GBE] = SPI_ACCESS_READ_WRITE;
    ctrl->descriptor.regions[SPI_DESC_REGION_GBE].permissions_per_master[SPI_MASTER_HOST] = SPI_ACCESS_READ;

    ctrl->lock_state.bios_we = true;
    ctrl->lock_state.smm_bwp = false;
    ctrl->lock_state.ble = false;
    ctrl->lock_state.flockdn = false;

    ctrl->initialized = true;
}

bool spi_set_protected_range(SPIController *ctrl, uint8_t range_idx,
                             uint32_t base, uint32_t limit,
                             uint8_t permissions) {
    if (ctrl == NULL || !ctrl->initialized)
        return false;

    if (range_idx >= SPI_MAX_PROTECTED_RANGES)
        return false;

    if (base > limit || limit >= SPI_FLASH_SIZE)
        return false;

    if (spi_is_locked_down(ctrl))
        return false;

    ctrl->protected_ranges.ranges[range_idx].base = base;
    ctrl->protected_ranges.ranges[range_idx].limit = limit;
    ctrl->protected_ranges.ranges[range_idx].permissions = permissions;
    ctrl->protected_ranges.ranges[range_idx].write_protect =
        ((permissions & SPI_ACCESS_WRITE) == 0);
    ctrl->protected_ranges.ranges[range_idx].read_protect =
        ((permissions & SPI_ACCESS_READ) == 0);

    return true;
}

bool spi_lock_config(SPIController *ctrl, uint32_t lock_flags) {
    if (ctrl == NULL || !ctrl->initialized)
        return false;

    if (spi_is_locked_down(ctrl))
        return false;

    if (lock_flags & SPI_LOCK_BIOS_WE)
        ctrl->lock_state.bios_we = false;
    if (lock_flags & SPI_LOCK_SMM_BWP)
        ctrl->lock_state.smm_bwp = true;
    if (lock_flags & SPI_LOCK_BLE)
        ctrl->lock_state.ble = true;
    if (lock_flags & SPI_LOCK_FLOCKDN) {
        ctrl->lock_state.flockdn = true;
    }

    return true;
}

bool spi_check_access(SPIController *ctrl, uint8_t master_id,
                      uint32_t address, size_t length,
                      bool is_write) {
    uint32_t i;

    if (ctrl == NULL || !ctrl->initialized)
        return false;

    if (address + length > SPI_FLASH_SIZE)
        return false;

    for (i = 0; i < length; i++) {
        uint32_t current_addr = address + (uint32_t)i;

        if (!spi_is_region_access_allowed(ctrl, master_id, current_addr))
            return false;

        if (spi_is_prx_protected(ctrl, current_addr, is_write))
            return false;

        if (master_id == SPI_MASTER_HOST &&
            is_write && !ctrl->lock_state.bios_we)
            return false;
    }

    return true;
}

bool spi_attack_attempt(SPIController *ctrl, uint32_t address,
                        const uint8_t *data, size_t length) {
    if (ctrl == NULL || data == NULL)
        return false;

    return spi_write_flash(ctrl, SPI_MASTER_HOST, address, data, length);
}

bool spi_read_flash(SPIController *ctrl, uint8_t master_id,
                    uint32_t address, uint8_t *buf, size_t length) {
    if (ctrl == NULL || buf == NULL || !ctrl->initialized)
        return false;

    if (!spi_check_access(ctrl, master_id, address, length, false))
        return false;

    memcpy(buf, &ctrl->flash_memory[address], length);
    return true;
}

bool spi_write_flash(SPIController *ctrl, uint8_t master_id,
                     uint32_t address, const uint8_t *buf, size_t length) {
    if (ctrl == NULL || buf == NULL || !ctrl->initialized)
        return false;

    if (!spi_check_access(ctrl, master_id, address, length, true))
        return false;

    memcpy(&ctrl->flash_memory[address], buf, length);
    return true;
}
