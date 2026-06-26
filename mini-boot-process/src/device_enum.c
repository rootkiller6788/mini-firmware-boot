#include "device_enum.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint16_t vendor;
    uint16_t device;
    uint32_t class_code;
    uint8_t  header_type;
} SimPCIDevice;

static uint32_t g_next_mmio = 0xD0000000;
static uint16_t g_next_io = 0x1000;

uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    (void)bus;
    (void)device;
    (void)function;
    (void)offset;
    return 0xFFFFFFFF;
}

uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    (void)bus;
    (void)device;
    (void)function;
    (void)offset;
    return 0xFFFF;
}

uint8_t pci_config_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    (void)bus;
    (void)device;
    (void)function;
    (void)offset;
    return 0xFF;
}

void pci_config_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value)
{
    (void)bus;
    (void)device;
    (void)function;
    (void)offset;
    (void)value;
}

void pci_enumerate_bus(PCIBus *bus_data, uint8_t bus_number)
{
    if (!bus_data) return;

    memset(bus_data, 0, sizeof(PCIBus));
    bus_data->bus_number = bus_number;

    printf("[PCI] Enumerating bus %d...\n", bus_number);
    printf("[PCI] Scanning 32 devices x 8 functions...\n");

    SimPCIDevice simulated[] = {
        {0x8086, 0x5912, PCI_CLASS_VGA,      0x00},
        {0x8086, 0xA102, PCI_CLASS_STORAGE,  0x00},
        {0x8086, 0xA103, PCI_CLASS_STORAGE,  0x00},
        {0x8086, 0xA12F, PCI_CLASS_USB,      0x00},
        {0x8086, 0x15B7, PCI_CLASS_NETWORK,  0x00},
        {0x8086, 0x5904, PCI_CLASS_HOST,     0x00},
        {0x8086, 0x1901, PCI_CLASS_BRIDGE,   0x01},
    };
    uint32_t sim_count = sizeof(simulated) / sizeof(simulated[0]);

    for (uint32_t i = 0; i < sim_count && bus_data->device_count < PCI_MAX_DEVICES; i++) {
        PCIDevice *dev = &bus_data->devices[bus_data->device_count];
        dev->bus = bus_number;
        dev->device = (uint8_t)(i * 4);
        dev->function = 0;
        dev->info.vendor = simulated[i].vendor;
        dev->info.device = simulated[i].device;
        dev->info.class_code = simulated[i].class_code;
        dev->info.header_type = simulated[i].header_type;
        dev->info.revision = 0x01;
        dev->info.subsystem_vendor = 0x8086;
        dev->info.subsystem_id = 0x0000;
        dev->info.interrupt_line = 0;
        dev->info.interrupt_pin = (uint8_t)((i % 4) + 1);
        dev->present = true;

        for (int bar_idx = 0; bar_idx < PCI_MAX_BAR; bar_idx++) {
            dev->bar[bar_idx] = 0;
            dev->bar_size[bar_idx] = 0;
            dev->bar_io[bar_idx] = false;
        }

        bus_data->device_count++;
        printf("[PCI]   %02X:%02X.%d Vendor=%04X Device=%04X Class=%06X\n",
               bus_number, dev->device, dev->function,
               dev->info.vendor, dev->info.device, dev->info.class_code);
    }

    printf("[PCI] Enumeration complete. %u devices found on bus %d.\n",
           bus_data->device_count, bus_number);
}

bool pci_find_device(const PCIBus *bus_data, uint16_t vendor, uint16_t device, PCIDevice *result)
{
    if (!bus_data || !result) return false;

    for (uint32_t i = 0; i < bus_data->device_count; i++) {
        const PCIDevice *dev = &bus_data->devices[i];
        if (dev->present
            && dev->info.vendor == vendor
            && dev->info.device == device) {
            *result = *dev;
            return true;
        }
    }
    return false;
}

bool pci_find_class(const PCIBus *bus_data, uint32_t class_code, PCIDevice *results,
                    uint32_t max_results, uint32_t *found)
{
    if (!bus_data || !results || !found) return false;

    *found = 0;
    for (uint32_t i = 0; i < bus_data->device_count && *found < max_results; i++) {
        const PCIDevice *dev = &bus_data->devices[i];
        if (dev->present && dev->info.class_code == class_code) {
            results[*found] = *dev;
            (*found)++;
        }
    }
    return true;
}

