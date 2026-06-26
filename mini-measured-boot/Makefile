CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -I include -std=c99
LDFLAGS = -lm
OUTDIR  = bin
GCCLIB  = D:/msys64/mingw64/lib/gcc/x86_64-w64-mingw32/15.2.0

ifeq ($(OS),Windows_NT)
  export PATH := $(PATH);$(GCCLIB)
endif

SRC       = src/tpm2_structs.c src/pcr_bank.c src/event_log.c src/crtm_drtm.c src/tpm_locality.c \
            src/nv_storage.c src/tpm_keys.c src/attestation.c
OBJ       = $(SRC:src/%.c=$(OUTDIR)/%.o)
EXAMPLES  = examples/tpm_extend_demo.c examples/event_log_demo.c examples/txt_demo.c
TARGETS   = $(OUTDIR)/tpm_extend_demo.exe $(OUTDIR)/event_log_demo.exe $(OUTDIR)/txt_demo.exe

.PHONY: all clean run-extend run-event run-txt test bench run-test run-bench

all: $(TARGETS)

$(OUTDIR):
	@mkdir -p "$(OUTDIR)"

$(OUTDIR)/%.o: src/%.c | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/tpm_extend_demo.exe: $(OBJ) examples/tpm_extend_demo.c | $(OUTDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(OUTDIR)/event_log_demo.exe: $(OBJ) examples/event_log_demo.c | $(OUTDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(OUTDIR)/txt_demo.exe: $(OBJ) examples/txt_demo.c | $(OUTDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

run-extend: $(OUTDIR)/tpm_extend_demo.exe
	"$(OUTDIR)\tpm_extend_demo.exe"

run-event: $(OUTDIR)/event_log_demo.exe
	"$(OUTDIR)\event_log_demo.exe"

run-txt: $(OUTDIR)/txt_demo.exe
	"$(OUTDIR)\txt_demo.exe"

test: $(OUTDIR)/test_all.exe
	@"$(OUTDIR)\test_all.exe"

bench: $(OUTDIR)/bench_pcr.exe
	@"$(OUTDIR)\bench_pcr.exe"

run-test: test
run-bench: bench

$(OUTDIR)/test_all.exe: $(OBJ) tests/test_all.c | $(OUTDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(OUTDIR)/bench_pcr.exe: $(OBJ) benches/bench_pcr.c | $(OUTDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	@rm -rf "$(OUTDIR)"
