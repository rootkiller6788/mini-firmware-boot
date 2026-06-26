CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -I include -std=c99
LDFLAGS  = -lm
OUTDIR   = bin

SRCS     = src/tpm_quote.c src/aik_identity.c src/attest_protocol.c \
           src/verifier_service.c src/rats.c \
           src/eventlog.c src/merkle_pcr.c src/dice.c
OBJS     = $(SRCS:src/%.c=$(OUTDIR)/%.o)

QUOTE_DEMO    = $(OUTDIR)/tpm_quote_demo.exe
ATTEST_DEMO   = $(OUTDIR)/attest_demo.exe
PRIVCA_DEMO   = $(OUTDIR)/privacy_ca_demo.exe
TEST_BIN      = $(OUTDIR)/test_suite.exe

DEMOS    = $(QUOTE_DEMO) $(ATTEST_DEMO) $(PRIVCA_DEMO)

.PHONY: all test clean run-quote run-attest run-privacy-ca

all: $(DEMOS)

test: $(TEST_BIN)
	$(TEST_BIN)

$(OUTDIR)/%.o: src/%.c
	mkdir -p "$(OUTDIR)"
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

$(TEST_BIN): tests/test_suite.c $(OBJS)
	$(CC) $(CFLAGS) $< $(OBJS) $(LDFLAGS) -o $@

clean:
	rm -rf "$(OUTDIR)"
