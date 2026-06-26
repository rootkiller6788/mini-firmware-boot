CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -I include -std=c99
LDFLAGS = -lm
OUTDIR  = bin
SRCDIR  = src
EXAMPLESDIR = examples

SRCS   = $(SRCDIR)/device_tree.c $(SRCDIR)/acpi_tables.c $(SRCDIR)/acpi_aml.c \
         $(SRCDIR)/smbios.c $(SRCDIR)/hob.c
OBJS   = $(SRCS:.c=.o)
DEPS   = $(OBJS:.o=.d)

EXAMPLES = $(OUTDIR)/fdt_parse_demo.exe \
           $(OUTDIR)/acpi_tables_demo.exe \
           $(OUTDIR)/smbios_demo.exe

.PHONY: all clean run-fdt run-acpi run-smbios distclean help

all: $(OUTDIR) $(EXAMPLES)

$(OUTDIR):
	mkdir -p "$(OUTDIR)"

$(OUTDIR)/fdt_parse_demo.exe: $(EXAMPLESDIR)/fdt_parse_demo.c $(SRCDIR)/device_tree.o | $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ $< $(SRCDIR)/device_tree.o $(LDFLAGS)

$(OUTDIR)/acpi_tables_demo.exe: $(EXAMPLESDIR)/acpi_tables_demo.c $(SRCDIR)/acpi_tables.o | $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ $< $(SRCDIR)/acpi_tables.o $(LDFLAGS)

$(OUTDIR)/smbios_demo.exe: $(EXAMPLESDIR)/smbios_demo.c $(SRCDIR)/smbios.o | $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ $< $(SRCDIR)/smbios.o $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

run-fdt: $(OUTDIR)/fdt_parse_demo.exe
	"$(OUTDIR)/fdt_parse_demo.exe"

run-acpi: $(OUTDIR)/acpi_tables_demo.exe
	"$(OUTDIR)/acpi_tables_demo.exe"

run-smbios: $(OUTDIR)/smbios_demo.exe
	"$(OUTDIR)/smbios_demo.exe"

clean:
	rm -rf "$(OUTDIR)"
	rm -f "$(SRCDIR)"/*.o

distclean: clean
	rm -f *.log

help:
	@echo "Usage: make [target]"
	@echo "  all       - Build all demo programs (default)"
	@echo "  run-fdt   - Run FDT parse demo"
	@echo "  run-acpi  - Run ACPI tables demo"
	@echo "  run-smbios - Run SMBIOS demo"
	@echo "  clean     - Remove build artifacts"
	@echo "  distclean - Clean all generated files"
	@echo "  help      - This message"
