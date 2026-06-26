CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -I include
LDFLAGS = -lm
OUTDIR  = bin

SRCS_STAGE1   = src/stage1.c
SRCS_STAGE2   = src/stage2.c
SRCS_LINUX    = src/linux_boot.c
SRCS_MODULES  = src/grub_modules.c
SRCS_FS       = src/filesys_boot.c

COMMON_SRCS = $(SRCS_STAGE1) $(SRCS_STAGE2) $(SRCS_LINUX) $(SRCS_MODULES) $(SRCS_FS)

DEMO1 = $(OUTDIR)/mbr_boot_demo
DEMO2 = $(OUTDIR)/multiboot_demo
DEMO3 = $(OUTDIR)/linux_boot_demo

.PHONY: all clean

all: $(OUTDIR) $(DEMO1) $(DEMO2) $(DEMO3)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(DEMO1): examples/mbr_boot_demo.c $(COMMON_SRCS)
	$(CC) $(CFLAGS) -o $@ examples/mbr_boot_demo.c $(SRCS_STAGE1) $(SRCS_STAGE2) $(LDFLAGS)

$(DEMO2): examples/multiboot_demo.c $(COMMON_SRCS)
	$(CC) $(CFLAGS) -o $@ examples/multiboot_demo.c $(SRCS_STAGE1) $(SRCS_STAGE2) $(LDFLAGS)

$(DEMO3): examples/linux_boot_demo.c $(COMMON_SRCS)
	$(CC) $(CFLAGS) -o $@ examples/linux_boot_demo.c $(SRCS_LINUX) $(SRCS_STAGE1) $(SRCS_STAGE2) $(LDFLAGS)

clean:
	rm -rf $(OUTDIR)
	rm -f disk_mbr.bin
