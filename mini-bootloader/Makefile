CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -I include
LDFLAGS = -lm
OUTDIR  = bin

SRCS_STAGE1   = src/stage1.c
SRCS_STAGE2   = src/stage2.c
SRCS_LINUX    = src/linux_boot.c
SRCS_MODULES  = src/grub_modules.c
SRCS_FS       = src/filesys_boot.c
SRCS_ELF      = src/boot_elf.c
SRCS_COMPRESS = src/boot_compress.c
SRCS_MEMORY   = src/boot_memory.c
SRCS_CHAIN    = src/boot_chain.c
SRCS_CONFIG   = src/boot_config.c
SRCS_SECURE   = src/boot_secure.c

COMMON_SRCS = $(SRCS_STAGE1) $(SRCS_STAGE2) $(SRCS_LINUX) $(SRCS_MODULES) $(SRCS_FS)
ALL_SRCS    = $(COMMON_SRCS) $(SRCS_ELF) $(SRCS_COMPRESS) $(SRCS_MEMORY) \
              $(SRCS_CHAIN) $(SRCS_CONFIG) $(SRCS_SECURE)

DEMO1 = $(OUTDIR)/mbr_boot_demo
DEMO2 = $(OUTDIR)/multiboot_demo
DEMO3 = $(OUTDIR)/linux_boot_demo
TEST  = $(OUTDIR)/test_runner

.PHONY: all clean test

all: $(OUTDIR) $(DEMO1) $(DEMO2) $(DEMO3)

test: $(OUTDIR) $(TEST)
	@echo "=== Running Tests ==="
	./$(TEST)
	@echo "=== All Tests Passed ==="

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(DEMO1): examples/mbr_boot_demo.c $(COMMON_SRCS)
	$(CC) $(CFLAGS) -o $@ examples/mbr_boot_demo.c $(SRCS_STAGE1) $(SRCS_STAGE2) $(LDFLAGS)

$(DEMO2): examples/multiboot_demo.c $(COMMON_SRCS)
	$(CC) $(CFLAGS) -o $@ examples/multiboot_demo.c $(SRCS_STAGE1) $(SRCS_STAGE2) $(LDFLAGS)

$(DEMO3): examples/linux_boot_demo.c $(COMMON_SRCS)
	$(CC) $(CFLAGS) -o $@ examples/linux_boot_demo.c $(SRCS_LINUX) $(SRCS_STAGE1) $(SRCS_STAGE2) $(LDFLAGS)

$(TEST): tests/test_all.c $(ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ tests/test_all.c $(ALL_SRCS) $(LDFLAGS)

clean:
	rm -rf $(OUTDIR)
	rm -f disk_mbr.bin
