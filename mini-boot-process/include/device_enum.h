#ifndef DEVICE_ENUM_H
#define DEVICE_ENUM_H

#include <stdbool.h>
#include <stdint.h>

#define PCI_MAX_BUS         256
#define PCI_MAX_DEVICE      32
#define PCI_MAX_FUNCTION    8
#define PCI_MAX_DEVICES     256
#define PCI_MAX_BAR         6

#define PCI_CONFIG_ADDR     0x0CF8
#define PCI_CONFIG_DATA     0x0CFC
#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_COMMAND         0x04
#define PCI_STATUS          0x06
#define PCI_REVISION_ID     0x08
#define PCI_CLASS_CODE      0x0B
#define PCI_HEADER_TYPE     0x0E
#define PCI_BAR0            0x10
#define PCI_BAR1            0x14
#define PCI_BAR2            0x18
#define PCI_BAR3            0x1C
#define PCI_BAR4            0x20
#define PCI_BAR5            0x24
#define PCI_SUBSYS_VENDOR   0x2C
#define PCI_SUBSYS_ID       0x2E
#define PCI_CAP_PTR         0x34
#define PCI_INT_LINE        0x3C
#define PCI_INT_PIN         0x3D

#define PCI_CLASS_VGA       0x0300
#define PCI_CLASS_STORAGE   0x0106
#define PCI_CLASS_NETWORK   0x0200
#define PCI_CLASS_USB       0x0C03
#define PCI_CLASS_BRIDGE    0x0604
#define PCI_CLASS_HOST      0x0600

#define PCI_CMD_IO          0x0001
#define PCI_CMD_MEM         0x0002
#define PCI_CMD_BUSMASTER   0x0004
#define PCI_CMD_INVALIDATE  0x0010
#define PCI_CMD_PERR        0x0040
#define PCI_CMD_SERR        0x0100

#define PCI_BAR_IO          0x00000001
#define PCI_BAR_MEM         0x00000000
#define PCI_BAR_PREFETCH    0x00000008
#define PCI_BAR_64BIT       0x00000004

typedef struct {
    uint16_t vendor;
    uint16_t device;
    uint8_t  revision;
    uint32_t class_code;
    uint8_t  header_type;
    uint16_t subsystem_vendor;
    uint16_t subsystem_id;
    uint8_t  interrupt_line;
    uint8_t  interrupt_pin;
} PCIDeviceInfo;

typedef struct {
    uint8_t       bus;
    uint8_t       device;
    uint8_t       function;
    PCIDeviceInfo info;
    uint64_t      bar[PCI_MAX_BAR];
    uint32_t      bar_size[PCI_MAX_BAR];
    bool          bar_io[PCI_MAX_BAR];
    bool          present;
} PCIDevice;

typedef struct {
    uint8_t   bus_number;
    PCIDevice devices[PCI_MAX_DEVICES];
    uint32_t  device_count;
} PCIBus;

uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint8_t  pci_config_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void     pci_config_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);

void pci_enumerate_bus(PCIBus *bus_data, uint8_t bus_number);
bool pci_find_device(const PCIBus *bus_data, uint16_t vendor, uint16_t device, PCIDevice *result);
bool pci_find_class(const PCIBus *bus_data, uint32_t class_code, PCIDevice *results, uint32_t max_results, uint32_t *found);
void pci_assign_resources(PCIBus *bus_data, uint64_t mmio_base, uint64_t io_base);
void pci_enable_bus_mastering(PCIBus *bus_data);
void pci_print_devices(const PCIBus *bus_data);
const char *pci_class_name(uint32_t class_code);

#endif
