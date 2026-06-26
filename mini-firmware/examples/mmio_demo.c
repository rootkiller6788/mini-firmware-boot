#include "mmio.h"
#include <stdio.h>

int main(void)
{
    printf("=== mini-firmware: MMIO Demo ===\n\n");

    MMIOManager mgr;
    if (!mmio_init(&mgr)) {
        fprintf(stderr, "Failed to init MMIO manager\n");
        return 1;
    }

    printf("Step 1: Mapping simulated UART at 0x3F8 (COM1)\n");
    if (!mmio_map(&mgr, 0x3F8, 8, MMIO_DEV_UART, "UART0")) {
        fprintf(stderr, "Failed to map UART\n");
        return 1;
    }

    printf("Step 2: Mapping simulated Timer at 0x40 (PIT)\n");
    if (!mmio_map(&mgr, 0x40, 8, MMIO_DEV_TIMER, "PIT")) {
        fprintf(stderr, "Failed to map Timer\n");
        return 1;
    }

    printf("Step 3: Mapping simulated GPIO at 0x500\n");
    if (!mmio_map(&mgr, 0x500, 8, MMIO_DEV_GPIO, "GPIOA")) {
        fprintf(stderr, "Failed to map GPIO\n");
        return 1;
    }

    printf("  Mapped %u regions\n\n", mgr.num_regions);

    printf("Step 4: Writing to UART (TX)\n");
    mmio_write32(&mgr, 0x3F8, (uint32_t)'H');
    mmio_write32(&mgr, 0x3F8, (uint32_t)'e');
    mmio_write32(&mgr, 0x3F8, (uint32_t)'l');
    mmio_write32(&mgr, 0x3F8, (uint32_t)'l');
    mmio_write32(&mgr, 0x3F8, (uint32_t)'o');

    printf("\nStep 5: Configuring Timer\n");
    mmio_write32(&mgr, 0x40, 0);
    mmio_write32(&mgr, 0x44, 1000);

    uint32_t timer_val = mmio_read32(&mgr, 0x40);
    uint32_t period_val = mmio_read32(&mgr, 0x44);
    printf("  Timer counter: %u, period: %u\n", timer_val, period_val);

    printf("\nStep 6: Controlling GPIO\n");
    mmio_write32(&mgr, 0x504, 0x000000FF);
    mmio_write32(&mgr, 0x500, 0x00000055);

    uint32_t gpio_out = mmio_read32(&mgr, 0x500);
    uint32_t gpio_dir = mmio_read32(&mgr, 0x504);
    printf("  GPIO output: 0x%08X, direction: 0x%08X\n", gpio_out, gpio_dir);

    printf("\nStep 7: Byte-level access\n");
    mmio_write8(&mgr, 0x3F8, (uint8_t)'X');
    uint8_t byte_val = mmio_read8(&mgr, 0x3F8);
    printf("  Read back byte from UART: '%c' (0x%02X)\n", byte_val, byte_val);

    printf("\n=== MMIO Demo Complete ===\n");
    return 0;
}
