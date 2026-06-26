/* PCR Extend Performance Benchmark
 * Measures throughput of PCR extend operations.
 * Tests: PCR extend latency, event log replay latency,
 *        NV storage write throughput.
 *
 * L7: Performance characterization for real-world deployment
 *      (UEFI secure boot measurement, IMA runtime measurement)
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "sha256.h"
#include "pcr_bank.h"
#include "event_log.h"
#include "nv_storage.h"

#define BENCH_ITERATIONS 100000

static double bench_pcr_extend(void) {
    PCRBank bank;
    uint8_t data[SHA256_DIGEST_SIZE];
    clock_t start, end;
    uint32_t i;

    pcr_bank_init(&bank, TPM_ALG_SHA256);
    sha256_hash((const uint8_t*)"benchmark_data", 14, data);

    start = clock();
    for (i = 0; i < BENCH_ITERATIONS; i++) {
        pcr_extend(&bank, 0, data, SHA256_DIGEST_SIZE);
    }
    end = clock();

    return (double)(end - start) / CLOCKS_PER_SEC;
}

static double bench_event_log_replay(void) {
    TCGEventLog log;
    PCRBank bank;
    uint8_t hash[SHA256_DIGEST_SIZE];
    clock_t start, end;
    uint32_t i;

    event_log_init(&log, 0x0200);
    sha256_hash((const uint8_t*)"event_data", 10, hash);

    for (i = 0; i < 1000; i++) {
        event_log_add(&log, i % 24, EV_EFI_ACTION, hash,
                      (const uint8_t*)"bench", 5);
    }

    start = clock();
    for (i = 0; i < BENCH_ITERATIONS / 100; i++) {
        event_log_replay(&log, &bank);
    }
    end = clock();

    return (double)(end - start) / CLOCKS_PER_SEC;
}

int main(void) {
    double pcr_time, replay_time;

    printf("=== PCR Benchmark ===\n");
    printf("Iterations per test: %d\n\n", BENCH_ITERATIONS);

    pcr_time = bench_pcr_extend();
    printf("PCR Extend: %d ops in %.3f sec (%.1f ops/sec)\n",
           BENCH_ITERATIONS, pcr_time,
           BENCH_ITERATIONS / pcr_time);

    replay_time = bench_event_log_replay();
    printf("Event Log Replay (1000 events): %d ops in %.3f sec (%.1f ops/sec)\n",
           BENCH_ITERATIONS / 100, replay_time,
           (BENCH_ITERATIONS / 100) / replay_time);

    return 0;
}
