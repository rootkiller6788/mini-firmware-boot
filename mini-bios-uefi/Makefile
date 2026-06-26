CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -std=c99 -I include -D_CRT_SECURE_NO_WARNINGS
LDFLAGS :=
OUTDIR  := bin
SRCDIR  := src
EXAMPLEDIR := examples
TESTDIR := tests

SRCS    := $(SRCDIR)/legacy_bios.c $(SRCDIR)/uefi_boot.c $(SRCDIR)/uefi_protocols.c \
           $(SRCDIR)/pe_coff.c $(SRCDIR)/gpt.c \
           $(SRCDIR)/bios_hardware.c $(SRCDIR)/uefi_image_auth.c $(SRCDIR)/uefi_acpi.c
OBJS    := $(SRCS:$(SRCDIR)/%.c=$(OUTDIR)/%.o)

DEMOS   := $(OUTDIR)/bios_post_demo.exe $(OUTDIR)/uefi_boot_demo.exe $(OUTDIR)/gpt_demo.exe
TEST    := $(OUTDIR)/test_all.exe

.PHONY: all clean bios_demo uefi_demo gpt_demo test

all: $(DEMOS)

test: $(TEST)
	./$(TEST)

$(OUTDIR):
	mkdir -p "$(OUTDIR)"

$(OUTDIR)/legacy_bios.o:       $(SRCDIR)/legacy_bios.c       include/legacy_bios.h       | $(OUTDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/uefi_boot.o:         $(SRCDIR)/uefi_boot.c         include/uefi_boot.h         | $(OUTDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/uefi_protocols.o:    $(SRCDIR)/uefi_protocols.c    include/uefi_protocols.h    include/uefi_boot.h | $(OUTDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/pe_coff.o:           $(SRCDIR)/pe_coff.c           include/pe_coff.h           | $(OUTDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/gpt.o:               $(SRCDIR)/gpt.c               include/gpt.h               | $(OUTDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/bios_hardware.o:     $(SRCDIR)/bios_hardware.c     include/bios_hardware.h     | $(OUTDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/uefi_image_auth.o:   $(SRCDIR)/uefi_image_auth.c   include/uefi_image_auth.h   | $(OUTDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/uefi_acpi.o:         $(SRCDIR)/uefi_acpi.c         include/uefi_acpi.h         | $(OUTDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/bios_post_demo.exe: $(OUTDIR)/legacy_bios.o $(EXAMPLEDIR)/bios_post_demo.c | $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ $(EXAMPLEDIR)/bios_post_demo.c $(OUTDIR)/legacy_bios.o $(LDFLAGS)

$(OUTDIR)/uefi_boot_demo.exe: $(OUTDIR)/uefi_boot.o $(OUTDIR)/uefi_protocols.o $(OUTDIR)/pe_coff.o $(EXAMPLEDIR)/uefi_boot_demo.c | $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ $(EXAMPLEDIR)/uefi_boot_demo.c $(OUTDIR)/uefi_boot.o $(OUTDIR)/uefi_protocols.o $(OUTDIR)/pe_coff.o $(LDFLAGS)

$(OUTDIR)/gpt_demo.exe: $(OUTDIR)/gpt.o $(EXAMPLEDIR)/gpt_demo.c | $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ $(EXAMPLEDIR)/gpt_demo.c $(OUTDIR)/gpt.o $(LDFLAGS)

$(OUTDIR)/test_all.exe: $(OBJS) $(TESTDIR)/test_all.c | $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ $(TESTDIR)/test_all.c $(OBJS) $(LDFLAGS)

bios_demo: $(OUTDIR)/bios_post_demo.exe

uefi_demo: $(OUTDIR)/uefi_boot_demo.exe

gpt_demo: $(OUTDIR)/gpt_demo.exe

clean:
	rm -rf "$(OUTDIR)"
