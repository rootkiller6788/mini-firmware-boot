CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -I include -std=c99
LDFLAGS  = -lm
OUTDIR   = bin

SRCS     = src/tpm_quote.c src/aik_identity.c src/attest_protocol.c \
           src/verifier_service.c src/rats.c
OBJS     = $(SRCS:src/%.c=$(OUTDIR)/%.o)

QUOTE_DEMO    = $(OUTDIR)/tpm_quote_demo.exe
ATTEST_DEMO   = $(OUTDIR)/attest_demo.exe
PRIVCA_DEMO   = $(OUTDIR)/privacy_ca_demo.exe

DEMOS    = $(QUOTE_DEMO) $(ATTEST_DEMO) $(PRIVCA_DEMO)

.PHONY: all clean run-quote run-attest run-privacy-ca

all: $(DEMOS)

$(OUTDIR)/%.o: src/%.c
	if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"
	$(CC) $(CFLAGS) -c $< -o $@

$(QUOTE_DEMO): examples/tpm_quote_demo.c $(OBJS)
	$(CC) $(CFLAGS) $< $(OBJS) $(LDFLAGS) -o $@

$(ATTEST_DEMO): examples/attest_demo.c $(OBJS)
	$(CC) $(CFLAGS) $< $(OBJS) $(LDFLAGS) -o $@

$(PRIVCA_DEMO): examples/privacy_ca_demo.c $(OBJS)
	$(CC) $(CFLAGS) $< $(OBJS) $(LDFLAGS) -o $@

run-quote: $(QUOTE_DEMO)
	$(QUOTE_DEMO)

run-attest: $(ATTEST_DEMO)
	$(ATTEST_DEMO)

run-privacy-ca: $(PRIVCA_DEMO)
	$(PRIVCA_DEMO)

clean:
	if exist "$(OUTDIR)" rmdir /s /q "$(OUTDIR)"
