CC = gcc
CFLAGS = -Wall -Wextra -O2 -I include
LDFLAGS = -lm
OUTDIR = bin

SRCS = src/uefi_sb.c src/signature_verify.c src/trust_chain.c \
       src/firmware_update.c src/root_of_trust.c \
       src/tpm.c src/measured_boot.c src/key_mgmt.c src/boot_policy.c
OBJS = $(SRCS:src/%.c=$(OUTDIR)/%.o)

EXAMPLES = $(OUTDIR)/secure_boot_demo.exe \
           $(OUTDIR)/signature_demo.exe \
           $(OUTDIR)/fit_verify_demo.exe

TEST = $(OUTDIR)/test_all.exe

.PHONY: all clean run-secure-boot run-signature run-fit-verify test run-test

all: $(OUTDIR) $(EXAMPLES) $(TEST)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUTDIR)/%.o: src/%.c | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/secure_boot_demo.exe: $(OBJS) examples/secure_boot_demo.c | $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ examples/secure_boot_demo.c $(OBJS) $(LDFLAGS)

$(OUTDIR)/signature_demo.exe: $(OBJS) examples/signature_demo.c | $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ examples/signature_demo.c $(OBJS) $(LDFLAGS)

$(OUTDIR)/fit_verify_demo.exe: $(OBJS) examples/fit_verify_demo.c | $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ examples/fit_verify_demo.c $(OBJS) $(LDFLAGS)

$(OUTDIR)/test_all.exe: $(OBJS) tests/test_all.c | $(OUTDIR)
	$(CC) $(CFLAGS) -Wl,--stack,16777216 -o $@ tests/test_all.c $(OBJS) $(LDFLAGS)

test: $(OUTDIR)/test_all.exe
	$(OUTDIR)\test_all.exe

run-test: test

run-secure-boot: $(OUTDIR)/secure_boot_demo.exe
	$(OUTDIR)\secure_boot_demo.exe

run-signature: $(OUTDIR)/signature_demo.exe
	$(OUTDIR)\signature_demo.exe

run-fit-verify: $(OUTDIR)/fit_verify_demo.exe
	$(OUTDIR)\fit_verify_demo.exe

clean:
	rm -rf $(OUTDIR)