void pci_assign_resources(PCIBus *bus_data, uint64_t mmio_base, uint64_t io_base)
{
    if (!bus_data) return;

    printf("[PCI:BAR] Assigning resources for bus %d...\n", bus_data->bus_number);
    printf("[PCI:BAR] MMIO base: 0x%08llX, IO base: 0x%04llX\n",
           (unsigned long long)mmio_base, (unsigned long long)io_base);

    (void)mmio_base;
    (void)io_base;

    uint64_t current_mmio = g_next_mmio;
    uint16_t current_io = g_next_io;

    for (uint32_t i = 0; i < bus_data->device_count; i++) {
        PCIDevice *dev = &bus_data->devices[i];
        if (!dev->present) continue;

        uint32_t num_bars = 1;
        uint32_t bar_size = 0x1000;

        switch (dev->info.class_code) {
        case PCI_CLASS_VGA:
            num_bars = 2;
            dev->bar[0] = current_mmio;
            dev->bar_size[0] = 0x1000000;
            dev->bar_io[0] = false;
            current_mmio += 0x1000000;
            dev->bar[1] = current_mmio;
            dev->bar_size[1] = 0x1000;
            dev->bar_io[1] = false;
            current_mmio += 0x1000;
            break;
        case PCI_CLASS_STORAGE:
            num_bars = 2;
            bar_size = 0x1000;
            for (uint32_t b = 0; b < num_bars; b++) {
                dev->bar[b] = current_mmio;
                dev->bar_size[b] = bar_size;
                dev->bar_io[b] = false;
                current_mmio += bar_size;
            }
            break;
        case PCI_CLASS_USB:
            num_bars = 1;
            dev->bar[0] = current_mmio;
            dev->bar_size[0] = 0x10000;
            dev->bar_io[0] = false;
            current_mmio += 0x10000;
            break;
        case PCI_CLASS_NETWORK:
            num_bars = 1;
            dev->bar[0] = current_mmio;
            dev->bar_size[0] = 0x10000;
            dev->bar_io[0] = false;
            current_mmio += 0x10000;
            break;
        case PCI_CLASS_BRIDGE:
        case PCI_CLASS_HOST:
            num_bars = 1;
            bar_size = 0x1000;
            for (uint32_t b = 0; b < num_bars; b++) {
                dev->bar[b] = current_mmio;
                dev->bar_size[b] = bar_size;
                dev->bar_io[b] = false;
                current_mmio += bar_size;
            }
            break;
        default:
            num_bars = 1;
            dev->bar[0] = current_mmio;
            dev->bar_size[0] = 0x1000;
            dev->bar_io[0] = false;
            current_mmio += 0x1000;
            break;
        }

        if (num_bars >= 2 && dev->info.class_code != PCI_CLASS_VGA) {
            dev->bar[1] = current_io;
            dev->bar_size[1] = 16;
            dev->bar_io[1] = true;
            current_io += 16;
        }

        printf("[PCI:BAR]   %02X:%02X.%d: BAR0=0x%08llX (size=%u %s)\n",
               dev->bus, dev->device, dev->function,
               (unsigned long long)dev->bar[0], dev->bar_size[0],
               dev->bar_io[0] ? "IO" : "MMIO");

        if (num_bars > 1 && dev->bar_size[1] > 0) {
            for (uint32_t b = 1; b < num_bars && b < PCI_MAX_BAR; b++) {
                printf("[PCI:BAR]   %02X:%02X.%d: BAR%u=0x%08llX (size=%u %s)\n",
                       dev->bus, dev->device, dev->function, b,
                       (unsigned long long)dev->bar[b], dev->bar_size[b],
                       dev->bar_io[b] ? "IO" : "MMIO");
            }
        }
    }

    g_next_mmio = current_mmio;
    g_next_io = current_io;
    printf("[PCI:BAR] Resource assignment complete.\n");
}

void pci_enable_bus_mastering(PCIBus *bus_data)
{
    if (!bus_data) return;

    printf("[PCI:CMD] Enabling bus mastering for all devices...\n");
    for (uint32_t i = 0; i < bus_data->device_count; i++) {
        PCIDevice *dev = &bus_data->devices[i];
        if (!dev->present) continue;

        printf("[PCI:CMD]   %02X:%02X.%d: CMD |= BUSMASTER (%04X)\n",
               dev->bus, dev->device, dev->function,
               PCI_CMD_IO | PCI_CMD_MEM | PCI_CMD_BUSMASTER);
    }
}

void pci_print_devices(const PCIBus *bus_data)
{
    if (!bus_data) return;

    printf("\n   ===== PCI BUS %d DEVICES =====\n", bus_data->bus_number);
    printf("   %-10s %-12s %-12s %-10s %s\n",
           "Location", "Vendor", "Device", "Rev", "Class");
    printf("   %-10s %-12s %-12s %-10s %s\n",
           "----------", "------------", "------------", "----------", "-----------");

    for (uint32_t i = 0; i < bus_data->device_count; i++) {
        const PCIDevice *dev = &bus_data->devices[i];
        if (!dev->present) continue;

        char loc[16];
        snprintf(loc, sizeof(loc), "%02X:%02X.%d",
                 dev->bus, dev->device, dev->function);

        printf("   %-10s %04X:%04X     %04X:%04X     %02X        %s\n",
               loc,
               dev->info.vendor >> 8, dev->info.vendor & 0xFF,
               dev->info.device >> 8, dev->info.device & 0xFF,
               dev->info.revision,
               pci_class_name(dev->info.class_code));

        for (int b = 0; b < PCI_MAX_BAR; b++) {
            if (dev->bar_size[b] > 0) {
                printf("     BAR%d: 0x%08llX (size=%u %s)\n",
                       b, (unsigned long long)dev->bar[b],
                       dev->bar_size[b],
                       dev->bar_io[b] ? "IO" : "MMIO");
            }
        }

        if (dev->info.interrupt_pin) {
            printf("     INT: pin=%c, line=%d\n",
                   'A' + dev->info.interrupt_pin - 1,
                   dev->info.interrupt_line);
        }
    }
    printf("   =================================\n");
}

const char *pci_class_name(uint32_t class_code)
{
    switch (class_code) {
    case PCI_CLASS_VGA:     return "VGA Controller";
    case PCI_CLASS_STORAGE: return "SATA/AHCI Controller";
    case PCI_CLASS_NETWORK: return "Ethernet Controller";
    case PCI_CLASS_USB:     return "USB xHCI Controller";
    case PCI_CLASS_BRIDGE:  return "PCIe Bridge";
    case PCI_CLASS_HOST:    return "Host Bridge";
    default:                return "Unknown Device";
    }
}
