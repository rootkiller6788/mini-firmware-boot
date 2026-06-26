#include "device_enum.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    printf("============================================================\n");
    printf("  mini-boot-process: PCI Device Enumeration Demo\n");
    printf("  Simulated PCI Bus — Enumeration, Resource Assignment\n");
    printf("============================================================\n\n");

    PCIBus bus;
    memset(&bus, 0, sizeof(bus));

    printf("--- Step 1: Enumerate PCI Bus 0 ---\n");
    pci_enumerate_bus(&bus, 0);
    printf("Found %u devices on bus 0.\n\n", bus.device_count);

    printf("--- Step 2: Print Raw Device List ---\n");
    pci_print_devices(&bus);

    printf("\n--- Step 3: Assign Resources (BARs) ---\n");
    pci_assign_resources(&bus, 0xD0000000, 0x1000);

    printf("\n--- Step 4: Enable Bus Mastering ---\n");
    pci_enable_bus_mastering(&bus);

    printf("\n--- Step 5: Device Lookup by Vendor/Device ID ---\n");
    PCIDevice found;
    if (pci_find_device(&bus, 0x8086, 0x5912, &found)) {
        printf("[LOOKUP] Found device %04X:%04X at %02X:%02X.%d (class=%06X — %s)\n",
               found.info.vendor, found.info.device,
               found.bus, found.device, found.function,
               found.info.class_code,
               pci_class_name(found.info.class_code));
    } else {
        printf("[LOOKUP] Device 8086:5912 not found.\n");
    }

    if (pci_find_device(&bus, 0x8086, 0x15B7, &found)) {
        printf("[LOOKUP] Found device %04X:%04X at %02X:%02X.%d (class=%06X — %s)\n",
               found.info.vendor, found.info.device,
               found.bus, found.device, found.function,
               found.info.class_code,
               pci_class_name(found.info.class_code));
    }

    printf("\n--- Step 6: Device Lookup by Class Code ---\n");
    uint32_t class_codes[] = {
        PCI_CLASS_VGA,
        PCI_CLASS_STORAGE,
        PCI_CLASS_NETWORK,
        PCI_CLASS_USB,
        PCI_CLASS_BRIDGE,
        PCI_CLASS_HOST
    };

    for (int c = 0; c < 6; c++) {
        PCIDevice results[8];
        uint32_t found_count = 0;

        pci_find_class(&bus, class_codes[c], results, 8, &found_count);
        printf("[CLASS] Class 0x%06X (%s): %u device(s)\n",
               class_codes[c],
               pci_class_name(class_codes[c]),
               found_count);

        for (uint32_t d = 0; d < found_count; d++) {
            printf("  %02X:%02X.%d : %04X:%04X (rev %02X)\n",
                   results[d].bus, results[d].device, results[d].function,
                   results[d].info.vendor, results[d].info.device,
                   results[d].info.revision);
        }
    }

    printf("\n--- Step 7: Final Device List with BAR Assignments ---\n");
    pci_print_devices(&bus);

    printf("\n============================================================\n");
    printf("  PCI Enumeration Demo Complete!\n");
    printf("============================================================\n");

    return 0;
}
