CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c99 -I include
LDFLAGS = -lm
OUTDIR  = bin

SRC     = src/firmware_layout.c src/reset_vector.c src/mmio.c \
          src/smbios_fw.c src/spi_nor.c

TARGETS = $(OUTDIR)/flash_boot_demo $(OUTDIR)/mmio_demo \
          $(OUTDIR)/smbios_demo

.PHONY: all clean test run-flash-boot run-mmio run-smbios

all: $(TARGETS)

$(OUTDIR)/flash_boot_demo: examples/flash_boot_demo.c $(SRC)
	-mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ examples/flash_boot_demo.c $(SRC) $(LDFLAGS)

$(OUTDIR)/mmio_demo: examples/mmio_demo.c $(SRC)
	-mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ examples/mmio_demo.c $(SRC) $(LDFLAGS)

$(OUTDIR)/smbios_demo: examples/smbios_demo.c $(SRC)
	-mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ examples/smbios_demo.c $(SRC) $(LDFLAGS)

run-flash-boot: $(OUTDIR)/flash_boot_demo
	$(OUTDIR)/flash_boot_demo

run-mmio: $(OUTDIR)/mmio_demo
	$(OUTDIR)/mmio_demo

run-smbios: $(OUTDIR)/smbios_demo
	$(OUTDIR)/smbios_demo

test: $(TARGETS)
	@echo ====================================
	@echo   Running flash_boot_demo...
	@echo ====================================
	$(OUTDIR)/flash_boot_demo
	@echo ""
	@echo ====================================
	@echo   Running mmio_demo...
	@echo ====================================
	$(OUTDIR)/mmio_demo
	@echo ""
	@echo ====================================
	@echo   Running smbios_demo...
	@echo ====================================
	$(OUTDIR)/smbios_demo
	@echo ""
	@echo ====================================
	@echo   All demos completed successfully.
	@echo ====================================

clean:
	-rm -rf $(OUTDIR)
