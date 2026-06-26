CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c99 -I include
LDFLAGS = -lm
OUTDIR  = bin

SRC     = src/firmware_layout.c src/reset_vector.c src/mmio.c \
          src/smbios_fw.c src/spi_nor.c src/bootblock.c \
          src/acpi_fw.c src/fit_image.c src/meminit.c \
          src/psci_fw.c src/firmware_update.c

DEMOS   = $(OUTDIR)/flash_boot_demo $(OUTDIR)/mmio_demo \
          $(OUTDIR)/smbios_demo

TESTS   = $(OUTDIR)/test_firmware

.PHONY: all clean test run-flash-boot run-mmio run-smbios

all: $(DEMOS) $(TESTS)

$(OUTDIR)/flash_boot_demo: examples/flash_boot_demo.c $(SRC)
	-mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ examples/flash_boot_demo.c $(SRC) $(LDFLAGS)

$(OUTDIR)/mmio_demo: examples/mmio_demo.c $(SRC)
	-mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ examples/mmio_demo.c $(SRC) $(LDFLAGS)

$(OUTDIR)/smbios_demo: examples/smbios_demo.c $(SRC)
	-mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ examples/smbios_demo.c $(SRC) $(LDFLAGS)

$(OUTDIR)/test_firmware: tests/test_firmware.c $(SRC)
	-mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ tests/test_firmware.c $(SRC) $(LDFLAGS)

run-flash-boot: $(OUTDIR)/flash_boot_demo
	$(OUTDIR)/flash_boot_demo

run-mmio: $(OUTDIR)/mmio_demo
	$(OUTDIR)/mmio_demo

run-smbios: $(OUTDIR)/smbios_demo
	$(OUTDIR)/smbios_demo

test: $(DEMOS) $(TESTS)
	@echo ==============================================
	@echo   Running Test Suite...
	@echo ==============================================
	$(OUTDIR)/test_firmware
	@echo ""
	@echo ==============================================
	@echo   Running Demos...
	@echo ==============================================
	$(OUTDIR)/flash_boot_demo
	@echo ""
	$(OUTDIR)/mmio_demo
	@echo ""
	$(OUTDIR)/smbios_demo
	@echo ""
	@echo ==============================================
	@echo   All tests and demos completed.
	@echo ==============================================

clean:
	-rm -rf $(OUTDIR)
