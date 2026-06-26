CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -I include -std=c99
LDFLAGS  = -lm
OUTDIR   = bin

SRC_DIR  = src
EXAM_DIR = examples
TEST_DIR = tests

SRCS = src/boot_phases.c \
       src/cpu_init.c \
       src/memory_init.c \
       src/device_enum.c \
       src/cache_as_ram.c \
       src/acpi_tables.c \
       src/smbios.c \
       src/firmware_volume.c \
       src/boot_policy.c

OBJS = src/boot_phases.o \
       src/cpu_init.o \
       src/memory_init.o \
       src/device_enum.o \
       src/cache_as_ram.o \
       src/acpi_tables.o \
       src/smbios.o \
       src/firmware_volume.o \
       src/boot_policy.o

BOOT_SIM_EXE = $(OUTDIR)/boot_sim_demo.exe
PCI_ENUM_EXE = $(OUTDIR)/pci_enum_demo.exe
CAR_DEMO_EXE = $(OUTDIR)/car_demo.exe
TEST_EXE     = $(OUTDIR)/test_all.exe

.PHONY: all clean boot_sim pci_enum car_demo test

all: boot_sim pci_enum car_demo

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(BOOT_SIM_EXE): $(OBJS) examples/boot_sim_demo.o | $(OUTDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(PCI_ENUM_EXE): $(OBJS) examples/pci_enum_demo.o | $(OUTDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(CAR_DEMO_EXE): $(OBJS) examples/car_demo.o | $(OUTDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(TEST_EXE): $(OBJS) tests/test_all.o | $(OUTDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

examples/%.o: examples/%.c
	$(CC) $(CFLAGS) -c $< -o $@

tests/%.o: tests/%.c
	$(CC) $(CFLAGS) -c $< -o $@

boot_sim: $(BOOT_SIM_EXE)

pci_enum: $(PCI_ENUM_EXE)

car_demo: $(CAR_DEMO_EXE)

test: $(TEST_EXE)
	./$(TEST_EXE)

clean:
	rm -rf $(OUTDIR) src/*.o examples/*.o tests/*.o
