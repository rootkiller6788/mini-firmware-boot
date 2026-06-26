CC = gcc
CFLAGS = -Wall -Wextra -O2 -I include
LDFLAGS = -lm
OUTDIR = bin

SRCDIR = src
EXDIR  = examples

SRCS = $(SRCDIR)/spi_protection.c \
       $(SRCDIR)/smm_attacks.c \
       $(SRCDIR)/dma_attacks.c \
       $(SRCDIR)/bmc_me.c \
       $(SRCDIR)/firmware_resiliency.c

OBJS = $(SRCS:$(SRCDIR)/%.c=$(OUTDIR)/%.o)

EXAMPLE_SRCS = $(EXDIR)/spi_lock_demo.c \
               $(EXDIR)/smm_attack_demo.c \
               $(EXDIR)/iommu_demo.c

EXAMPLES = $(EXAMPLE_SRCS:$(EXDIR)/%.c=$(OUTDIR)/%)

.PHONY: all clean run-spi run-smm run-iommu

all: $(OUTDIR) $(OBJS) $(EXAMPLES)

$(OUTDIR):
	@if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"

$(OUTDIR)/spi_protection.o: $(SRCDIR)/spi_protection.c include/spi_protection.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/smm_attacks.o: $(SRCDIR)/smm_attacks.c include/smm_attacks.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/dma_attacks.o: $(SRCDIR)/dma_attacks.c include/dma_attacks.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/bmc_me.o: $(SRCDIR)/bmc_me.c include/bmc_me.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/firmware_resiliency.o: $(SRCDIR)/firmware_resiliency.c include/firmware_resiliency.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/spi_lock_demo: $(EXDIR)/spi_lock_demo.c $(OUTDIR)/spi_protection.o
	$(CC) $(CFLAGS) $< $(OUTDIR)/spi_protection.o -o $@ $(LDFLAGS)

$(OUTDIR)/smm_attack_demo: $(EXDIR)/smm_attack_demo.c $(OUTDIR)/smm_attacks.o
	$(CC) $(CFLAGS) $< $(OUTDIR)/smm_attacks.o -o $@ $(LDFLAGS)

$(OUTDIR)/iommu_demo: $(EXDIR)/iommu_demo.c $(OUTDIR)/dma_attacks.o
	$(CC) $(CFLAGS) $< $(OUTDIR)/dma_attacks.o -o $@ $(LDFLAGS)

run-spi: $(OUTDIR)/spi_lock_demo
	$(OUTDIR)/spi_lock_demo

run-smm: $(OUTDIR)/smm_attack_demo
	$(OUTDIR)/smm_attack_demo

run-iommu: $(OUTDIR)/iommu_demo
	$(OUTDIR)/iommu_demo

clean:
	@if exist "$(OUTDIR)" rmdir /s /q "$(OUTDIR)"
